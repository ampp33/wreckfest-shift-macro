#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include "config/Config.h"
#include "input/KeyboardReader.h"

namespace vwheel {

class VirtualController;
class ClutchController;

enum class OperatingMode { Driving, Chat };

/// Central orchestrator tying keyboard input to controller output.
///
/// ModeManager owns the two explicit operating modes described in the
/// project spec:
///
///   - Chat Mode (the startup default): the keyboard is not grabbed, the
///     virtual controller is left centered/idle, and every key behaves
///     normally for the desktop, Steam overlay, and in-game chat.
///   - Driving Mode: the keyboard is grabbed exclusively, and configured
///     bindings are translated into virtual controller input.
///
/// F11/driving_hotkey and F12/chat_hotkey switch modes explicitly — there
/// is no toggle key, by design (see README). The Ctrl+Alt+Esc emergency
/// escape sequence and the two mode hotkeys are recognized in *both*
/// modes; every other binding is only live in Driving Mode.
class ModeManager {
public:
    using EmergencyCallback = std::function<void()>;

    ModeManager(KeyboardReader& keyboard, VirtualController& controller, ClutchController& clutch,
                const Config& config);

    /// Invoked when Ctrl+Alt+Esc is detected. The callback is expected to
    /// tear the whole daemon down (see main.cpp) — ModeManager itself only
    /// detects the combo, it doesn't own process lifetime.
    void setEmergencyCallback(EmergencyCallback callback);

    /// Feeds one decoded key event. Wire this up as
    /// KeyboardReader::setKeyEventCallback's target.
    void handleKeyEvent(const KeyboardReader::KeyEvent& event);

    /// Advances the digital steering ramp/return integration by `dt`. A
    /// no-op outside Driving Mode. Call this at a fixed, fairly high rate
    /// (the default main loop uses 4 ms / 250 Hz) for smooth steering.
    void tick(std::chrono::steady_clock::duration dt);

    /// Applies a freshly reloaded configuration (SIGHUP). A change to
    /// [keyboard].device is logged but not applied — switching which
    /// physical device is grabbed at runtime is not supported; restart
    /// the daemon instead.
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
    };

    void enterDrivingMode();
    void enterChatMode();
    void dispatchAction(Action action, bool pressed);
    void dispatchGear(std::uint16_t gearButtonCode, bool pressed);
    void rebuildActionMap();
    void notify(const std::string& title, const std::string& message);
    void setLed(bool drivingOn);

    KeyboardReader& keyboard_;
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

    // Modifier tracking for the Ctrl+Alt+Esc emergency escape combo.
    bool leftCtrlHeld_ = false;
    bool rightCtrlHeld_ = false;
    bool leftAltHeld_ = false;
    bool rightAltHeld_ = false;

    EmergencyCallback emergencyCallback_;
};

} // namespace vwheel
