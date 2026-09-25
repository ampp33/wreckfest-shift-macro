// Exact keypress -> controller-event sequences for the clutch-assisted
// gear shift, driven through the real ModeManager/ClutchController.

#include "TestHarness.h"
#include "Helpers.h"

using namespace rig;

// The documented shift, for every gear key:
//   key down -> clutch axis engages
//            -> (press_delay) gear button down
//            -> (release_delay) clutch axis releases
//   key up   -> gear button up
TEST(every_gear_key_runs_the_full_clutch_sequence) {
    for (const auto& gear : gearKeys(Rig::defaultConfig())) {
        Rig rig;
        const Config& cfg = rig.config();
        rig.driving();
        rig.resetRecording();

        rig.keyCode(gear.key, true);
        rig.sleepMs(pressMs(cfg) + releaseMs(cfg) + 80);
        const auto shift = rig.events();
        CHECK_EVENTS(rig, {clutchOn(cfg), std::string(gear.button) + " down", clutchOff(cfg)});
        if (shift.size() == 3) {
            CHECK_MSG(shift[0].ms < kSlackMs, "clutch engaged " << shift[0].ms << "ms after key down");
            const double gap1 = shift[1].ms - shift[0].ms;
            const double gap2 = shift[2].ms - shift[1].ms;
            CHECK_MSG(gap1 >= pressMs(cfg) && gap1 <= pressMs(cfg) + kSlackMs,
                      gear.button << ": clutch->gear gap " << gap1 << "ms, expected "
                                  << pressMs(cfg) << "ms");
            CHECK_MSG(gap2 >= releaseMs(cfg) && gap2 <= releaseMs(cfg) + kSlackMs,
                      gear.button << ": gear->clutch-release gap " << gap2 << "ms, expected "
                                  << releaseMs(cfg) << "ms");
        }

        // Gear stays held with no further events for as long as the key is down.
        rig.resetRecording();
        rig.sleepMs(150);
        CHECK_EVENTS(rig, {});

        rig.keyCode(gear.key, false);
        rig.sleepMs(20);
        CHECK_EVENTS(rig, {std::string(gear.button) + " up"});
    }
}

// Pins the shipped default.toml: which physical key ends up on which pad
// button. If you rebind keys in default.toml, update this table on purpose.
TEST(default_config_key_to_button_map) {
    struct Row { const char* key; const char* button; };
    const Row rows[] = {
        {"KEY_Q", "A"}, {"KEY_A", "B"}, {"KEY_W", "X"}, {"KEY_S", "Y"},
        {"KEY_E", "LB"}, {"KEY_D", "RB"}, {"KEY_Z", "LS"},
    };
    for (const auto& row : rows) {
        Rig rig;
        const Config& cfg = rig.config();
        rig.driving();
        rig.resetRecording();
        CHECK(rig.down(row.key)); // bound keys are hidden from the game
        rig.sleepMs(pressMs(cfg) + releaseMs(cfg) + 60);
        CHECK_EVENTS(rig, {clutchOn(cfg), std::string(row.button) + " down", clutchOff(cfg)});
        rig.up(row.key);
    }
}

// A tap shorter than press_delay is unwound before the gear button ever
// presses: the clutch blips and NO shift happens. (Characterization of the
// design in ClutchController.h — worth knowing if a quick tap "does nothing".)
TEST(tap_shorter_than_press_delay_never_shifts) {
    Rig rig(withTimings(100, 50));
    rig.driving();
    rig.resetRecording();
    rig.down("KEY_A");
    rig.sleepMs(40);
    rig.up("KEY_A");
    rig.settle(60);
    CHECK_EVENTS(rig, {clutchOn(rig.config()), clutchOff(rig.config())});
}

// Key released after the gear pressed but before the release_delay ended:
// gear releases first, then the clutch is let go immediately.
TEST(release_during_release_delay_drops_gear_then_clutch) {
    Rig rig(withTimings(100, 300));
    const Config& cfg = rig.config();
    rig.driving();
    rig.resetRecording();
    rig.down("KEY_A");
    rig.sleepMs(100 + 100); // gear is down, clutch still engaged
    rig.up("KEY_A");
    rig.sleepMs(500);
    CHECK_EVENTS(rig, {clutchOn(cfg), "B down", "B up", clutchOff(cfg)});
}

// A second gear key while one is held does nothing until the first is
// released; then the second runs its own full sequence.
TEST(second_gear_is_queued_until_first_released) {
    Rig rig;
    const Config& cfg = rig.config();
    rig.driving();
    rig.down("KEY_Q");
    rig.settle();
    rig.resetRecording();

    rig.down("KEY_A");
    rig.sleepMs(150);
    CHECK_EVENTS(rig, {}); // queued: no visible effect at all

    rig.up("KEY_Q");
    rig.sleepMs(pressMs(cfg) + releaseMs(cfg) + 80);
    const auto ev = rig.events();
    CHECK_EVENTS(rig, {"A up", clutchOn(cfg), "B down", clutchOff(cfg)});
    if (ev.size() == 4) {
        const double gap = ev[2].ms - ev[1].ms;
        CHECK_MSG(gap >= pressMs(cfg), "queued gear pressed only " << gap << "ms after clutch");
    }

    rig.resetRecording();
    rig.up("KEY_A");
    rig.sleepMs(20);
    CHECK_EVENTS(rig, {"B up"});
}

