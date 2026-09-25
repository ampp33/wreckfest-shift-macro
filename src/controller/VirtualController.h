#pragma once

#include <windows.h>
#include <xinput.h>

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace vcontroller {

/// An in-memory Xbox 360 controller that the game reads through its own
/// XInputGetState() calls (see XInputHook). There is no device, driver,
/// or other process involved: the "controller" is just the XINPUT_GAMEPAD
/// this class hands back when the game polls the configured slot.
///
/// setButton()/setAxis() stage individual state changes; call
/// syncReport() once a logical batch of changes is complete to publish
/// them. The game only ever sees published state, so grouping changes
/// this way (e.g. "clutch axis + gear button" in one frame) lets it
/// observe them atomically — the same contract the uinput-based version
/// of this class had with EV_SYN/SYN_REPORT.
///
/// Thread-safe: the game's input thread (key events, via KeyboardHook),
/// the plugin's tick thread (steering ramp), ClutchController's worker
/// thread (gear buttons, clutch axis), and the game's XInput polling all
/// touch the same instance concurrently. An internal mutex serializes
/// every access.
class VirtualController {
public:
    VirtualController() = default;

    VirtualController(const VirtualController&) = delete;
    VirtualController& operator=(const VirtualController&) = delete;

    /// Sets one of the kButton* digital buttons' state.
    void setButton(std::uint16_t code, bool pressed);

    /// Sets one of the k* axes' raw value. Range depends on the axis:
    /// sticks and the clutch axis are signed 16-bit (-32768..32767),
    /// triggers are unsigned 8-bit (0..255), and the D-pad axes are
    /// -1/0/1 (for kDpadY, -1 is up, matching the Linux hat convention
    /// the config's ABS_HAT0Y name comes from). Out-of-range values are
    /// clamped.
    void setAxis(std::uint16_t code, std::int32_t value);

    /// Publishes every setButton()/setAxis() call since the last
    /// syncReport() to the game atomically.
    void syncReport();

    /// Centers both sticks, zeroes both triggers and the D-pad, and
    /// releases every button, publishing immediately. Used on every mode
    /// switch so the game doesn't see a stuck input if the switch happens
    /// mid-press.
    void resetAllInputs();

    /// Fills `state` with the currently published controller state. Called
    /// from the game's XInputGetState() via XInputHook.
    void readState(XINPUT_STATE& state);

    /// Test hook: `observer` is invoked with every state as it is
    /// published (syncReport() and resetAllInputs()), in publication
    /// order, including publishes that change nothing. It runs with the
    /// controller's internal mutex held, so it must be quick and must not
    /// call back into this VirtualController. Pass an empty function to
    /// detach. Unused by the plugin itself.
    using PublishObserver = std::function<void(const XINPUT_GAMEPAD&)>;
    void setPublishObserver(PublishObserver observer);

    /// Reads axis `code` (one of the k* axis constants) back out of a
    /// gamepad state, in the same units setAxis() takes. 0 for an unknown
    /// code.
    static std::int32_t axisValue(const XINPUT_GAMEPAD& pad, std::uint16_t code);

    /// Human-readable name for a gear/reverse button code, e.g.
    /// "B(Gear2)", for logging. Falls back to the raw code for anything
    /// else.
    static std::string buttonName(std::uint16_t code);

    // --- Axis code constants -------------------------------------------------
    // Numbered after the Linux ABS_* codes they're named for in config
    // files (see KeyCodes::lookupAxis); the values themselves are just
    // identifiers now.
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

    // --- Button code constants: XINPUT_GAMEPAD_* bit flags --------------------
    static constexpr std::uint16_t kButtonA = XINPUT_GAMEPAD_A;
    static constexpr std::uint16_t kButtonB = XINPUT_GAMEPAD_B;
    static constexpr std::uint16_t kButtonX = XINPUT_GAMEPAD_X;
    static constexpr std::uint16_t kButtonY = XINPUT_GAMEPAD_Y;
    static constexpr std::uint16_t kButtonLB = XINPUT_GAMEPAD_LEFT_SHOULDER;
    static constexpr std::uint16_t kButtonRB = XINPUT_GAMEPAD_RIGHT_SHOULDER;
    static constexpr std::uint16_t kButtonBack = XINPUT_GAMEPAD_BACK;
    static constexpr std::uint16_t kButtonStart = XINPUT_GAMEPAD_START;
    static constexpr std::uint16_t kButtonThumbL = XINPUT_GAMEPAD_LEFT_THUMB;
    static constexpr std::uint16_t kButtonThumbR = XINPUT_GAMEPAD_RIGHT_THUMB;

    /// Fixed mapping from "gear N" (index 0..5 == gear1..gear6) to the
    /// Xbox button that represents it. Wreckfest (and most racing sims)
    /// let you bind individual gears to arbitrary controller buttons, so
    /// this is a design choice rather than a hardware constraint: it
    /// leaves Back/Start free for menu use.
    static constexpr std::array<std::uint16_t, 6> kGearButtons = {
        kButtonA, kButtonB, kButtonX, kButtonY, kButtonLB, kButtonRB,
    };
    static constexpr std::uint16_t kReverseButton = kButtonThumbL;
    /// Every button a gear or reverse can be on.
    static constexpr std::uint16_t kAllGearButtons = kButtonA | kButtonB | kButtonX | kButtonY |
                                                     kButtonLB | kButtonRB | kReverseButton;
    static constexpr std::uint16_t kHandbrakeButton = kButtonThumbR;

private:
    std::mutex mutex_;
    XINPUT_GAMEPAD staged_{};
    XINPUT_GAMEPAD published_{};
    // XInput's change counter: games may skip processing a poll whose
    // dwPacketNumber matches the previous one, so it must advance on
    // every publish.
    DWORD packetNumber_ = 0;
    PublishObserver observer_;
};

} // namespace vcontroller
