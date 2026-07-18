#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace vwheel {

class VirtualController;

/// Drives the clutch axis and the clutch-assisted gear shift described in
/// the project README. Two independent things can each want the clutch
/// axis engaged, and they compose rather than fight each other:
///
///   - A manual clutch key (beginManualClutch()/endManualClutch()):
///     engages the axis for exactly as long as the key is held, with no
///     other side effects. Useful for driving with a real clutch-pedal
///     feel, and for binding the clutch axis in-game — a game's "press
///     any input" bind-detection needs the axis held for more than the
///     few milliseconds an automatic shift pulse lasts.
///
///   - The automatic per-gear-shift pulse (beginShift()/endShift()):
///     unlike a momentary "tap", the gear button tracks the physical key
///     — it goes down and stays down for as long as the key is held, and
///     only comes up on release. The clutch axis brackets just the
///     moment the button engages:
///
///       beginShift() [key down]
///         1. clutch axis wants engaged
///         2. wait press_delay_ms
///         3. gear button -> pressed          <- button now stays down
///         4. wait release_delay_ms
///         5. clutch axis no longer wants engaged (from this source)
///
///       endShift() [key up]
///         6. gear button -> released         <- whenever the key comes up
///
/// The clutch axis is physically written only on a transition of
/// "does anything want it engaged" (manual hold OR in-flight/held shift):
/// holding the manual clutch key keeps the axis engaged straight through
/// any number of gear shifts underneath it, exactly like holding a real
/// clutch pedal down through a quick multi-gear shift.
///
/// Steps 1-5 run on a dedicated worker thread so the delays never block
/// the main epoll loop. endShift() can land at any point in that
/// sequence (a very fast tap can release the key before the button was
/// even pressed) — see the .cpp file for how each case is unwound
/// without ever leaving the gear button or the clutch axis stuck.
///
/// Only one gear can be "active" (in flight or held) at a time. If
/// beginShift() is called for a different gear while one is still held,
/// the held gear is force-released immediately before the new one
/// starts — a safety net for overlapping key presses rather than the
/// primary expected flow.
class ClutchController {
public:
    struct Settings {
        bool enabled = true;
        std::uint16_t axis = 0; // ABS_* code
        std::int32_t pressValue = 32767;
        std::int32_t releaseValue = 0;
        std::chrono::milliseconds pressDelay{2};
        std::chrono::milliseconds releaseDelay{2};
    };

    /// `controller` must outlive this ClutchController.
    explicit ClutchController(VirtualController& controller, Settings initialSettings);
    ~ClutchController();

    ClutchController(const ClutchController&) = delete;
    ClutchController& operator=(const ClutchController&) = delete;

    /// Replaces the active settings (e.g. after a config reload). A shift
    /// already in flight finishes using the settings snapshot it started
    /// with; the manual clutch hold and any future one immediately use
    /// the new settings.
    void updateSettings(const Settings& settings);

    /// Call on the manual clutch key's press (down transition). Engages
    /// the clutch axis immediately if nothing else already has it
    /// engaged; otherwise just records that this source wants it held.
    void beginManualClutch();

    /// Call on the manual clutch key's release (up transition). Releases
    /// the clutch axis unless an in-flight/held gear shift still wants it
    /// engaged.
    void endManualClutch();

    /// Call on a gear key's press (down transition). `gearButtonCode` is
    /// one of VirtualController::kGearButtons or kReverseButton.
    /// Non-blocking: the clutch-engage sequence runs asynchronously on
    /// the worker thread, ending with the gear button held down.
    void beginShift(std::uint16_t gearButtonCode);

    /// Call on the same gear key's release (up transition). Releases the
    /// gear button (if it was pressed) and/or cancels an in-flight engage
    /// sequence cleanly, releasing this source's claim on the clutch axis
    /// (the axis itself only actually releases once nothing else,
    /// including a manual hold, still wants it engaged).
    void endShift(std::uint16_t gearButtonCode);

    /// Force-clears any held gear/clutch state and cancels any in-flight
    /// engage sequence, releasing the gear button and clutch axis if they
    /// were engaged. Call this when switching operating modes: Keybinding
    /// Mode bypasses beginShift()/endShift() entirely for its own gear
    /// dispatch, so without this a sequence started in Driving Mode could
    /// otherwise finish asynchronously after the mode switch and leave a
    /// button stuck, or fire again after Keybinding Mode already released
    /// it directly.
    void reset();

private:
    void workerLoop();

    /// Recomputes whether the clutch axis should be engaged from every
    /// source that can want it held (manualClutchHeld_ and
    /// autoClutchWantsEngaged_) and writes the axis only on an actual
    /// transition. Must be called with mutex_ held; `settings` is the
    /// snapshot to use for the axis code/values (callers on the worker
    /// thread pass their in-flight snapshot so a mid-shift config reload
    /// can't change which axis a single shift writes to; beginManualClutch/
    /// endManualClutch pass the live settings_).
    void syncClutchAxis(const Settings& settings);

    VirtualController& controller_;

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;

    bool stopping_ = false;

    // The gear currently in flight or held, 0 if none. Only one at a time
    // — see the class comment.
    std::uint16_t activeRequestGear_ = 0;
    // True once the worker has actually pressed activeRequestGear_'s
    // button (i.e. past step 3). While false, the request is still
    // mid-engage and endShift() must cancel rather than just release.
    bool activeRequestIsHeld_ = false;
    // True when the worker has a new request to start processing.
    bool pendingWork_ = false;
    // Bumped by every beginShift()/endShift() call; the worker compares
    // against its own snapshot to detect that its in-flight request was
    // superseded or cancelled while it was sleeping.
    std::uint64_t generation_ = 0;

    // Clutch axis engagement sources — see syncClutchAxis().
    bool manualClutchHeld_ = false;
    bool autoClutchWantsEngaged_ = false;
    bool axisEngaged_ = false; // last value actually written to the axis

    Settings settings_;
};

} // namespace vwheel
