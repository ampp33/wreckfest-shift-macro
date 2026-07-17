#include "input/KeyboardReader.h"

#include <libevdev/libevdev.h>
#include <linux/input.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include "logging/Logger.h"

namespace vwheel {

namespace fs = std::filesystem;

namespace {

/// True if `dev` exposes a plausible full keyboard key range and is not a
/// mouse/joystick/touchpad masquerading as one (those expose EV_ABS axes
/// like ABS_X for pointer or stick position; real keyboards don't).
bool looksLikeKeyboard(libevdev* dev) {
    return libevdev_has_event_code(dev, EV_KEY, KEY_A) &&
           libevdev_has_event_code(dev, EV_KEY, KEY_ENTER) &&
           libevdev_has_event_code(dev, EV_KEY, KEY_SPACE) &&
           !libevdev_has_event_code(dev, EV_ABS, ABS_X);
}

} // namespace

KeyboardReader::KeyboardReader(std::string devicePath) : devicePath_(std::move(devicePath)) {
    openDevice();
}

KeyboardReader::~KeyboardReader() {
    release();
    if (dev_ != nullptr) {
        libevdev_free(dev_);
        dev_ = nullptr;
    }
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

void KeyboardReader::openDevice() {
    if (devicePath_.empty()) {
        devicePath_ = autoDetectDevice();
        Logger::instance().info("Auto-detected keyboard device: " + devicePath_);
    }

    // O_RDWR (not O_RDONLY) so we can also write EV_LED events back to the
    // device for the optional Scroll Lock mode indicator.
    fd_ = open(devicePath_.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        throw std::runtime_error("failed to open keyboard device '" + devicePath_ +
                                  "': " + std::strerror(errno));
    }

    const int rc = libevdev_new_from_fd(fd_, &dev_);
    if (rc < 0) {
        close(fd_);
        fd_ = -1;
        throw std::runtime_error("libevdev_new_from_fd failed for '" + devicePath_ +
                                  "': " + std::strerror(-rc));
    }

    Logger::instance().info("Opened keyboard '" + std::string(libevdev_get_name(dev_)) + "' at " +
                             devicePath_);
}

std::string KeyboardReader::autoDetectDevice() {
    std::vector<std::string> candidates;
    for (const auto& entry : fs::directory_iterator("/dev/input")) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("event", 0) != 0) {
            continue;
        }
        candidates.push_back(entry.path().string());
    }
    std::sort(candidates.begin(), candidates.end());

    for (const auto& path : candidates) {
        const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            continue;
        }
        libevdev* dev = nullptr;
        const int rc = libevdev_new_from_fd(fd, &dev);
        if (rc == 0 && looksLikeKeyboard(dev)) {
            libevdev_free(dev);
            close(fd);
            return path;
        }
        if (dev != nullptr) {
            libevdev_free(dev);
        }
        close(fd);
    }

    throw std::runtime_error(
        "could not auto-detect a keyboard device under /dev/input; "
        "set [keyboard].device explicitly in the config file");
}

void KeyboardReader::grab() {
    if (grabbed_) {
        return;
    }
    const int rc = libevdev_grab(dev_, LIBEVDEV_GRAB);
    if (rc < 0) {
        throw std::runtime_error("EVIOCGRAB failed on '" + devicePath_ +
                                  "': " + std::strerror(-rc));
    }
    grabbed_ = true;
    Logger::instance().debug("Grabbed keyboard " + devicePath_);
}

void KeyboardReader::release() {
    if (!grabbed_) {
        return;
    }
    // Best-effort: releasing should never throw, since it runs from
    // destructors and emergency-recovery paths.
    libevdev_grab(dev_, LIBEVDEV_UNGRAB);
    grabbed_ = false;
    Logger::instance().debug("Released keyboard " + devicePath_);
}

void KeyboardReader::setKeyEventCallback(KeyEventCallback callback) {
    callback_ = std::move(callback);
}

void KeyboardReader::processEvents() {
    input_event ev{};

    for (;;) {
        const int rc = libevdev_next_event(dev_, LIBEVDEV_READ_FLAG_NORMAL, &ev);

        if (rc == -EAGAIN) {
            return; // queue drained
        }

        if (rc == LIBEVDEV_READ_STATUS_SYNC) {
            // We fell behind (e.g. a burst of events); resynchronize by
            // draining the forced sync events libevdev replays for us.
            while (rc == LIBEVDEV_READ_STATUS_SYNC) {
                if (ev.type == EV_KEY && callback_) {
                    callback_(KeyEvent{static_cast<std::uint16_t>(ev.code), ev.value != 0});
                }
                if (libevdev_next_event(dev_, LIBEVDEV_READ_FLAG_SYNC, &ev) !=
                    LIBEVDEV_READ_STATUS_SYNC) {
                    break;
                }
            }
            continue;
        }

        if (rc != LIBEVDEV_READ_STATUS_SUCCESS) {
            if (rc < 0) {
                Logger::instance().warning("Keyboard read error: " + std::string(std::strerror(-rc)));
            }
            return;
        }

        if (ev.type == EV_KEY && ev.value != 2 /* ignore autorepeat */ && callback_) {
            callback_(KeyEvent{static_cast<std::uint16_t>(ev.code), ev.value != 0});
        }
    }
}

void KeyboardReader::setLed(std::uint16_t ledCode, bool on) {
    input_event events[2]{};
    events[0].type = EV_LED;
    events[0].code = ledCode;
    events[0].value = on ? 1 : 0;
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;
    events[1].value = 0;

    if (write(fd_, events, sizeof(events)) < 0) {
        Logger::instance().debug("Failed to set keyboard LED " + std::to_string(ledCode) + ": " +
                                  std::strerror(errno));
    }
}

} // namespace vwheel
