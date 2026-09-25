#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace vcontroller {

class VirtualController;
struct ClutchConfig;

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
/// the game's input thread. endShift() can land at any point in that
/// sequence (a very fast tap can release the key before the button was
/// even pressed) — see the .cpp file for how each case is unwound
/// without ever leaving the gear button or the clutch axis stuck.
///
/// Only one gear can be in flight or held at a time. If beginShift() is
/// called for a different gear while one is already held, nothing starts
/// yet — the new key is just remembered as `pendingGear_`. The engage
/// sequence above only ever begins once the currently-held gear's key is
/// actually released:
///
///   - releasing the held gear's key releases its button immediately
///     (the plain, non-automated tail of step 6), then, if a gear was
///     queued in the meantime, immediately starts steps 1-5 for it;
///   - releasing a *queued* gear's key before its turn ever comes just
///     drops it — the held gear stays held, untouched, exactly as if the
///     extra press never happened.
///
/// This keeps the two clutch/gear updates instead of racing them
/// together: the currently-held gear is never touched until you actually
/// let go of it, so it's always either fully held or fully released, and
/// the clutch axis is never engaged while two gear buttons could
/// plausibly both be considered "active." A queued gear still goes
/// through the same press_delay_ms/fast-tap-cancel timing as any fresh
/// shift from neutral once its turn starts.
///
/// Frame timing (Settings::frameTiming): steps 1-5 are counted in game
/// polls instead of milliseconds, and run on the game's own polling
/// thread via onGamePoll() rather than on the worker. Each onGamePoll()
/// call sets the state that very poll returns, so with pressFrames = P
/// and releaseFrames = R the game sees exactly P polls of clutch only,
/// then R polls of gear + clutch, then gear only. R = 0 means the game
/// never sees the gear and the clutch together; P = 0 means they appear
/// in the same poll. Everything else (holding, queuing, cancelling on
/// key release) behaves the same as with millisecond timing.
class ClutchController {
public:
    struct Settings {
        bool enabled = true;
        std::uint16_t axis = 0; // ABS_* code
        std::int32_t pressValue = 32767;
        std::int32_t releaseValue = 0;
        std::chrono::milliseconds pressDelay{2};
        std::chrono::milliseconds releaseDelay{2};
        bool frameTiming = false;
        std::uint32_t pressFrames = 2;
        std::uint32_t releaseFrames = 2;

        static Settings fromConfig(const ClutchConfig& config);
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
    /// Non-blocking. If nothing is currently in flight or held, the
    /// clutch-engage sequence starts immediately on the worker thread. If
    /// a different gear is already in flight or held, this one is simply
    /// queued — see the class comment.
    void beginShift(std::uint16_t gearButtonCode);

    /// Call on the same gear key's release (up transition). Three cases:
    /// releasing the currently-held gear releases its button immediately
    /// and, if another gear was queued, starts its engage sequence;
    /// releasing a queued gear before its turn comes up just drops it;
    /// releasing a gear mid-engage (before its button was ever pressed)
    /// cancels the sequence cleanly. See the class comment.
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

    /// Call from the game's XInputGetState() for the virtual slot, just
    /// before the published state is read. Advances a frame-timed engage
    /// sequence by one poll (see the class comment); does nothing when no
    /// frame-timed sequence is in flight.
    void onGamePoll();

private:
    /// Starts the engage sequence for activeRequestGear_ — on the worker
    /// with millisecond timing, on the next onGamePoll() with frame
    /// timing. Must be called with mutex_ held.
    void startEngage();

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

    // The gear currently in flight (engage sequence running) or actually
    // held on the controller, 0 if neither. Only one at a time — see the
    // class comment.
    std::uint16_t activeRequestGear_ = 0;
    // True once the worker has actually pressed activeRequestGear_'s
    // button (i.e. past step 3). While false, it's still mid-engage and
    // endShift() must cancel rather than just release.
    bool activeRequestIsHeld_ = false;
    // A different gear key pressed while activeRequestGear_ was already
    // in flight or held, 0 if none. Starts its own engage sequence as
    // soon as activeRequestGear_'s key is released; dropped untouched if
    // its own key releases first.
    std::uint16_t pendingGear_ = 0;
    // True when the worker has a new request to start processing.
    bool pendingWork_ = false;
    // Bumped by every beginShift()/endShift() call; the worker compares
    // against its own snapshot to detect that its in-flight request was
    // superseded or cancelled while it was sleeping.
    std::uint64_t generation_ = 0;

    // Frame-timed engage sequence for activeRequestGear_, advanced by
    // onGamePoll(). frameSettings_ is the snapshot it started with.
    bool frameSequenceActive_ = false;
    std::uint32_t frameIndex_ = 0; // polls seen since the sequence started
    Settings frameSettings_;

    // Clutch axis engagement sources — see syncClutchAxis().
    bool manualClutchHeld_ = false;
    bool autoClutchWantsEngaged_ = false;
    bool axisEngaged_ = false; // last value actually written to the axis

    Settings settings_;
};

} // namespace vcontroller
