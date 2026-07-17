#pragma once

#include <cstdint>
#include <functional>
#include <string>

// Forward-declared to avoid leaking <libevdev/libevdev.h> into every
// translation unit that includes this header.
struct libevdev;

namespace vwheel {

/// Owns exclusive access to one physical keyboard device via libevdev.
///
/// KeyboardReader opens an evdev device node, optionally grabs it with
/// EVIOCGRAB (via libevdev_grab) so events stop reaching every other
/// process — including the game and the X/Wayland compositor — and
/// forwards decoded key press/release events to a caller-supplied
/// callback. The physical device's LEDs can also be driven directly,
/// which is used for the optional Scroll Lock driving-mode indicator.
///
/// The underlying file descriptor is exposed via fd() so the owner can
/// register it with epoll; KeyboardReader itself does no blocking I/O
/// beyond a single non-blocking read per processEvents() call.
class KeyboardReader {
public:
    struct KeyEvent {
        std::uint16_t code = 0; // KEY_* code
        bool pressed = false;   // true = key down, false = key up
    };

    using KeyEventCallback = std::function<void(const KeyEvent&)>;

    /// Opens `devicePath`. If `devicePath` is empty, scans /dev/input for
    /// the first device that looks like a keyboard (see autoDetectDevice).
    /// Throws std::runtime_error on failure to open or identify a device.
    explicit KeyboardReader(std::string devicePath);
    ~KeyboardReader();

    KeyboardReader(const KeyboardReader&) = delete;
    KeyboardReader& operator=(const KeyboardReader&) = delete;

    /// The evdev device's file descriptor, suitable for epoll.
    int fd() const noexcept { return fd_; }

    /// Requests exclusive access to the device (EVIOCGRAB). While grabbed,
    /// no other process — including the X/Wayland compositor — receives
    /// events from this device. Safe to call when already grabbed.
    void grab();

    /// Releases exclusive access. Safe to call when not currently grabbed.
    /// Also called automatically by the destructor so a crash or normal
    /// exit never leaves the physical keyboard unusable.
    void release();

    bool isGrabbed() const noexcept { return grabbed_; }

    /// Installs the callback invoked for every key press/release decoded
    /// by processEvents(). Replaces any previously installed callback.
    void setKeyEventCallback(KeyEventCallback callback);

    /// Drains and dispatches all currently pending events. Intended to be
    /// called when epoll reports fd() as readable; returns immediately
    /// once the device's read queue is empty (EAGAIN).
    void processEvents();

    /// Directly sets a keyboard LED (e.g. LED_SCROLLL) by writing an
    /// EV_LED event to the device. Best-effort: failures are logged at
    /// debug level and otherwise ignored, since not all keyboards expose
    /// software-controllable LEDs.
    void setLed(std::uint16_t ledCode, bool on);

    /// Scans /dev/input/event* for the first device that exposes a full
    /// alphanumeric key range (KEY_A, KEY_ENTER, KEY_SPACE) and no
    /// pointer/joystick absolute axes. Throws std::runtime_error if none
    /// is found.
    static std::string autoDetectDevice();

    const std::string& devicePath() const noexcept { return devicePath_; }

private:
    void openDevice();

    std::string devicePath_;
    int fd_ = -1;
    libevdev* dev_ = nullptr;
    bool grabbed_ = false;
    KeyEventCallback callback_;
};

} // namespace vwheel
