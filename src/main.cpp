// Wreckfest Virtual Wheel — entry point.
//
// Wires together the five components (KeyboardReader, VirtualController,
// ClutchController, ModeManager, Logger) around a single epoll-driven
// event loop. There is no polling anywhere: the loop blocks in
// epoll_wait() until the keyboard has events, a timer fires, or a signal
// arrives.

#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>

#include "config/Config.h"
#include "controller/VirtualController.h"
#include "input/KeyboardReader.h"
#include "logging/Logger.h"
#include "modes/ModeManager.h"
#include "timing/ClutchController.h"

namespace {

using namespace vwheel;
namespace fs = std::filesystem;

/// Steering ramp update rate. 250 Hz gives smooth digital-to-analog
/// steering feel without meaningfully loading the CPU.
constexpr auto kSteeringTickInterval = std::chrono::milliseconds(4);

/// Resolves the config file to load: an explicit CLI argument always
/// wins; otherwise we try a couple of sensible defaults so the daemon
/// works both from a source checkout (`./build/virtual-wheel`) and from
/// an installed layout.
fs::path resolveConfigPath(int argc, char** argv) {
    if (argc > 1) {
        return fs::path(argv[1]);
    }

    std::array<fs::path, 3> candidates{
        fs::path("config/default.toml"),
        fs::path("/etc/wreckfest-virtual-wheel/config.toml"),
        fs::path(),
    };

    // Executable-relative fallback: build/virtual-wheel -> ../config/default.toml
    std::error_code ec;
    const fs::path exePath = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        candidates[2] = exePath.parent_path() / ".." / "config" / "default.toml";
    }

    for (const auto& candidate : candidates) {
        if (!candidate.empty() && fs::exists(candidate)) {
            return candidate;
        }
    }

    throw std::runtime_error(
        "no config file found (looked for config/default.toml, "
        "/etc/wreckfest-virtual-wheel/config.toml, and next to the executable); "
        "pass a path explicitly: virtual-wheel /path/to/config.toml");
}

/// Wraps a signalfd configured for SIGINT/SIGTERM/SIGHUP. Blocking these
/// signals via the process mask (done in the constructor) is what
/// redirects them into the fd instead of the default handlers/terminal
/// signal disposition, so this must be constructed before any other
/// thread is spawned.
class SignalFd {
public:
    SignalFd() {
        sigemptyset(&mask_);
        sigaddset(&mask_, SIGINT);
        sigaddset(&mask_, SIGTERM);
        sigaddset(&mask_, SIGHUP);

        if (sigprocmask(SIG_BLOCK, &mask_, nullptr) != 0) {
            throw std::runtime_error(std::string("sigprocmask failed: ") + std::strerror(errno));
        }

        fd_ = signalfd(-1, &mask_, SFD_NONBLOCK | SFD_CLOEXEC);
        if (fd_ < 0) {
            throw std::runtime_error(std::string("signalfd failed: ") + std::strerror(errno));
        }
    }

    ~SignalFd() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    int fd() const noexcept { return fd_; }

    /// Reads one pending signal, if any. Returns nullopt if none pending.
    std::optional<int> poll() {
        signalfd_siginfo info{};
        const ssize_t n = read(fd_, &info, sizeof(info));
        if (n != static_cast<ssize_t>(sizeof(info))) {
            return std::nullopt;
        }
        return static_cast<int>(info.ssi_signo);
    }

private:
    sigset_t mask_{};
    int fd_ = -1;
};

/// Wraps a CLOCK_MONOTONIC timerfd firing every `interval`.
class TimerFd {
public:
    explicit TimerFd(std::chrono::nanoseconds interval) {
        fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (fd_ < 0) {
            throw std::runtime_error(std::string("timerfd_create failed: ") + std::strerror(errno));
        }

        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(interval);
        const auto nanoseconds = interval - seconds;

        itimerspec spec{};
        spec.it_interval.tv_sec = seconds.count();
        spec.it_interval.tv_nsec = nanoseconds.count();
        spec.it_value = spec.it_interval;

        if (timerfd_settime(fd_, 0, &spec, nullptr) != 0) {
            throw std::runtime_error(std::string("timerfd_settime failed: ") + std::strerror(errno));
        }
    }

    ~TimerFd() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    int fd() const noexcept { return fd_; }

    /// Drains the expiration counter. Must be called on every readable
    /// event or the fd stays permanently ready.
    void drain() {
        std::uint64_t expirations = 0;
        const ssize_t n = read(fd_, &expirations, sizeof(expirations));
        (void)n; // expiration count is unused: main loop derives dt from steady_clock instead
    }

private:
    int fd_ = -1;
};

/// Thin RAII wrapper around an epoll instance plus its fd registrations.
class EpollLoop {
public:
    EpollLoop() {
        fd_ = epoll_create1(EPOLL_CLOEXEC);
        if (fd_ < 0) {
            throw std::runtime_error(std::string("epoll_create1 failed: ") + std::strerror(errno));
        }
    }

