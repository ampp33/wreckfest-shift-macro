#pragma once
// Test rig: the plugin's real ModeManager + ClutchController +
// VirtualController wired together exactly as dllmain.cpp does, with a
// recorder on the virtual controller that timestamps every state the game
// could ever observe.

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "config/Config.h"
#include "config/KeyCodes.h"
#include "controller/VirtualController.h"
#include "modes/ModeManager.h"
#include "timing/ClutchController.h"

#ifndef WRECKFEST_DEFAULT_CONFIG
#error "WRECKFEST_DEFAULT_CONFIG must point at config/default.toml"
#endif

namespace rig {

using vcontroller::ClutchController;
using vcontroller::Config;
using vcontroller::ModeManager;
using vcontroller::VirtualController;
namespace KeyCodes = vcontroller::KeyCodes;

/// One published controller state and when it was published (ms since the
/// recorder was last reset).
struct Snapshot {
    double ms = 0;
    XINPUT_GAMEPAD pad{};
};

/// One observable change between consecutive published states.
struct Event {
    double ms = 0;
    std::string text; // e.g. "RY=32767", "A down", "DPAD_LEFT up"
};

inline std::string describeBit(WORD bit) {
    switch (bit) {
        case XINPUT_GAMEPAD_A: return "A";
        case XINPUT_GAMEPAD_B: return "B";
        case XINPUT_GAMEPAD_X: return "X";
        case XINPUT_GAMEPAD_Y: return "Y";
        case XINPUT_GAMEPAD_LEFT_SHOULDER: return "LB";
        case XINPUT_GAMEPAD_RIGHT_SHOULDER: return "RB";
        case XINPUT_GAMEPAD_LEFT_THUMB: return "LS";
        case XINPUT_GAMEPAD_RIGHT_THUMB: return "RS";
        case XINPUT_GAMEPAD_BACK: return "BACK";
        case XINPUT_GAMEPAD_START: return "START";
        case XINPUT_GAMEPAD_DPAD_UP: return "DPAD_UP";
        case XINPUT_GAMEPAD_DPAD_DOWN: return "DPAD_DOWN";
        case XINPUT_GAMEPAD_DPAD_LEFT: return "DPAD_LEFT";
        case XINPUT_GAMEPAD_DPAD_RIGHT: return "DPAD_RIGHT";
    }
    return "BIT" + std::to_string(bit);
}

inline std::vector<Event> diff(const XINPUT_GAMEPAD& before, const XINPUT_GAMEPAD& after,
                               double ms) {
    std::vector<Event> out;
    static constexpr WORD kBits[] = {
        XINPUT_GAMEPAD_A,          XINPUT_GAMEPAD_B,           XINPUT_GAMEPAD_X,
        XINPUT_GAMEPAD_Y,          XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB, XINPUT_GAMEPAD_BACK,
        XINPUT_GAMEPAD_START,      XINPUT_GAMEPAD_DPAD_UP,     XINPUT_GAMEPAD_DPAD_DOWN,
        XINPUT_GAMEPAD_DPAD_LEFT,  XINPUT_GAMEPAD_DPAD_RIGHT,
    };
    for (const WORD bit : kBits) {
        const bool was = (before.wButtons & bit) != 0;
        const bool is = (after.wButtons & bit) != 0;
        if (was != is) {
            out.push_back({ms, describeBit(bit) + (is ? " down" : " up")});
        }
    }
    const auto axis = [&](const char* name, int a, int b) {
        if (a != b) out.push_back({ms, std::string(name) + "=" + std::to_string(b)});
    };
    axis("LX", before.sThumbLX, after.sThumbLX);
    axis("LY", before.sThumbLY, after.sThumbLY);
    axis("RX", before.sThumbRX, after.sThumbRX);
    axis("RY", before.sThumbRY, after.sThumbRY);
    axis("LT", before.bLeftTrigger, after.bLeftTrigger);
    axis("RT", before.bRightTrigger, after.bRightTrigger);
    return out;
}

class Rig {
public:
    using Clock = std::chrono::steady_clock;

    static Config defaultConfig() {
        return Config::loadFromFile(WRECKFEST_DEFAULT_CONFIG);
    }

    explicit Rig(Config config = defaultConfig())
        : config_(std::move(config)),
          clutch_(controller_, clutchSettings(config_)),
          modes_(controller_, clutch_, config_) {
        controller_.setPublishObserver([this](const XINPUT_GAMEPAD& pad) {
            std::lock_guard<std::mutex> lock(recordMutex_);
            snapshots_.push_back({msSince(origin_), pad});
        });
    }

    ~Rig() { controller_.setPublishObserver(nullptr); }

    const Config& config() const { return config_; }
    ModeManager& modes() { return modes_; }

    // --- Driving the plugin as the game's keyboard hook would --------------

