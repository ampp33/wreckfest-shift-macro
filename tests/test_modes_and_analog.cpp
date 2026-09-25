// Mode gating (which keys are hidden from the game / ignored) and the
// non-gear controls: throttle, brake, handbrake, reset, steering.

#include "TestHarness.h"
#include "Helpers.h"

using namespace rig;

TEST(chat_mode_ignores_every_binding) {
    Rig rig; // starts in Chat Mode
    const Config& cfg = rig.config();
    rig.resetRecording();
    for (const auto& gear : gearKeys(cfg)) {
        CHECK(!rig.keyCode(gear.key, true));
        CHECK(!rig.keyCode(gear.key, false));
    }
    CHECK(!rig.down("KEY_UP"));
    CHECK(!rig.up("KEY_UP"));
    rig.settle();
    CHECK_EVENTS(rig, {});
}

TEST(mode_hotkeys_are_hidden_in_every_mode) {
    Rig rig;
    const Config& cfg = rig.config();
    for (int i = 0; i < 3; ++i) {
        CHECK(rig.keyCode(cfg.mode.drivingHotkey, true));
        CHECK(rig.keyCode(cfg.mode.drivingHotkey, false));
        CHECK(rig.keyCode(cfg.mode.chatHotkey, true));
        CHECK(rig.keyCode(cfg.mode.chatHotkey, false));
        CHECK(rig.keyCode(cfg.mode.keybindHotkey, true));
        CHECK(rig.keyCode(cfg.mode.keybindHotkey, false));
    }
}

TEST(driving_mode_hides_bound_keys_but_not_unbound_ones) {
    Rig rig;
    rig.driving();
    CHECK(rig.down("KEY_A"));
    CHECK(rig.up("KEY_A"));
    CHECK(!rig.down("KEY_ESC")); // pause menu must still reach the game
    CHECK(!rig.up("KEY_ESC"));
    rig.settle();
}

// Keybinding Mode: literal, immediate, no clutch — one clean signal per key.
TEST(keybinding_mode_fires_gear_buttons_immediately_without_clutch) {
    Rig rig;
    rig.keybinding();
    rig.resetRecording();
    CHECK(rig.down("KEY_A"));
    const auto ev = rig.events();
    CHECK_EVENTS(rig, {"B down"});
    if (!ev.empty()) CHECK_MSG(ev[0].ms < 5, "keybinding gear took " << ev[0].ms << "ms");
    rig.sleepMs(150);
    CHECK_EVENTS(rig, {"B down"}); // still no clutch pulse afterwards
    rig.up("KEY_A");
    CHECK_EVENTS(rig, {"B down", "B up"});
}

TEST(throttle_brake_handbrake_reset) {
    Rig rig;
    rig.driving();
    rig.resetRecording();
    rig.down("KEY_UP");
    rig.up("KEY_UP");
    rig.down("KEY_DOWN");
    rig.up("KEY_DOWN");
    rig.down("KEY_SPACE");
    rig.up("KEY_SPACE");
    rig.down("KEY_R");
    rig.up("KEY_R");
    CHECK_EVENTS(rig, {"RT=255", "RT=0", "LT=255", "LT=0", "RS down", "RS up",
                       "DPAD_UP down", "DPAD_UP up"});
}

// default.toml has steering.use_dpad = true: steering is D-pad left/right,
// and holding both cancels out (falling back to whichever key is still held).
TEST(dpad_steering_from_default_config) {
    Rig rig;
    CHECK(rig.config().steering.useDpad); // premise of this test
    rig.driving();
    rig.resetRecording();
    rig.down("KEY_LEFT");
    rig.down("KEY_RIGHT");
    rig.up("KEY_LEFT");
    rig.up("KEY_RIGHT");
    CHECK_EVENTS(rig, {"DPAD_LEFT down", "DPAD_LEFT up", "DPAD_RIGHT down", "DPAD_RIGHT up"});
}

TEST(stick_steering_instant) {
    Config c = Rig::defaultConfig();
    c.steering.useDpad = false;
    c.steering.instant = true;
    Rig rig(c);
    rig.driving();
    rig.resetRecording();
    rig.down("KEY_LEFT");
    rig.up("KEY_LEFT");
    rig.down("KEY_RIGHT");
    rig.up("KEY_RIGHT");
    CHECK_EVENTS(rig, {"LX=-32767", "LX=0", "LX=32767", "LX=0"});
}

TEST(stick_steering_ramps_at_configured_speed_and_returns) {
    Config c = Rig::defaultConfig();
    c.steering.useDpad = false;
    c.steering.instant = false;
    c.steering.rampSpeed = 5000;
    c.steering.returnSpeed = 2500;
    Rig rig(c);
    rig.driving();
    rig.down("KEY_RIGHT");
    CHECK_EQ(int(rig.pad().sThumbLX), 0); // the key alone moves nothing; ticks do

    for (int i = 0; i < 250; ++i) rig.tick(std::chrono::milliseconds(4)); // 1.0 s
    CHECK_EQ(int(rig.pad().sThumbLX), 5000);

    rig.up("KEY_RIGHT");
    for (int i = 0; i < 250; ++i) rig.tick(std::chrono::milliseconds(4)); // 1.0 s back
    CHECK_EQ(int(rig.pad().sThumbLX), 2500);

    rig.down("KEY_LEFT");
    for (int i = 0; i < 5000; ++i) rig.tick(std::chrono::milliseconds(4)); // long enough to clamp
    CHECK_EQ(int(rig.pad().sThumbLX), -32767);
}

TEST(keybinding_mode_steering_ignores_ramp) {
    Config c = Rig::defaultConfig();
    c.steering.useDpad = false;
    c.steering.instant = false;
    Rig rig(c);
    rig.keybinding();
    rig.resetRecording();
    rig.down("KEY_LEFT");
    rig.up("KEY_LEFT");
    CHECK_EVENTS(rig, {"LX=-32767", "LX=0"});
}

int main(int argc, char** argv) { return harness::runAll(argc, argv); }
