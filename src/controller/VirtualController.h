#pragma once

#include <array>
#include <cstdint>
#include <mutex>

namespace vwheel {

/// Creates and drives a virtual Xbox 360-compatible gamepad via
/// /dev/uinput. The kernel, udev, SDL2, Steam and Proton all see this as
/// an ordinary USB Xbox 360 controller (it reports the real Microsoft
/// vendor/product IDs), so no special driver support is needed on the
/// game side.
///
/// setButton()/setAxis() stage individual state changes; call
/// syncReport() once a logical batch of changes is complete to flush an
/// EV_SYN/SYN_REPORT frame. Grouping changes this way (e.g. "clutch axis
/// + gear button" in one frame) lets consumers observe them atomically.
///
/// Thread-safe: both the main/epoll thread (throttle, brake, steering,
/// handbrake) and ClutchController's worker thread (gear buttons, clutch
/// axis) write to the same instance concurrently. An internal mutex
/// serializes every write so a syncReport() from one thread can never
/// interleave with a partially-written update from the other.
class VirtualController {
public:
    VirtualController();
    ~VirtualController();

    VirtualController(const VirtualController&) = delete;
    VirtualController& operator=(const VirtualController&) = delete;

    /// Sets a BTN_* digital button's state.
    void setButton(std::uint16_t code, bool pressed);

    /// Sets an ABS_* axis's raw value. Range depends on the axis: sticks
    /// and the clutch axis are signed 16-bit (-32768..32767), triggers are
    /// unsigned 8-bit (0..255), and the D-pad hat axes are -1/0/1.
    void setAxis(std::uint16_t code, std::int32_t value);

    /// Flushes a SYN_REPORT, making all staged setButton()/setAxis() calls
    /// since the last syncReport() visible to readers atomically.
    void syncReport();

    /// Centers both sticks, zeroes both triggers and the D-pad, and
    /// releases every button. Used when entering Chat Mode so the game
    /// doesn't see a stuck input if the mode switch happens mid-press.
    void resetAllInputs();

    // --- Axis code constants -------------------------------------------------
    // Exposed so higher layers (ModeManager, ClutchController) can address
    // specific axes without depending on <linux/input-event-codes.h>
    // themselves.
    static constexpr std::uint16_t kLeftStickX = 0x00;  // ABS_X
    static constexpr std::uint16_t kLeftStickY = 0x01;  // ABS_Y
    static constexpr std::uint16_t kLeftTrigger = 0x02; // ABS_Z  (LT, 0..255)
    static constexpr std::uint16_t kRightStickX = 0x03; // ABS_RX
    static constexpr std::uint16_t kRightStickY = 0x04; // ABS_RY
    static constexpr std::uint16_t kRightTrigger = 0x05; // ABS_RZ (RT, 0..255)
    static constexpr std::uint16_t kDpadX = 0x10;        // ABS_HAT0X
    static constexpr std::uint16_t kDpadY = 0x11;        // ABS_HAT0Y

    static constexpr std::int32_t kStickMin = -32768;
    static constexpr std::int32_t kStickMax = 32767;
    static constexpr std::int32_t kTriggerMin = 0;
    static constexpr std::int32_t kTriggerMax = 255;

    // --- Button code constants (BTN_* from linux/input-event-codes.h) --------
    static constexpr std::uint16_t kButtonA = 0x130;
    static constexpr std::uint16_t kButtonB = 0x131;
    static constexpr std::uint16_t kButtonX = 0x133;
    static constexpr std::uint16_t kButtonY = 0x134;
    static constexpr std::uint16_t kButtonLB = 0x136;
    static constexpr std::uint16_t kButtonRB = 0x137;
    static constexpr std::uint16_t kButtonBack = 0x13a;
    static constexpr std::uint16_t kButtonStart = 0x13b;
    static constexpr std::uint16_t kButtonGuide = 0x13c;
    static constexpr std::uint16_t kButtonThumbL = 0x13d;
    static constexpr std::uint16_t kButtonThumbR = 0x13e;

    /// Fixed mapping from "gear N" (index 0..5 == gear1..gear6) to the
    /// Xbox button that represents it. Wreckfest (and most racing sims)
    /// let you bind individual gears to arbitrary controller buttons, so
    /// this is a design choice rather than a hardware constraint: it
    /// leaves Back/Start/Guide free for menu/overlay use.
    static constexpr std::array<std::uint16_t, 6> kGearButtons = {
        kButtonA, kButtonB, kButtonX, kButtonY, kButtonLB, kButtonRB,
    };
    static constexpr std::uint16_t kReverseButton = kButtonThumbL;
    static constexpr std::uint16_t kHandbrakeButton = kButtonThumbR;

private:
    void createDevice();
    void writeEvent(std::uint16_t type, std::uint16_t code, std::int32_t value);

    int fd_ = -1;
    std::mutex mutex_;
};

} // namespace vwheel
