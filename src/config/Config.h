#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#include "logging/Logger.h"

namespace vwheel {

/// `[keyboard]` — which physical device to grab.
struct KeyboardConfig {
    /// Path to the evdev device node, e.g. "/dev/input/event4". If empty,
    /// KeyboardReader auto-detects the first device that looks like a
    /// keyboard (see KeyboardReader::autoDetectDevice).
    std::string device;
};

/// `[mode]` — Driving/Chat mode hotkeys and notification behavior.
struct ModeConfig {
    std::uint16_t drivingHotkey = 0; // KEY_F11 by default, resolved from config
    std::uint16_t chatHotkey = 0;    // KEY_F12 by default
    bool notifications = true;
};

/// `[bindings]` — keyboard key -> controller action mapping.
///
/// Every field is a KEY_* evdev code. A value of 0 means "unbound": the
/// action has no keyboard key assigned to it and will never fire.
struct BindingsConfig {
    std::uint16_t gear1 = 0;
    std::uint16_t gear2 = 0;
    std::uint16_t gear3 = 0;
    std::uint16_t gear4 = 0;
    std::uint16_t gear5 = 0;
    std::uint16_t gear6 = 0;
    std::uint16_t reverse = 0;

    std::uint16_t throttle = 0;
    std::uint16_t brake = 0;

    std::uint16_t steerLeft = 0;
    std::uint16_t steerRight = 0;

    std::uint16_t handbrake = 0;

    /// Manual clutch key: held down, it engages the clutch axis directly
    /// (independent of any gear shift) for as long as it's held, and
    /// composes with the automatic per-shift clutch pulse — see
    /// ClutchController. 0 means unbound.
    std::uint16_t clutch = 0;
};

/// `[clutch]` — analog clutch-assist automation applied to every gear
/// change (including reverse).
struct ClutchConfig {
    bool enabled = true;
    std::uint16_t axis = 0; // ABS_* code, e.g. ABS_RY
    std::int32_t pressValue = 32767;
    std::int32_t releaseValue = 0;
    std::chrono::milliseconds pressDelay{2};
    std::chrono::milliseconds releaseDelay{2};
};

/// `[steering]` — how digital left/right key presses are turned into an
/// analog steering axis value.
struct SteeringConfig {
    std::string mode = "digital";
    std::int32_t maxValue = 32767;
    /// Axis units per second while a steering key is held down.
    double rampSpeed = 5000.0;
    /// Axis units per second while returning to center.
    double returnSpeed = 5000.0;
};

/// Top-level daemon configuration, parsed from a single TOML file.
///
/// A Config is an immutable snapshot: reloading (SIGHUP) parses a fresh
/// Config and hands it to the components that care, rather than mutating
/// this one in place. That keeps every component free to decide for
/// itself how to apply the new settings (e.g. ClutchController copies the
/// fields it needs under its own mutex).
class Config {
public:
    /// Parses `path` into a Config. Throws std::runtime_error with a
    /// human-readable message on any parse or validation failure.
    static Config loadFromFile(const std::filesystem::path& path);

    KeyboardConfig keyboard;
    ModeConfig mode;
    BindingsConfig bindings;
    ClutchConfig clutch;
    SteeringConfig steering;
    LogLevel logLevel = LogLevel::Info;
};

} // namespace vwheel