TEST(queued_gear_released_before_its_turn_is_dropped) {
    Rig rig;
    rig.driving();
    rig.down("KEY_Q");
    rig.settle();
    rig.resetRecording();
    rig.down("KEY_A");
    rig.up("KEY_A");
    rig.settle();
    CHECK_EVENTS(rig, {});
    CHECK((rig.pad().wButtons & XINPUT_GAMEPAD_A) != 0); // Q's gear still held
}

// Handing over to a queued gear while the first gear's clutch is still
// engaged. Characterization: the clutch axis is released and immediately
// re-pressed around the hand-over (two back-to-back publishes).
TEST(handover_while_clutch_still_engaged) {
    Rig rig(withTimings(100, 400));
    const Config& cfg = rig.config();
    rig.driving();
    rig.resetRecording();
    rig.down("KEY_Q");
    rig.sleepMs(100 + 100); // Q's gear down, clutch engaged
    rig.down("KEY_A");      // queued
    rig.up("KEY_Q");
    rig.sleepMs(100 + 400 + 80);
    CHECK_EVENTS(rig, {clutchOn(cfg), "A down", "A up", clutchOff(cfg), clutchOn(cfg),
                       "B down", clutchOff(cfg)});
}

// Manual clutch key: axis follows the key exactly, and composes with the
// automatic pulse (no release at the end of a shift while it's held).
TEST(manual_clutch_key_holds_axis_through_a_shift) {
    Rig rig;
    const Config& cfg = rig.config();
    rig.driving();
    rig.resetRecording();

    rig.keyCode(cfg.bindings.clutch, true);
    rig.down("KEY_A");
    rig.sleepMs(pressMs(cfg) + releaseMs(cfg) + 80);
    CHECK_EVENTS(rig, {clutchOn(cfg), "B down"}); // no clutch release after the shift

    rig.keyCode(cfg.bindings.clutch, false);
    rig.up("KEY_A");
    rig.sleepMs(20);
    CHECK_EVENTS(rig, {clutchOn(cfg), "B down", clutchOff(cfg), "B up"});
}

// With [clutch].enabled = false the axis is never touched, but the
// press_delay still applies before the gear button.
TEST(clutch_disabled_only_presses_gear_after_delay) {
    Config c = Rig::defaultConfig();
    c.clutch.enabled = false;
    Rig rig(c);
    rig.driving();
    rig.resetRecording();
    rig.down("KEY_A");
    rig.sleepMs(pressMs(c) + 60);
    const auto ev = rig.events();
    CHECK_EVENTS(rig, {"B down"});
    if (ev.size() == 1) {
        CHECK_MSG(ev[0].ms >= pressMs(c), "gear pressed after only " << ev[0].ms << "ms");
    }
}

// Switching mode mid-shift must cancel the sequence: nothing may fire late.
TEST(mode_switch_mid_shift_cancels_cleanly) {
    Rig rig(withTimings(100, 100));
    const Config& cfg = rig.config();
    rig.driving();
    rig.resetRecording();
    rig.down("KEY_A");
    rig.sleepMs(50);
    rig.chat();
    rig.sleepMs(400);
    CHECK_EVENTS(rig, {clutchOn(cfg), clutchOff(cfg)});
    CHECK_EQ(rig.pad().wButtons, 0);
}

TEST(leaving_driving_mode_releases_a_held_gear) {
    Rig rig;
    const Config& cfg = rig.config();
    rig.driving();
    rig.down("KEY_A");
    rig.settle();
    rig.resetRecording();
    rig.chat();
    CHECK_EVENTS(rig, {"B up"});
    CHECK(!rig.up("KEY_A")); // in Chat Mode the key goes to the game, not us
    rig.settle();
    CHECK_EVENTS(rig, {"B up"});
    (void)cfg;
}

TEST(focus_loss_releases_everything) {
    Rig rig;
    const Config& cfg = rig.config();
    rig.driving();
    rig.down("KEY_UP");
    rig.keyCode(cfg.bindings.clutch, true);
    rig.down("KEY_A");
    rig.settle();
    rig.releaseHeldInputs();
    rig.settle();
    const XINPUT_GAMEPAD pad = rig.pad();
    CHECK_EQ(pad.wButtons, 0);
    CHECK_EQ(int(pad.bRightTrigger), 0);
    CHECK_EQ(pad.sThumbRY, 0);
}

int main(int argc, char** argv) { return harness::runAll(argc, argv); }