    /// Delivers a key event by KEY_* name; returns whether the plugin says
    /// to hide it from the game.
    bool key(const std::string& keyName, bool pressed) {
        const auto code = KeyCodes::lookupKey(keyName);
        if (!code) throw std::runtime_error("unknown key name " + keyName);
        return keyCode(*code, pressed);
    }
    bool down(const std::string& keyName) { return key(keyName, true); }
    bool up(const std::string& keyName) { return key(keyName, false); }

    bool keyCode(std::uint16_t code, bool pressed) {
        // dllmain.cpp serializes every ModeManager call with a mutex.
        std::lock_guard<std::mutex> lock(modeMutex_);
        return modes_.handleKeyEvent({code, pressed});
    }

    void tick(std::chrono::steady_clock::duration dt) {
        std::lock_guard<std::mutex> lock(modeMutex_);
        modes_.tick(dt);
    }

    void releaseHeldInputs() {
        std::lock_guard<std::mutex> lock(modeMutex_);
        modes_.releaseHeldInputs();
    }

    void driving() { keyCode(config_.mode.drivingHotkey, true); keyCode(config_.mode.drivingHotkey, false); }
    void chat() { keyCode(config_.mode.chatHotkey, true); keyCode(config_.mode.chatHotkey, false); }
    void keybinding() { keyCode(config_.mode.keybindHotkey, true); keyCode(config_.mode.keybindHotkey, false); }

    static void sleepMs(double ms) {
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(ms));
    }

    /// Long enough for any in-flight engage sequence to finish.
    void settle(double extraMs = 40) {
        sleepMs(static_cast<double>(config_.clutch.pressDelay.count() +
                                    config_.clutch.releaseDelay.count()) + extraMs);
    }

    // --- Reading back what the game could have seen -----------------------

    /// Forgets everything recorded so far; timestamps restart at 0 and
    /// "before" for the next event is the state currently published.
    void resetRecording() {
        std::lock_guard<std::mutex> lock(recordMutex_);
        baseline_ = snapshots_.empty() ? XINPUT_GAMEPAD{} : snapshots_.back().pad;
        snapshots_.clear();
        origin_ = Clock::now();
    }

    std::vector<Snapshot> snapshots() {
        std::lock_guard<std::mutex> lock(recordMutex_);
        return snapshots_;
    }

    XINPUT_GAMEPAD baseline() {
        std::lock_guard<std::mutex> lock(recordMutex_);
        return baseline_;
    }

    /// Every change between consecutive published states, in order.
    std::vector<Event> events() {
        std::lock_guard<std::mutex> lock(recordMutex_);
        std::vector<Event> out;
        XINPUT_GAMEPAD prev = baseline_;
        for (const auto& snap : snapshots_) {
            for (auto& e : diff(prev, snap.pad, snap.ms)) out.push_back(std::move(e));
            prev = snap.pad;
        }
        return out;
    }

    std::vector<std::string> eventTexts() {
        std::vector<std::string> out;
        for (const auto& e : events()) out.push_back(e.text);
        return out;
    }

    /// The currently published pad (what XInputGetState would return now).
    XINPUT_GAMEPAD pad() {
        XINPUT_STATE state{};
        controller_.readState(state);
        return state.Gamepad;
    }

    std::string timeline() {
        std::ostringstream os;
        for (const auto& e : events()) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%8.2fms  ", e.ms);
            os << "        " << buf << e.text << "\n";
        }
        return os.str();
    }

private:
    static double msSince(Clock::time_point t) {
        return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
    }

    static ClutchController::Settings clutchSettings(const Config& c) {
        return ClutchController::Settings{c.clutch.enabled,      c.clutch.axis,
                                          c.clutch.pressValue,   c.clutch.releaseValue,
                                          c.clutch.pressDelay,   c.clutch.releaseDelay};
    }

    Config config_;
    std::mutex recordMutex_;
    std::vector<Snapshot> snapshots_;
    XINPUT_GAMEPAD baseline_{};
    Clock::time_point origin_ = Clock::now();

    std::mutex modeMutex_;
    VirtualController controller_;
    ClutchController clutch_;
    ModeManager modes_;
};

// --- Assertions over recorded events ----------------------------------------

inline std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
    return "[" + s + "]";
}

} // namespace rig

/// Exact-sequence assertion: the recorded events must equal `expected`
/// element for element — nothing missing, nothing extra, in this order.
#define CHECK_EVENTS(rig_, ...)                                                           \
    do {                                                                                  \
        const std::vector<std::string> expected_ = __VA_ARGS__;                           \
        const auto actual_ = (rig_).eventTexts();                                         \
        CHECK_MSG(actual_ == expected_, "event sequence mismatch\n      expected: "        \
                                            << rig::join(expected_) << "\n      actual:   " \
                                            << rig::join(actual_) << "\n      timeline:\n"  \
                                            << (rig_).timeline());                        \
    } while (0)