    ~EpollLoop() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    void add(int watchedFd) {
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = watchedFd;
        if (epoll_ctl(fd_, EPOLL_CTL_ADD, watchedFd, &ev) != 0) {
            throw std::runtime_error(std::string("epoll_ctl(ADD) failed: ") + std::strerror(errno));
        }
    }

    /// Blocks until at least one registered fd is readable, then invokes
    /// `onReadable` for each one. Never busy-polls.
    template <typename OnReadable>
    void waitOnce(OnReadable&& onReadable) {
        std::array<epoll_event, 8> events{};
        const int n = epoll_wait(fd_, events.data(), static_cast<int>(events.size()), -1);
        if (n < 0) {
            if (errno == EINTR) {
                return;
            }
            throw std::runtime_error(std::string("epoll_wait failed: ") + std::strerror(errno));
        }
        for (int i = 0; i < n; ++i) {
            onReadable(events[static_cast<std::size_t>(i)].data.fd);
        }
    }

private:
    int fd_ = -1;
};

int runDaemon(int argc, char** argv) {
    const fs::path configPath = resolveConfigPath(argc, argv);
    Config config = Config::loadFromFile(configPath);
    Logger::instance().setLevel(config.logLevel);
    Logger::instance().info("Loaded config from " + configPath.string());

    // Order matters here: destruction happens in reverse, and we need the
    // clutch worker thread stopped (releasing any in-flight gear button)
    // before the virtual controller device it writes to is torn down, and
    // the controller torn down before the keyboard grab is released.
    KeyboardReader keyboard(config.keyboard.device);
    VirtualController controller;
    ClutchController clutch(controller,
                             ClutchController::Settings{
                                 config.clutch.enabled,
                                 config.clutch.axis,
                                 config.clutch.pressValue,
                                 config.clutch.releaseValue,
                                 config.clutch.pressDelay,
                                 config.clutch.releaseDelay,
                             });
    ModeManager modeManager(keyboard, controller, clutch, config);

    bool shuttingDown = false;
    modeManager.setEmergencyCallback([&shuttingDown] {
        Logger::instance().error("EMERGENCY STOP: releasing keyboard and exiting immediately");
        shuttingDown = true;
    });
    keyboard.setKeyEventCallback(
        [&modeManager](const KeyboardReader::KeyEvent& event) { modeManager.handleKeyEvent(event); });

    SignalFd signals;
    TimerFd steeringTimer(kSteeringTickInterval);
    EpollLoop epoll;
    epoll.add(keyboard.fd());
    epoll.add(signals.fd());
    epoll.add(steeringTimer.fd());

    Logger::instance().info(
        "Ready. Starting in Chat Mode — press the configured driving hotkey to grab the "
        "keyboard and begin translating input.");

    auto lastTick = std::chrono::steady_clock::now();

    while (!shuttingDown) {
        epoll.waitOnce([&](int readyFd) {
            if (readyFd == keyboard.fd()) {
                keyboard.processEvents();
            } else if (readyFd == steeringTimer.fd()) {
                steeringTimer.drain();
                const auto now = std::chrono::steady_clock::now();
                modeManager.tick(now - lastTick);
                lastTick = now;
            } else if (readyFd == signals.fd()) {
                if (const auto signo = signals.poll()) {
                    if (*signo == SIGHUP) {
                        Logger::instance().info("SIGHUP received, reloading configuration");
                        try {
                            Config reloaded = Config::loadFromFile(configPath);
                            Logger::instance().setLevel(reloaded.logLevel);
                            modeManager.applyConfig(reloaded);
                        } catch (const std::exception& ex) {
                            Logger::instance().error(std::string("Config reload failed, keeping "
                                                                  "previous configuration: ") +
                                                      ex.what());
                        }
                    } else {
                        Logger::instance().info(std::string("Received signal ") +
                                                 strsignal(*signo) + ", shutting down");
                        shuttingDown = true;
                    }
                }
            }
        });
    }

    // Destructors now run in reverse declaration order: ModeManager,
    // ClutchController (stops its thread and releases any in-flight
    // gear/clutch state), VirtualController (UI_DEV_DESTROY), then
    // KeyboardReader (EVIOCGRAB release + close). This is the same clean
    // shutdown path whether we got here via SIGINT/SIGTERM or the
    // Ctrl+Alt+Esc emergency escape.
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // Reap notify-send children (spawned fire-and-forget by ModeManager)
    // automatically instead of accumulating zombies.
    std::signal(SIGCHLD, SIG_IGN);

    try {
        return runDaemon(argc, argv);
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("Fatal error: ") + ex.what());
        return EXIT_FAILURE;
    } catch (...) {
        Logger::instance().error("Fatal error: unknown exception");
        return EXIT_FAILURE;
    }
}
