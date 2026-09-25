#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#include "logging/Logger.h"

namespace vcontroller {

/// `[controller]` — where the virtual controller shows up to the game.
struct ControllerConfig {
    /// XInput user index (0-3) the virtual controller occupies. A physical
    /// controller in the same slot is hidden from the game while the
    /// plugin is loaded; other slots pass through untouched.
    std::uint32_t slot = 0;
};

/// `[mode]` — Driving/Chat/Keybinding mode hotkeys.
struct ModeConfig {
    std::uint16_t drivingHotkey = 0;   // KEY_F11 by default, resolved from config
    std::uint16_t chatHotkey = 0;      // KEY_F12 by default
    std::uint16_t keybindHotkey = 0;   // KEY_F10 by default; 0 = feature unbound
};

/// `[bindings]` — keyboard key -> controller action mapping.
///
/// Every field is a Windows virtual-key code, resolved from a KEY_* name. A value of 0 means "unbound": the
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

    /// Reset/recovery key, mapped to D-pad Up (ABS_HAT0Y) rather than a
    /// BTN_* code: every standard button is already spoken for by a gear,
    /// reverse, or the handbrake, and Back/Start/Guide are deliberately
    /// left free for menus/overlay. The D-pad's vertical axis is
    /// otherwise unused regardless of [steering].use_dpad (which only
    /// ever drives the horizontal axis), so it's free for this.
    std::uint16_t reset = 0;
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
    /// `timing = "frames"`: count the two delays in game polls
    /// (pressDelayFrames/releaseDelayFrames) instead of milliseconds, so
    /// exactly which frames see the clutch and the gear is fixed.
    bool frameTiming = false;
    std::uint32_t pressDelayFrames = 2;
    std::uint32_t releaseDelayFrames = 2;
};

/// `[steering]` — how digital left/right key presses are turned into an
/// analog steering axis value.
struct SteeringConfig {
    std::string mode = "digital";
    std::int32_t maxValue = 32767;
    /// Axis units per second while a steering key is held down. Ignored
    /// when `instant` is true.
    double rampSpeed = 5000.0;
    /// Axis units per second while returning to center. Ignored when
    /// `instant` is true.
    double returnSpeed = 5000.0;
    /// If true, steer_left/steer_right snap the axis directly to
    /// +-maxValue/0 instead of ramping — immediate digital on/off rather
    /// than a smoothed analog feel. Ignored when `useDpad` is true (the
    /// D-pad has no ramp to skip — it's a 3-position digital hat).
    bool instant = false;
    /// If true, steer_left/steer_right drive the D-pad's horizontal axis
    /// (ABS_HAT0X, -1/0/1) instead of the left stick's X axis. Always
    /// immediate, regardless of `instant`/rampSpeed/returnSpeed. Many
    /// games apply analog-stick smoothing to the left stick regardless of
    /// how fast it's actually moved; routing through the D-pad — a
    /// genuinely digital control — sidesteps that.
    bool useDpad = false;
};

/// Top-level plugin configuration, parsed from a single TOML file.
///
/// A Config is an immutable snapshot: reloading (on file change) parses a fresh
/// Config and hands it to the components that care, rather than mutating
/// this one in place. That keeps every component free to decide for
/// itself how to apply the new settings (e.g. ClutchController copies the
/// fields it needs under its own mutex).
class Config {
public:
    /// Parses `path` into a Config. Throws std::runtime_error with a
    /// human-readable message on any parse or validation failure.
    static Config loadFromFile(const std::filesystem::path& path);

    ControllerConfig controller;
    ModeConfig mode;
    BindingsConfig bindings;
    ClutchConfig clutch;
    SteeringConfig steering;
    LogLevel logLevel = LogLevel::Info;
};

} // namespace vcontroller
