#pragma once
// Shared helpers for building test configs and expectations.

#include <string>
#include <vector>

#include "Rig.h"

namespace rig {

/// The default config with the clutch timings replaced. Tests that need to
/// land a key event inside a specific window (e.g. "after the gear pressed
/// but before the clutch releases") use wide windows so scheduling jitter
/// can't flip the outcome.
inline Config withTimings(int pressDelayMs, int releaseDelayMs) {
    Config c = Rig::defaultConfig();
    c.clutch.pressDelay = std::chrono::milliseconds(pressDelayMs);
    c.clutch.releaseDelay = std::chrono::milliseconds(releaseDelayMs);
    return c;
}

struct GearKey {
    std::uint16_t key;      // Windows VK code the config binds
    const char* button;     // event-name of the pad button it must drive
};

/// gear1..gear6 then reverse, as bound by `c`, with the pad button each
/// must produce (fixed by VirtualController::kGearButtons/kReverseButton).
inline std::vector<GearKey> gearKeys(const Config& c) {
    return {
        {c.bindings.gear1, "A"},  {c.bindings.gear2, "B"},  {c.bindings.gear3, "X"},
        {c.bindings.gear4, "Y"},  {c.bindings.gear5, "LB"}, {c.bindings.gear6, "RB"},
        {c.bindings.reverse, "LS"},
    };
}

inline std::string clutchOn(const Config& c) { return "RY=" + std::to_string(c.clutch.pressValue); }
inline std::string clutchOff(const Config& c) { return "RY=" + std::to_string(c.clutch.releaseValue); }

inline double pressMs(const Config& c) { return static_cast<double>(c.clutch.pressDelay.count()); }
inline double releaseMs(const Config& c) { return static_cast<double>(c.clutch.releaseDelay.count()); }

/// Scheduling slack allowed on top of a configured delay before a timing
/// assertion fails. Delays are never *shorter* than configured (that's
/// checked strictly); this only bounds how late they may run.
constexpr double kSlackMs = 30.0;

inline bool isGearBit(WORD b) {
    return (b & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y |
                 XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER |
                 XINPUT_GAMEPAD_LEFT_THUMB)) != 0;
}

} // namespace rig
