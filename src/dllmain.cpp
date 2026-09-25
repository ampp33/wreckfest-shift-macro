// Wreckfest Shift Macro — Ultimate ASI Loader plugin entry point.
//
// Loaded into the game process by Ultimate ASI Loader (from the game's
// scripts/ folder). Wires together the components (KeyboardHook,
// VirtualController + XInputHook, ClutchController, ModeManager, Logger):
//
//   game window thread  --WH_KEYBOARD-->  KeyboardHook  --> ModeManager
//   plugin tick thread  --250 Hz------->  ModeManager::tick (steering ramp),
//                                         focus-loss release, config reload
//   ModeManager / ClutchController  --> VirtualController
//   game's XInputGetState()         <-- VirtualController (via XInputHook)
//
// The plugin lives for the whole game process and never tears itself
// down: at process exit Windows has already killed our threads before
// DLL_PROCESS_DETACH, so joining them there would hang, and there is
// nothing to clean up that the process exit doesn't already take care of
// (no grabbed devices or kernel objects, unlike the Linux daemon).

#include <windows.h>
#include <mmsystem.h>

#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "config/Config.h"
#include "controller/VirtualController.h"
#include "hooks/KeyboardHook.h"
#include "hooks/XInputHook.h"
#include "logging/Logger.h"
#include "modes/ModeManager.h"
#include "timing/ClutchController.h"
#include "timing/ShiftObserver.h"

namespace {

using namespace vcontroller;
namespace fs = std::filesystem;

/// Steering ramp update rate. 250 Hz gives smooth digital-to-analog
/// steering feel without meaningfully loading the CPU.
constexpr auto kTickInterval = std::chrono::milliseconds(4);

/// How often the tick thread checks the config file for changes.
constexpr auto kConfigPollInterval = std::chrono::seconds(1);

/// How often the game's XInput poll timing is summarized in the log
/// (debug level only). Logged from here rather than from the hook so the
/// game's input thread never does file I/O.
constexpr auto kPollTimingLogInterval = std::chrono::seconds(5);

ShiftObserver::Settings shiftObserverSettings(const Config& config) {
    const auto& clutch = config.clutch;
    ShiftObserver::Settings settings;
    settings.clutchEnabled = clutch.enabled;
    settings.clutchAxis = clutch.axis;
    settings.clutchReleaseValue = clutch.releaseValue;
    settings.timingLabel =
        !clutch.enabled ? "clutch disabled"
        : clutch.frameTiming
            ? "frames " + std::to_string(clutch.pressDelayFrames) + "/" +
                  std::to_string(clutch.releaseDelayFrames)
            : "ms " + std::to_string(clutch.pressDelay.count()) + "/" +
                  std::to_string(clutch.releaseDelay.count());
    return settings;
}

/// True if the foreground window belongs to this (the game's) process.
bool gameHasFocus() {
    const HWND foreground = GetForegroundWindow();
    if (foreground == nullptr) {
        return false;
    }
    DWORD processId = 0;
    GetWindowThreadProcessId(foreground, &processId);
    return processId == GetCurrentProcessId();
}

class Plugin {
public:
    Plugin(fs::path configPath, const Config& config)
        : configPath_(std::move(configPath)),
          clutch_(controller_, ClutchController::Settings::fromConfig(config.clutch)),
          modeManager_(controller_, clutch_, config) {
        std::error_code ec;
        configWriteTime_ = fs::last_write_time(configPath_, ec);
        shiftObserver_.updateSettings(shiftObserverSettings(config));
    }

