#include "timing/ClutchController.h"

#include "controller/VirtualController.h"
#include "logging/Logger.h"

namespace vwheel {

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

    // Final safety net: if the daemon shuts down while a gear was
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

    if (activeRequestGear_ == gearButtonCode) {
        return; // duplicate press (e.g. autorepeat) — already active
    }

    if (activeRequestGear_ != 0 && activeRequestIsHeld_) {
        // A different gear is still physically held; this shouldn't
        // happen in normal single-key-at-a-time driving, but don't leave
        // it stuck if it does.
        controller_.setButton(activeRequestGear_, false);
        controller_.syncReport();
    }

    activeRequestGear_ = gearButtonCode;
    activeRequestIsHeld_ = false;
    pendingWork_ = true;
    ++generation_;
    cv_.notify_all();
}

void ClutchController::endShift(std::uint16_t gearButtonCode) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (activeRequestGear_ != gearButtonCode) {
        return; // stale release: already superseded or never began
    }

    if (activeRequestIsHeld_) {
        controller_.setButton(gearButtonCode, false);
        controller_.syncReport();
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
    ++generation_;
    cv_.notify_all();
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

        cv_.wait_for(lock, settings.pressDelay,
                     [&] { return generation_ != myGeneration || stopping_; });

        if (generation_ != myGeneration) {
            // Cancelled before the gear button was ever pressed — endShift()
            // already cleared this source's clutch claim itself in this
            // case. Nothing left to do for this request.
            continue;
        }

        controller_.setButton(gear, true);
        controller_.syncReport();
        activeRequestIsHeld_ = true;

        cv_.wait_for(lock, settings.releaseDelay,
                     [&] { return generation_ != myGeneration || stopping_; });

        if (generation_ == myGeneration) {
            autoClutchWantsEngaged_ = false;
            syncClutchAxis(settings);
        }
        // If the generation changed, endShift() (key released quickly) or
        // a new beginShift() (different gear force-started) already
        // handled this source's clutch claim.
    }
}

} // namespace vwheel
