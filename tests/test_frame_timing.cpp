// Frame-timed clutch sequences ([clutch] timing = "frames") and the
// ShiftObserver report of what the game saw.
//
// With frame timing the engage sequence advances only on game polls, so
// these tests drive it poll by poll with Rig::poll() and check exactly
// what each poll returns — no timing slack involved.

#include "TestHarness.h"
#include "Helpers.h"

#include "timing/ShiftObserver.h"

using namespace rig;
using vcontroller::ShiftObserver;

namespace {

Config withFrames(std::uint32_t pressFrames, std::uint32_t releaseFrames) {
    Config c = Rig::defaultConfig();
    c.clutch.frameTiming = true;
    c.clutch.pressDelayFrames = pressFrames;
    c.clutch.releaseDelayFrames = releaseFrames;
    return c;
}

/// What one poll saw, compactly: "-" (neither), "C" (clutch only),
/// "G" (gear1 only), "GC" (both).
std::string seen(const XINPUT_GAMEPAD& pad, const Config& cfg) {
    const bool gear = (pad.wButtons & XINPUT_GAMEPAD_A) != 0;
    const bool clutch = pad.sThumbRY == cfg.clutch.pressValue;
    if (gear && clutch) return "GC";
    if (gear) return "G";
    if (clutch) return "C";
    return "-";
}

/// Presses gear1 and returns what each of the next `polls` polls saw.
std::vector<std::string> shiftPolls(Rig& rig, int polls) {
    rig.driving();
    rig.down("KEY_Q");
    std::vector<std::string> out;
    for (int i = 0; i < polls; ++i) out.push_back(seen(rig.poll(), rig.config()));
    return out;
}

} // namespace

TEST(frames_2_2_gives_two_clutch_frames_then_two_overlap_frames) {
    Rig rig(withFrames(2, 2));
    const std::vector<std::string> expected = {"C", "C", "GC", "GC", "G", "G"};
    const auto actual = shiftPolls(rig, 6);
    CHECK_MSG(actual == expected, "polls saw " << join(actual) << ", expected " << join(expected));
}

TEST(frames_2_0_never_shows_gear_and_clutch_together) {
    Rig rig(withFrames(2, 0));
    const std::vector<std::string> expected = {"C", "C", "G", "G"};
    const auto actual = shiftPolls(rig, 4);
    CHECK_MSG(actual == expected, "polls saw " << join(actual) << ", expected " << join(expected));
}

TEST(frames_0_2_shows_gear_and_clutch_in_the_same_first_frame) {
    Rig rig(withFrames(0, 2));
    const std::vector<std::string> expected = {"GC", "GC", "G"};
    const auto actual = shiftPolls(rig, 3);
    CHECK_MSG(actual == expected, "polls saw " << join(actual) << ", expected " << join(expected));
}

TEST(frames_0_0_is_a_shift_with_no_clutch_at_all) {
    Rig rig(withFrames(0, 0));
    const Config& cfg = rig.config();
    rig.driving();
    rig.resetRecording();
    rig.down("KEY_Q");
    CHECK(seen(rig.poll(), cfg) == "G");
    CHECK(seen(rig.poll(), cfg) == "G");
    CHECK_EVENTS(rig, {"A down"});
}

TEST(frame_sequence_waits_for_the_game_to_poll) {
    Rig rig(withFrames(2, 2));
    rig.driving();
    rig.resetRecording();
    rig.down("KEY_Q");
    rig.sleepMs(50); // no polls: nothing should have happened yet
    CHECK_EVENTS(rig, {});
}

TEST(frame_sequence_key_release_before_gear_cancels_cleanly) {
    Rig rig(withFrames(3, 2));
    const Config& cfg = rig.config();
    rig.driving();
    rig.resetRecording();
    rig.down("KEY_Q");
    CHECK(seen(rig.poll(), cfg) == "C");
    rig.up("KEY_Q");
    for (int i = 0; i < 6; ++i) CHECK(seen(rig.poll(), cfg) == "-");
    CHECK_EVENTS(rig, {clutchOn(cfg), clutchOff(cfg)});
}

TEST(frame_sequence_queued_gear_runs_its_own_frames_after_release) {
    Rig rig(withFrames(1, 1));
    const Config& cfg = rig.config();
    rig.driving();
    rig.down("KEY_Q");
    for (int i = 0; i < 3; ++i) rig.poll(); // gear1 fully engaged and held
    rig.resetRecording();

    rig.down("KEY_A"); // queued behind gear1
    rig.poll();
    CHECK_EVENTS(rig, {});
    rig.up("KEY_Q"); // gear1 up, gear2's frame sequence starts
    rig.poll();      // clutch-only frame
    rig.poll();      // gear2 + clutch
    rig.poll();      // gear2 only
    CHECK_EVENTS(rig, {"A up", clutchOn(cfg), "B down", clutchOff(cfg)});
}

