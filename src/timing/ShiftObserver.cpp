#include "timing/ShiftObserver.h"

#include <cstdio>

#include "controller/VirtualController.h"

namespace vcontroller {

namespace {

double msBetween(ShiftObserver::Clock::time_point from, ShiftObserver::Clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

} // namespace

void ShiftObserver::updateSettings(const Settings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = settings;
}

void ShiftObserver::observe(const XINPUT_GAMEPAD& seen, Clock::time_point when) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool clutch =
        settings_.clutchEnabled &&
        VirtualController::axisValue(seen, settings_.clutchAxis) != settings_.clutchReleaseValue;
    const WORD gears = seen.wButtons & VirtualController::kAllGearButtons;
    const WORD newGears = gears & static_cast<WORD>(~previousGears_);

    // Extend the running shift's overlap, or report it once it ends (or a
    // new shift cuts it short). The overlap lasted until this poll.
    if (shift_) {
        if (clutch && (gears & shift_->gear) && newGears == 0) {
            ++shift_->overlapFrames;
        } else {
            finish(*shift_, msBetween(shift_->firstGearPoll, when));
            shift_.reset();
        }
    }

    if (newGears != 0) {
        Shift shift;
        shift.gear = newGears & static_cast<WORD>(-newGears); // lowest new bit
        shift.leadFrames = clutchRun_;
        shift.leadMs = clutchRun_ > 0 ? msBetween(clutchRunStart_, when) : 0;
        shift.overlapFrames = clutch ? 1 : 0;
        shift.firstGearPoll = when;
        if (clutch) {
            shift_ = shift;
        } else {
            finish(shift, 0);
        }
    }

    if (clutch) {
        if (clutchRun_ == 0) {
            clutchRunStart_ = when;
        }
        ++clutchRun_;
    } else {
        clutchRun_ = 0;
    }
    previousGears_ = gears;
}

std::vector<std::string> ShiftObserver::takeReports() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    out.swap(reports_);
    return out;
}

void ShiftObserver::finish(const Shift& shift, double overlapMs) {
    char line[224];
    std::snprintf(line, sizeof(line),
                  "Game saw shift to %s [%s]: %u clutch-only frame(s) before the gear (%.1f ms), "
                  "%u gear+clutch frame(s) (%.1f ms)%s",
                  VirtualController::buttonName(shift.gear).c_str(),
                  settings_.timingLabel.c_str(), shift.leadFrames, shift.leadMs,
                  shift.overlapFrames, overlapMs,
                  shift.overlapFrames == 0 ? " — never saw them together" : "");
    reports_.emplace_back(line);
}

} // namespace vcontroller
