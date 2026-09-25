#include "timing/ClutchController.h"

#include <string>

#include "config/Config.h"
#include "controller/VirtualController.h"
#include "logging/Logger.h"

namespace vcontroller {

namespace {

std::string buttonName(std::uint16_t code) { return VirtualController::buttonName(code); }

} // namespace

ClutchController::Settings ClutchController::Settings::fromConfig(const ClutchConfig& config) {
    Settings settings;
    settings.enabled = config.enabled;
    settings.axis = config.axis;
    settings.pressValue = config.pressValue;
    settings.releaseValue = config.releaseValue;
    settings.pressDelay = config.pressDelay;
    settings.releaseDelay = config.releaseDelay;
    settings.frameTiming = config.frameTiming;
    settings.pressFrames = config.pressDelayFrames;
    settings.releaseFrames = config.releaseDelayFrames;
    return settings;
}

ClutchController::ClutchController(VirtualController& controller, Settings initialSettings)
    : controller_(controller), settings_(initialSettings) {
    worker_ = std::thread(&ClutchController::workerLoop, this);
}

ClutchController::~ClutchController() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }

    // Final safety net: if this is destroyed while a gear was
    // genuinely held by the user, the worker thread exits without
    // touching it (see workerLoop()). No concurrent writer remains once
    // the worker has joined, so it's safe to clean up directly here.
    bool needsSync = false;
    if (activeRequestIsHeld_) {
        controller_.setButton(activeRequestGear_, false);
        needsSync = true;
    }
    if (axisEngaged_) {
        controller_.setAxis(settings_.axis, settings_.releaseValue);
        needsSync = true;
    }
    if (needsSync) {
        controller_.syncReport();
    }
}

void ClutchController::updateSettings(const Settings& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = settings;
}

void ClutchController::syncClutchAxis(const Settings& settings) {
    const bool desired = manualClutchHeld_ || autoClutchWantsEngaged_;
    if (desired == axisEngaged_) {
        return;
    }
    axisEngaged_ = desired;
    Logger::instance().debug(std::string("clutch axis -> ") + (desired ? "PRESS" : "release") +
                              (settings.enabled ? "" : " (clutch disabled in config, not sent)"));
    if (settings.enabled) {
        controller_.setAxis(settings.axis, desired ? settings.pressValue : settings.releaseValue);
        controller_.syncReport();
    }
}

void ClutchController::beginManualClutch() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (manualClutchHeld_) {
        return; // already held (e.g. autorepeat) — nothing to do
    }
    manualClutchHeld_ = true;
    syncClutchAxis(settings_);
}

void ClutchController::endManualClutch() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!manualClutchHeld_) {
        return;
    }
    manualClutchHeld_ = false;
    syncClutchAxis(settings_);
}

void ClutchController::beginShift(std::uint16_t gearButtonCode) {
    std::lock_guard<std::mutex> lock(mutex_);
    Logger::instance().debug("beginShift(" + buttonName(gearButtonCode) + ")");

    if (gearButtonCode == activeRequestGear_ || gearButtonCode == pendingGear_) {
        Logger::instance().debug("  duplicate/autorepeat, ignored");
        return; // duplicate press (e.g. autorepeat) — already active/queued
    }

    if (activeRequestGear_ != 0) {
        // Something is already in flight or held — queue this one rather
        // than starting anything now. It'll begin its own engage sequence
        // once activeRequestGear_'s key is released, or get dropped
        // untouched if its own key releases first — see the class
        // comment.
        Logger::instance().debug("  queued behind " + buttonName(activeRequestGear_));
        pendingGear_ = gearButtonCode;
        return;
    }

    Logger::instance().debug("  nothing active — starting engage sequence");
    activeRequestGear_ = gearButtonCode;
    startEngage();
    cv_.notify_all();
}

void ClutchController::startEngage() {
    ++generation_;
    if (settings_.frameTiming) {
        frameSequenceActive_ = true;
        frameIndex_ = 0;
        frameSettings_ = settings_;
    } else {
        pendingWork_ = true;
    }
}

void ClutchController::endShift(std::uint16_t gearButtonCode) {
    std::lock_guard<std::mutex> lock(mutex_);
    Logger::instance().debug("endShift(" + buttonName(gearButtonCode) + ")");

    if (gearButtonCode == pendingGear_) {
        // Queued gear released before its turn ever came — just drop it.
        // The currently held/in-flight gear is untouched.
        Logger::instance().debug("  was queued, never started — dropped");
        pendingGear_ = 0;
        return;
    }

    if (activeRequestGear_ != gearButtonCode) {
        Logger::instance().debug("  stale release (not active or queued), ignored");
        return; // stale release: already superseded or never began
    }

    if (activeRequestIsHeld_) {
        // Plain, non-automated release — the gear was fully engaged, so
        // its key simply controls its button directly.
        Logger::instance().debug("  releasing held button");
        controller_.setButton(gearButtonCode, false);
        controller_.syncReport();
    } else {
        Logger::instance().debug("  cancelling in-flight engage (button was never pressed)");
    }
    // Whether the button had been pressed yet or not, we own clearing
    // this source's claim on the clutch axis here: the worker thread
    // checks `generation_` right after each of its waits and skips its
    // own axis update once it sees this request has been
    // cancelled/completed out from under it.
    autoClutchWantsEngaged_ = false;
    syncClutchAxis(settings_);

    activeRequestGear_ = 0;
    activeRequestIsHeld_ = false;
    pendingWork_ = false;
    frameSequenceActive_ = false;
    ++generation_;

    if (pendingGear_ != 0) {
        // A gear was queued behind this one — its turn starts now.
        Logger::instance().debug("  starting queued gear " + buttonName(pendingGear_));
        activeRequestGear_ = pendingGear_;
        pendingGear_ = 0;
        startEngage();
    }

    cv_.notify_all();
}