TEST(manual_clutch_hold_survives_a_frame_sequence) {
    Rig rig(withFrames(1, 1));
    const Config& cfg = rig.config();
    rig.driving();
    rig.down("KEY_LEFTSHIFT");
    rig.down("KEY_Q");
    for (int i = 0; i < 4; ++i) rig.poll();
    CHECK(seen(rig.poll(), cfg) == "GC"); // still clutched: the manual hold wins
    rig.up("KEY_LEFTSHIFT");
    CHECK(seen(rig.poll(), cfg) == "G");
}

// --- ShiftObserver ------------------------------------------------------------

namespace {

void observerFor(const Config& cfg, ShiftObserver& obs) {
    ShiftObserver::Settings s;
    s.clutchEnabled = cfg.clutch.enabled;
    s.clutchAxis = cfg.clutch.axis;
    s.clutchReleaseValue = cfg.clutch.releaseValue;
    s.timingLabel = "test";
    obs.updateSettings(s);
}

/// Runs gear1's shift through `rig` for 20 polls 7 ms apart, feeding
/// every polled state to `obs`, and returns its reports.
std::vector<std::string> observeShift(Rig& rig, ShiftObserver& obs) {
    observerFor(rig.config(), obs);
    rig.driving();
    rig.down("KEY_Q");
    const auto t0 = ShiftObserver::Clock::now();
    for (int i = 0; i < 20; ++i) {
        obs.observe(rig.poll(), t0 + std::chrono::milliseconds(7 * i));
    }
    return obs.takeReports();
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

} // namespace

TEST(observer_reports_clutch_lead_and_overlap_frames) {
    Rig rig(withFrames(2, 3));
    ShiftObserver obs;
    const auto reports = observeShift(rig, obs);
    CHECK_MSG(reports.size() == 1, reports.size() << " reports");
    if (reports.size() == 1) {
        const std::string& r = reports[0];
        CHECK_MSG(contains(r, "A(Gear1)"), r);
        CHECK_MSG(contains(r, "2 clutch-only frame(s) before the gear (14.0 ms)"), r);
        CHECK_MSG(contains(r, "3 gear+clutch frame(s) (21.0 ms)"), r);
        CHECK_MSG(!contains(r, "never saw them together"), r);
    }
}

TEST(observer_flags_a_shift_that_never_showed_gear_and_clutch_together) {
    Rig rig(withFrames(2, 0));
    ShiftObserver obs;
    const auto reports = observeShift(rig, obs);
    CHECK_MSG(reports.size() == 1, reports.size() << " reports");
    if (reports.size() == 1) {
        CHECK_MSG(contains(reports[0], "0 gear+clutch frame(s) (0.0 ms) — never saw them together"),
                  reports[0]);
    }
}

TEST(observer_reports_a_shift_without_clutch_immediately) {
    Rig rig(withFrames(0, 0));
    ShiftObserver obs;
    observerFor(rig.config(), obs);
    rig.driving();
    rig.down("KEY_Q");
    obs.observe(rig.poll(), ShiftObserver::Clock::now());
    const auto reports = obs.takeReports();
    CHECK_MSG(reports.size() == 1, reports.size() << " reports");
    if (reports.size() == 1) {
        CHECK_MSG(contains(reports[0], "0 clutch-only frame(s)"), reports[0]);
        CHECK_MSG(contains(reports[0], "never saw them together"), reports[0]);
    }
}

TEST(observer_reports_each_of_several_shifts) {
    Rig rig(withFrames(0, 3));
    ShiftObserver obs;
    observerFor(rig.config(), obs);
    rig.driving();
    const auto t0 = ShiftObserver::Clock::now();
    int poll = 0;
    const auto run = [&](int polls) {
        for (int i = 0; i < polls; ++i, ++poll) {
            obs.observe(rig.poll(), t0 + std::chrono::milliseconds(7 * poll));
        }
    };
    rig.down("KEY_Q");
    run(6);
    rig.down("KEY_A"); // queued; starts when Q releases
    rig.up("KEY_Q");
    run(6);
    const auto reports = obs.takeReports();
    CHECK_MSG(reports.size() == 2, reports.size() << " reports");
    if (reports.size() == 2) {
        CHECK_MSG(contains(reports[0], "A(Gear1)") && contains(reports[0], "3 gear+clutch frame(s) (21.0 ms)"),
                  reports[0]);
        CHECK_MSG(contains(reports[1], "B(Gear2)") && contains(reports[1], "3 gear+clutch frame(s) (21.0 ms)"),
                  reports[1]);
    }
}

int main(int argc, char** argv) { return harness::runAll(argc, argv); }
