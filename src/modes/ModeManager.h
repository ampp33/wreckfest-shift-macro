#pragma once

#include <chrono>
#include <cstdint>
#include <unordered_map>

#include "config/Config.h"
#include "hooks/KeyboardHook.h"

namespace vcontroller {

class VirtualController;
class ClutchController;

enum class OperatingMode { Driving, Chat, Keybinding };

/// Central orchestrator tying keyboard input to controller output.
///
/// ModeManager owns the three explicit operating modes described in the
/// project spec:
///
///   - Chat Mode (the startup default): every key reaches the game
///     untouched, the virtual controller is left centered/idle, and
///     typing in in-game chat works normally.
///   - Driving Mode: bound keys are hidden from the game and translated
///     into virtual controller input, gear shifts going through the full
///     clutch-assisted timing sequence. Unbound keys (Esc, etc.) still
///     reach the game.
///   - Keybinding Mode: bound keys are hidden from the game, same as
///     Driving Mode, but every binding is dispatched immediately and
///     literally — gear keys press/release their button directly with no
///     clutch-assist pulse or delay, and steering snaps straight to
///     +-max instead of ramping. Meant to be switched into only long
///     enough to bind each control in-game: a game's "press any input"
///     bind-detection needs one clean, unambiguous signal per key, and
///     the clutch pulse/steering ramp used during actual driving would
///     otherwise be the first (or only) thing it catches.
///
/// F11/driving_hotkey, F12/chat_hotkey, and F10/keybind_hotkey switch
/// modes explicitly — there is no toggle key, by design (see README).
/// The mode hotkeys are recognized (and hidden from the game) in *all*
/// modes; every other binding is only live in Driving and Keybinding
/// Mode.
///
/// Not internally synchronized: key events arrive on the game's window
/// thread and tick() runs on the plugin's own thread, so the owner must
/// serialize every call (see dllmain.cpp).
class ModeManager {
public:
    ModeManager(VirtualController& controller, ClutchController& clutch, const Config& config);

    /// Feeds one key event. Returns true if the key should be hidden from
    /// the game: the mode hotkeys always are, and bound keys are in
    /// Driving and Keybinding Mode. Wire this up as KeyboardHook's
    /// callback.
    bool handleKeyEvent(const KeyboardHook::KeyEvent& event);

    /// Releases every held input (gears, clutch, throttle, steering, ...)
    /// without changing mode. Call when the game window loses focus: the
    /// key releases that would normally do this go to another window.
    void releaseHeldInputs();

    /// Advances the digital steering ramp/return integration by `dt`. A
    /// no-op outside Driving Mode. Call this at a fixed, fairly high rate
    /// (the plugin's tick thread uses 4 ms / 250 Hz) for smooth steering.
    void tick(std::chrono::steady_clock::duration dt);

    /// Applies a freshly reloaded configuration.
    void applyConfig(const Config& config);

    OperatingMode currentMode() const noexcept { return mode_; }

private:
    enum class Action {
        Gear1,
        Gear2,
        Gear3,
        Gear4,
        Gear5,
        Gear6,
        Reverse,
        Throttle,
        Brake,
        SteerLeft,
        SteerRight,
        Handbrake,
        Clutch,
        Reset,
    };

    /// Human-readable name for debug logging.
    static const char* actionName(Action action);

    void enterDrivingMode();
    void enterChatMode();
    void enterKeybindingMode();
    void dispatchAction(Action action, bool pressed);
    void dispatchGear(std::uint16_t gearButtonCode, bool pressed, bool immediate);
    /// Snaps steering directly to the current target (left stick +-max/0,
    /// or D-pad -1/0/1 if [steering].use_dpad) with no ramp. Used for
    /// [steering].instant, [steering].use_dpad, and, always, Keybinding
    /// Mode.
    void applySteering();
    void rebuildActionMap();
    /// Clears any state that could otherwise leak across a mode switch:
    /// cancels in-flight clutch/gear state and resets the virtual
    /// controller to neutral. Called on entry to every mode.
    void resetTransientState();

    VirtualController& controller_;
    ClutchController& clutch_;

    OperatingMode mode_ = OperatingMode::Chat;

    ModeConfig modeConfig_;
    BindingsConfig bindings_;
    SteeringConfig steeringConfig_;
    std::unordered_map<std::uint16_t, Action> actionByKey_;

    // Digital steering ramp state.
    double steeringValue_ = 0.0;
    bool steerLeftHeld_ = false;
    bool steerRightHeld_ = false;
};

} // namespace vcontroller
