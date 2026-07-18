#include "modes/ModeManager.h"

#include <linux/input-event-codes.h>
#include <unistd.h>
#include <sys/wait.h>

#include <algorithm>
#include <cmath>

#include "controller/VirtualController.h"
#include "logging/Logger.h"
#include "timing/ClutchController.h"

namespace vwheel {

ModeManager::ModeManager(KeyboardReader& keyboard, VirtualController& controller,
                          ClutchController& clutch, const Config& config)
    : keyboard_(keyboard), controller_(controller), clutch_(clutch) {
    applyConfig(config);
}

void ModeManager::setEmergencyCallback(EmergencyCallback callback) {
    emergencyCallback_ = std::move(callback);
}

void ModeManager::applyConfig(const Config& config) {
    if (!keyboard_.devicePath().empty() && config.keyboard.device != keyboard_.devicePath()) {
        Logger::instance().warning(
            "[keyboard].device changed in config but changing the grabbed device at runtime is "
            "not supported; restart the daemon to pick up the new device. Keeping " +
            keyboard_.devicePath());
    }

    modeConfig_ = config.mode;
    bindings_ = config.bindings;
    steeringConfig_ = config.steering;
    rebuildActionMap();

    clutch_.updateSettings(ClutchController::Settings{
        config.clutch.enabled,
        config.clutch.axis,
        config.clutch.pressValue,
        config.clutch.releaseValue,
        config.clutch.pressDelay,
        config.clutch.releaseDelay,
    });

    Logger::instance().info("Configuration applied");
}

void ModeManager::rebuildActionMap() {
    actionByKey_.clear();

    const auto bind = [this](std::uint16_t key, Action action) {
        if (key == 0) {
            return; // unbound
        }
        actionByKey_[key] = action;
    };

    bind(bindings_.gear1, Action::Gear1);
    bind(bindings_.gear2, Action::Gear2);
    bind(bindings_.gear3, Action::Gear3);
    bind(bindings_.gear4, Action::Gear4);
    bind(bindings_.gear5, Action::Gear5);
    bind(bindings_.gear6, Action::Gear6);
    bind(bindings_.reverse, Action::Reverse);
    bind(bindings_.throttle, Action::Throttle);
    bind(bindings_.brake, Action::Brake);
    bind(bindings_.steerLeft, Action::SteerLeft);
    bind(bindings_.steerRight, Action::SteerRight);
    bind(bindings_.handbrake, Action::Handbrake);
    bind(bindings_.clutch, Action::Clutch);
}

void ModeManager::handleKeyEvent(const KeyboardReader::KeyEvent& event) {
    switch (event.code) {
        case KEY_LEFTCTRL: leftCtrlHeld_ = event.pressed; break;
        case KEY_RIGHTCTRL: rightCtrlHeld_ = event.pressed; break;
        case KEY_LEFTALT: leftAltHeld_ = event.pressed; break;
        case KEY_RIGHTALT: rightAltHeld_ = event.pressed; break;
        default: break;
    }

    // Emergency escape: recognized in every mode, always wins.
    if (event.code == KEY_ESC && event.pressed && (leftCtrlHeld_ || rightCtrlHeld_) &&
        (leftAltHeld_ || rightAltHeld_)) {
        Logger::instance().warning("Emergency escape (Ctrl+Alt+Esc) triggered");
        if (emergencyCallback_) {
            emergencyCallback_();
        }
        return;
    }

    // Mode-switch hotkeys: also recognized in every mode.
    if (event.pressed && event.code == modeConfig_.drivingHotkey) {
        enterDrivingMode();
        return;
    }
    if (event.pressed && event.code == modeConfig_.chatHotkey) {
        enterChatMode();
        return;
    }
    if (event.pressed && modeConfig_.keybindHotkey != 0 && event.code == modeConfig_.keybindHotkey) {
        enterKeybindingMode();
        return;
    }

    if (mode_ != OperatingMode::Driving && mode_ != OperatingMode::Keybinding) {
        return; // Chat Mode: no other key is translated to controller input.
    }

    const auto it = actionByKey_.find(event.code);
    if (it != actionByKey_.end()) {
        dispatchAction(it->second, event.pressed);
    }
}

void ModeManager::dispatchAction(Action action, bool pressed) {
    // Keybinding Mode: every binding fires immediately and literally, with
    // no clutch-assist pulse/delay and no steering ramp — see the class
    // comment in ModeManager.h for why.
    const bool immediate = (mode_ == OperatingMode::Keybinding);

    switch (action) {
        case Action::Gear1:
            dispatchGear(VirtualController::kGearButtons[0], pressed, immediate);
            break;
        case Action::Gear2:
            dispatchGear(VirtualController::kGearButtons[1], pressed, immediate);
            break;
        case Action::Gear3:
            dispatchGear(VirtualController::kGearButtons[2], pressed, immediate);
            break;
        case Action::Gear4:
            dispatchGear(VirtualController::kGearButtons[3], pressed, immediate);
            break;
        case Action::Gear5:
            dispatchGear(VirtualController::kGearButtons[4], pressed, immediate);
            break;
        case Action::Gear6:
            dispatchGear(VirtualController::kGearButtons[5], pressed, immediate);
            break;
        case Action::Reverse:
            dispatchGear(VirtualController::kReverseButton, pressed, immediate);
            break;
        case Action::Throttle:
            controller_.setAxis(VirtualController::kRightTrigger,
                                 pressed ? VirtualController::kTriggerMax
                                         : VirtualController::kTriggerMin);
            controller_.syncReport();
            break;
        case Action::Brake:
            controller_.setAxis(VirtualController::kLeftTrigger,
                                 pressed ? VirtualController::kTriggerMax
                                         : VirtualController::kTriggerMin);
            controller_.syncReport();
            break;
        case Action::SteerLeft:
            steerLeftHeld_ = pressed;
            if (immediate || steeringConfig_.instant || steeringConfig_.useDpad) {
                applySteering();
            }
            break;
        case Action::SteerRight:
            steerRightHeld_ = pressed;
            if (immediate || steeringConfig_.instant || steeringConfig_.useDpad) {
                applySteering();
            }
            break;
        case Action::Handbrake:
            controller_.setButton(VirtualController::kHandbrakeButton, pressed);
            controller_.syncReport();
            break;
        case Action::Clutch:
            if (pressed) {
                clutch_.beginManualClutch();
            } else {
                clutch_.endManualClutch();
            }
            break;
    }
}

void ModeManager::dispatchGear(std::uint16_t gearButtonCode, bool pressed, bool immediate) {
    if (immediate) {
        // Keybinding Mode: a direct, literal press/release with no
        // clutch-assist automation to confuse a game's bind-detection.
        controller_.setButton(gearButtonCode, pressed);
        controller_.syncReport();
        return;
    }
    if (pressed) {
        clutch_.beginShift(gearButtonCode);
    } else {
        clutch_.endShift(gearButtonCode);
    }
}

void ModeManager::applySteering() {
    const double target = (steerRightHeld_ && !steerLeftHeld_)   ? 1.0
                           : (steerLeftHeld_ && !steerRightHeld_) ? -1.0
                                                                   : 0.0;

    if (steeringConfig_.useDpad) {
        controller_.setAxis(VirtualController::kDpadX, static_cast<std::int32_t>(target));
        controller_.syncReport();
        return;
    }

    steeringValue_ = target * static_cast<double>(steeringConfig_.maxValue);
    controller_.setAxis(VirtualController::kLeftStickX,
                         static_cast<std::int32_t>(std::lround(steeringValue_)));
    controller_.syncReport();
}

void ModeManager::tick(std::chrono::steady_clock::duration dt) {
    // Keybinding Mode, [steering].instant, and [steering].use_dpad all
    // drive steering directly from dispatchAction()/applySteering() on
    // every key event — the ramp integration below is only for the
    // smoothed left-stick feel of ordinary Driving Mode.
    if (mode_ != OperatingMode::Driving || steeringConfig_.mode != "digital" ||
        steeringConfig_.instant || steeringConfig_.useDpad) {
        return;
    }

    const double target = (steerRightHeld_ && !steerLeftHeld_)   ? 1.0
                           : (steerLeftHeld_ && !steerRightHeld_) ? -1.0
                                                                   : 0.0;
    const double targetValue = target * static_cast<double>(steeringConfig_.maxValue);
    const double speed = (target != 0.0) ? steeringConfig_.rampSpeed : steeringConfig_.returnSpeed;
    const double deltaSeconds = std::chrono::duration<double>(dt).count();
    const double step = speed * deltaSeconds;

    if (steeringValue_ < targetValue) {
        steeringValue_ = std::min(steeringValue_ + step, targetValue);
    } else if (steeringValue_ > targetValue) {
        steeringValue_ = std::max(steeringValue_ - step, targetValue);
    }

    controller_.setAxis(VirtualController::kLeftStickX,
                         static_cast<std::int32_t>(std::lround(steeringValue_)));
    controller_.syncReport();
}

void ModeManager::enterDrivingMode() {
    if (mode_ == OperatingMode::Driving) {
        return;
    }
    Logger::instance().info("Entering Driving Mode");
    keyboard_.grab();
    mode_ = OperatingMode::Driving;
    resetTransientState();
    setLed(mode_);
    if (modeConfig_.notifications) {
        notify("Wreckfest Virtual Wheel", "Driving Mode");
    }
}

void ModeManager::enterChatMode() {
    if (mode_ == OperatingMode::Chat) {
        return;
    }
    Logger::instance().info("Entering Chat Mode");
    keyboard_.release();
    mode_ = OperatingMode::Chat;
    resetTransientState();
    setLed(mode_);
    if (modeConfig_.notifications) {
        notify("Wreckfest Virtual Wheel", "Chat Mode");
    }
}

void ModeManager::enterKeybindingMode() {
    if (mode_ == OperatingMode::Keybinding) {
        return;
    }
    Logger::instance().info("Entering Keybinding Mode");
    keyboard_.grab();
    mode_ = OperatingMode::Keybinding;
    resetTransientState();
    setLed(mode_);
    if (modeConfig_.notifications) {
        notify("Wreckfest Virtual Wheel", "Keybinding Mode");
    }
}

void ModeManager::resetTransientState() {
    // Cancels any in-flight clutch/gear sequence and releases whatever it
    // had engaged, then zeroes the virtual controller outright — a clean
    // slate regardless of which mode (and which dispatch path) last wrote
    // to it. Necessary because Keybinding Mode writes gear buttons
    // directly, bypassing ClutchController's own bookkeeping.
    clutch_.reset();
    controller_.resetAllInputs();
    steerLeftHeld_ = false;
    steerRightHeld_ = false;
    steeringValue_ = 0.0;
}

void ModeManager::setLed(OperatingMode mode) {
    // Best-effort optional indicators: Scroll Lock = Driving Mode, Num
    // Lock = Keybinding Mode.
    keyboard_.setLed(LED_SCROLLL, mode == OperatingMode::Driving);
    keyboard_.setLed(LED_NUML, mode == OperatingMode::Keybinding);
}

void ModeManager::notify(const std::string& title, const std::string& message) {
    // Fork+exec rather than system()/popen() so we never pass user-derived
    // strings through a shell. The child is reaped automatically because
    // main() sets SIGCHLD to SIG_IGN at startup.
    const pid_t pid = fork();
    if (pid < 0) {
        Logger::instance().debug("notify-send fork() failed");
        return;
    }
    if (pid == 0) {
        execlp("notify-send", "notify-send", title.c_str(), message.c_str(),
               static_cast<char*>(nullptr));
        _exit(127); // notify-send not installed/available — silently give up
    }
}

} // namespace vwheel