    void start(HMODULE gameModule, const Config& config) {
        XInputHook::Listeners listeners;
        listeners.beforePoll = [this] { clutch_.onGamePoll(); };
        listeners.afterPoll = [this](const XINPUT_GAMEPAD& seen) {
            shiftObserver_.observe(seen, std::chrono::steady_clock::now());
        };
        if (!XInputHook::install(gameModule, controller_, config.controller.slot,
                                 std::move(listeners))) {
            Logger::instance().error(
                "Game executable doesn't import XInputGetState; the virtual controller "
                "can't be connected. Is this the 64-bit Wreckfest_x64.exe?");
            return;
        }
        if (!KeyboardHook::install(gameModule, [this](const KeyboardHook::KeyEvent& event) {
                std::lock_guard<std::mutex> lock(mutex_);
                return modeManager_.handleKeyEvent(event);
            })) {
            Logger::instance().error(
                "Game executable doesn't import SetWindowsHookEx; can't intercept keys.");
            return;
        }

        // 1 ms timer resolution so the 4 ms tick and ClutchController's
        // millisecond-scale shift delays are honored (the Windows default
        // is ~15.6 ms). A no-op cost under Wine, which is already precise.
        timeBeginPeriod(1);
        tickThread_ = std::thread(&Plugin::tickLoop, this);

        Logger::instance().info(
            "Ready. Starting in Chat Mode — press the configured driving hotkey in-game to "
            "begin translating input.");
    }

private:
    void tickLoop() {
        auto lastTick = std::chrono::steady_clock::now();
        auto lastConfigPoll = lastTick;
        auto lastPollTimingLog = lastTick;
        bool hadFocus = false; // the game window doesn't exist yet at startup
        bool warnedNoKeyboardHook = false;

        while (true) {
            std::this_thread::sleep_for(kTickInterval);
            const auto now = std::chrono::steady_clock::now();

            const bool hasFocus = gameHasFocus();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (hadFocus && !hasFocus) {
                    // Key releases now go to whatever window took focus;
                    // don't leave the throttle (or a gear) held down.
                    modeManager_.releaseHeldInputs();
                }
                modeManager_.tick(now - lastTick);
            }
            hadFocus = hasFocus;
            lastTick = now;

            for (const std::string& report : shiftObserver_.takeReports()) {
                Logger::instance().debug(report);
            }

            if (now - lastConfigPoll >= kConfigPollInterval) {
                lastConfigPoll = now;
                reloadConfigIfChanged();
                if (!warnedNoKeyboardHook && hasFocus && !KeyboardHook::isActive()) {
                    warnedNoKeyboardHook = true;
                    Logger::instance().warning(
                        "Game window is focused but the game hasn't installed its keyboard hook "
                        "through us yet; hotkeys won't work until it does.");
                }
            }

            if (now - lastPollTimingLog >= kPollTimingLogInterval) {
                lastPollTimingLog = now;
                XInputHook::logPollTiming();
            }
        }
    }

    void reloadConfigIfChanged() {
        std::error_code ec;
        const auto writeTime = fs::last_write_time(configPath_, ec);
        if (ec || writeTime == configWriteTime_) {
            return;
        }
        configWriteTime_ = writeTime;

        Logger::instance().info("Config file changed, reloading");
        try {
            const Config reloaded = Config::loadFromFile(configPath_);
            Logger::instance().setLevel(reloaded.logLevel);
            XInputHook::setSlot(reloaded.controller.slot);
            shiftObserver_.updateSettings(shiftObserverSettings(reloaded));
            std::lock_guard<std::mutex> lock(mutex_);
            modeManager_.applyConfig(reloaded);
        } catch (const std::exception& ex) {
            Logger::instance().error(
                std::string("Config reload failed, keeping previous configuration: ") + ex.what());
        }
    }

    fs::path configPath_;
    fs::file_time_type configWriteTime_{};

    VirtualController controller_;
    ClutchController clutch_;
    // Serializes every ModeManager call: key events arrive on the game's
    // window thread, ticks and reloads on tickThread_.
    std::mutex mutex_;
    ModeManager modeManager_;
    ShiftObserver shiftObserver_;
    std::thread tickThread_;
};

fs::path modulePath(HMODULE module) {
    std::wstring buffer(MAX_PATH, L'\0');
    while (true) {
        const DWORD length =
            GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length < buffer.size()) {
            buffer.resize(length);
            return fs::path(buffer);
        }
        buffer.resize(buffer.size() * 2);
    }
}

void startPlugin(HMODULE self) {
    // Config and log sit next to the plugin, named after it:
    // scripts/wreckfest-shift-macro.asi -> .toml / .log
    const fs::path pluginPath = modulePath(self);
    fs::path configPath = pluginPath;
    configPath.replace_extension(".toml");
    fs::path logPath = pluginPath;
    logPath.replace_extension(".log");

    Logger::instance().openFile(logPath);

    try {
        const Config config = Config::loadFromFile(configPath);
        Logger::instance().setLevel(config.logLevel);
        Logger::instance().info("Loaded config from " + configPath.string());

        // Intentionally leaked — see the comment at the top of this file.
        auto* plugin = new Plugin(configPath, config);
        plugin->start(GetModuleHandleW(nullptr), config);
    } catch (const std::exception& ex) {
        Logger::instance().error(std::string("Plugin disabled: ") + ex.what());
    }
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        // Ultimate ASI Loader loads plugins before the game's own startup
        // code runs, so the import patches land before the game ever calls
        // SetWindowsHookEx or XInputGetState. Everything here is safe under
        // the loader lock: no LoadLibrary, and the threads started here
        // aren't waited on.
        startPlugin(instance);
    }
    return TRUE;
}
