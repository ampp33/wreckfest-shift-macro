#pragma once

#include <windows.h>
#include <xinput.h>

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace vcontroller {

/// Reports every gear shift as the game actually saw it, frame by frame.
///
/// The plugin publishes state whenever it likes, but the game only sees
/// what's published at the instant it calls XInputGetState(). This class
/// is fed every state the game really received (observe()), so each
/// report says how many of the game's frames saw the clutch before the
/// gear appeared, and how many saw the gear and the clutch together —
/// the latter is what decides whether Wreckfest counts the shift as
/// clutched (it needs the clutch in the gear's first frame, and ~3
/// frames of overlap never missed in testing).
///
/// observe() runs on the game's polling thread; takeReports() on the
/// plugin's tick thread, which does the actual logging.
class ShiftObserver {
public:
    using Clock = std::chrono::steady_clock;

    struct Settings {
        bool clutchEnabled = true;
        std::uint16_t clutchAxis = 0; // VirtualController axis code
        std::int32_t clutchReleaseValue = 0;
        /// Shown in every report so a log of mixed runs stays readable,
        /// e.g. "ms 20/20" or "frames 0/3".
        std::string timingLabel;
    };

    void updateSettings(const Settings& settings);

    /// Call with every state the game received from the virtual slot, in
    /// order, with when it was read.
    void observe(const XINPUT_GAMEPAD& seen, Clock::time_point when);

    /// Returns (and forgets) the finished shift reports, oldest first.
    std::vector<std::string> takeReports();

private:
    struct Shift {
        WORD gear = 0;
        std::uint32_t leadFrames = 0; // clutch-only frames just before the gear appeared
        double leadMs = 0;
        std::uint32_t overlapFrames = 0; // frames with gear + clutch, from the first gear frame
        Clock::time_point firstGearPoll;
    };

    /// Must be called with mutex_ held.
    void finish(const Shift& shift, double overlapMs);

    std::mutex mutex_;
    Settings settings_;

    WORD previousGears_ = 0;
    // Consecutive polls, up to and including the last one, that saw the
    // clutch engaged, and when the first of them happened.
    std::uint32_t clutchRun_ = 0;
    Clock::time_point clutchRunStart_;

    // The shift whose gear + clutch overlap is still running.
    std::optional<Shift> shift_;

    std::vector<std::string> reports_;
};

} // namespace vcontroller