void ClutchController::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    bool needsSync = false;

    pendingGear_ = 0;
    if (activeRequestGear_ != 0) {
        if (activeRequestIsHeld_) {
            controller_.setButton(activeRequestGear_, false);
            needsSync = true;
        }
        activeRequestGear_ = 0;
        activeRequestIsHeld_ = false;
        pendingWork_ = false;
        frameSequenceActive_ = false;
        ++generation_; // cancels any in-flight worker sequence
        cv_.notify_all();
    }

    manualClutchHeld_ = false;
    autoClutchWantsEngaged_ = false;
    if (axisEngaged_) {
        axisEngaged_ = false;
        if (settings_.enabled) {
            controller_.setAxis(settings_.axis, settings_.releaseValue);
            needsSync = true;
        }
    }

    if (needsSync) {
        controller_.syncReport();
    }
}

void ClutchController::onGamePoll() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!frameSequenceActive_) {
        return;
    }
    const Settings& settings = frameSettings_;
    ++frameIndex_;
    const bool wantGear = frameIndex_ > settings.pressFrames;
    const bool wantClutch = frameIndex_ <= settings.pressFrames + settings.releaseFrames;

    if (frameIndex_ == 1) {
        Logger::instance().debug("poll: frame-timed engage for " + buttonName(activeRequestGear_) +
                                  ": " + std::to_string(settings.pressFrames) +
                                  " clutch-only frame(s), " +
                                  std::to_string(settings.releaseFrames) + " gear+clutch frame(s)");
    }

    // Stage everything this poll should see, then publish it once below
    // so the gear and clutch changes land in the same frame.
    if (wantGear && !activeRequestIsHeld_) {
        controller_.setButton(activeRequestGear_, true);
        activeRequestIsHeld_ = true;
    }
    autoClutchWantsEngaged_ = wantClutch;
    syncClutchAxis(settings);
    controller_.syncReport();

    if (!wantClutch) {
        frameSequenceActive_ = false;
        Logger::instance().debug("poll: " + buttonName(activeRequestGear_) +
                                  " frame-timed engage sequence complete");
    }
}

void ClutchController::workerLoop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (true) {
        cv_.wait(lock, [&] { return stopping_ || pendingWork_; });
        if (stopping_ && !pendingWork_) {
            return;
        }

        const std::uint16_t gear = activeRequestGear_;
        const std::uint64_t myGeneration = generation_;
        const Settings settings = settings_;
        pendingWork_ = false;

        // Everything below runs with `lock` held except during the two
        // wait_for() calls (which release it while blocked and reacquire
        // it before returning). That means beginManualClutch()/
        // endManualClutch()/beginShift()/endShift() can only ever observe
        // this iteration between steps, never mid-step.
        autoClutchWantsEngaged_ = true;
        syncClutchAxis(settings);
        Logger::instance().debug("worker: engaging clutch for " + buttonName(gear) + ", waiting " +
                                  std::to_string(settings.pressDelay.count()) + "ms before pressing");

        cv_.wait_for(lock, settings.pressDelay,
                     [&] { return generation_ != myGeneration || stopping_; });

        if (generation_ != myGeneration) {
            // Cancelled before the gear button was ever pressed — endShift()
            // already cleared this source's clutch claim itself in this
            // case. Nothing left to do for this request.
            Logger::instance().debug("worker: " + buttonName(gear) + " cancelled before press");
            continue;
        }

        // activeRequestGear_ is only ever set to a new value once nothing
        // else is in flight or held (see beginShift()/endShift()), so
        // there's never a previous gear to release here — this is always
        // a plain press into a genuinely empty slot.
        controller_.setButton(gear, true);
        controller_.syncReport();
        activeRequestIsHeld_ = true;
        Logger::instance().debug("worker: pressed " + buttonName(gear) + ", waiting " +
                                  std::to_string(settings.releaseDelay.count()) +
                                  "ms before releasing clutch");

        cv_.wait_for(lock, settings.releaseDelay,
                     [&] { return generation_ != myGeneration || stopping_; });

        if (generation_ == myGeneration) {
            autoClutchWantsEngaged_ = false;
            syncClutchAxis(settings);
            Logger::instance().debug("worker: " + buttonName(gear) + " engage sequence complete");
        } else {
            Logger::instance().debug("worker: " + buttonName(gear) +
                                      " clutch claim handled elsewhere (superseded before release)");
        }
        // If the generation changed, endShift() already handled this
        // source's clutch claim (and possibly started a queued gear's own
        // sequence in its place).
    }
}

} // namespace vcontroller
