// What the GAME sees, as opposed to what the plugin publishes.
//
// The game doesn't get events: it calls XInputGetState() once per frame (or
// input-thread tick) and sees only the latest published state at that
// instant. A shift is "clutch, then gear" only if the poll that first
// notices the gear button already sees the clutch axis engaged.
//
// The plugin releases the clutch release_delay after pressing the gear
// button. If the game's poll period is longer than that window, then
// depending on where its poll happens to land relative to the shift, it
// can see "gear pressed, clutch already released" — i.e. the clutch
// appears not to have been used. That produces an intermittent failure
// whose probability is 1 - release_delay/poll_period.
//
// This test records real shift timelines from the plugin and replays them
// against simulated pollers at every phase offset.

#include <cmath>
#include <iostream>

#include "TestHarness.h"
#include "Helpers.h"

using namespace rig;

namespace {

struct PollStats {
    int phases = 0;
    int gearSeenWithoutClutch = 0; // first poll to see the gear button saw clutch released
    double unsafeFraction() const { return phases ? double(gearSeenWithoutClutch) / phases : 0; }
};

XINPUT_GAMEPAD stateAt(const std::vector<Snapshot>& snaps, const XINPUT_GAMEPAD& baseline,
                       double t) {
    XINPUT_GAMEPAD state = baseline;
    for (const auto& s : snaps) {
        if (s.ms > t) break;
        state = s.pad;
    }
    return state;
}

/// Replays `snaps` against a poller of the given period at every phase
/// offset (0.25 ms steps) and counts phases where the first poll that sees
/// a gear button down sees the clutch axis NOT engaged.
PollStats analyze(const std::vector<Snapshot>& snaps, const XINPUT_GAMEPAD& baseline,
                  double periodMs, const Config& cfg) {
    PollStats stats;
    const double end = snaps.empty() ? 0 : snaps.back().ms + periodMs;
    for (double phase = 0; phase < periodMs; phase += 0.25) {
        ++stats.phases;
        for (double t = phase; t < end; t += periodMs) {
            const XINPUT_GAMEPAD st = stateAt(snaps, baseline, t);
            if (isGearBit(st.wButtons)) {
                if (st.sThumbRY != cfg.clutch.pressValue) ++stats.gearSeenWithoutClutch;
                break;
            }
        }
    }
    return stats;
}

/// Records one real shift (gear1 held ~300 ms) with `cfg`'s timings.
struct Recording {
    std::vector<Snapshot> snaps;
    XINPUT_GAMEPAD baseline{};
};

Recording recordShift(const Config& cfg) {
    Rig rig(cfg);
    rig.driving();
    rig.resetRecording();
    rig.down("KEY_Q");
    rig.sleepMs(pressMs(cfg) + releaseMs(cfg) + 300);
    rig.up("KEY_Q");
    rig.settle();
    return {rig.snapshots(), rig.baseline()};
}

Config timed(const Config& base, int press, int release) {
    Config c = base;
    c.clutch.pressDelay = std::chrono::milliseconds(press);
    c.clutch.releaseDelay = std::chrono::milliseconds(release);
    return c;
}

} // namespace

// Sanity-check the model itself: a poller sampling far faster than the
// release window can never miss the clutch.
TEST(fast_poller_always_sees_clutch_with_gear) {
    const Config cfg = Rig::defaultConfig();
    const Recording rec = recordShift(cfg);
    const PollStats s = analyze(rec.snaps, rec.baseline, 1.0, cfg);
    CHECK_MSG(s.gearSeenWithoutClutch == 0,
              "1ms poller saw gear without clutch in " << s.gearSeenWithoutClutch << "/" << s.phases << " phases");
}

// The mechanism: unsafe fraction ~= max(0, 1 - release_delay / poll_period).
TEST(unsafe_poll_fraction_matches_release_delay_model) {
    const Config base = Rig::defaultConfig();
    const double period = 1000.0 / 60.0;
    for (const int release : {5, 10, 17, 25}) {
        const Config cfg = timed(base, 60, release);
        const Recording rec = recordShift(cfg);
        const PollStats s = analyze(rec.snaps, rec.baseline, period, cfg);
        const double predicted = std::max(0.0, 1.0 - release / period);
        std::cerr << "    release_delay=" << release << "ms @60Hz: game sees gear without clutch in "
                  << int(s.unsafeFraction() * 100 + 0.5) << "% of poll phases (model: "
                  << int(predicted * 100 + 0.5) << "%)\n";
        CHECK_MSG(std::fabs(s.unsafeFraction() - predicted) < 0.08,
                  "release_delay=" << release << ": measured " << s.unsafeFraction() << " vs model "
                                   << predicted);
    }
}

// The real question: with the shipped config, does the game always see the
// clutch together with the gear button?
TEST(default_config_is_safe_at_common_poll_rates) {
    const Config cfg = Rig::shippedConfig();
    if (cfg.clutch.frameTiming) {
        // Frame timing is poll-exact, so check what each poll sees. Measured
        // in game (2026-09-24): the clutch must be engaged in the frame the
        // gear first appears, and 1 frame of overlap was often missed, 2
        // very occasionally, 3 never.
        Rig rig(cfg);
        rig.driving();
        rig.down("KEY_Q");
        int firstGearPoll = -1;
        int overlap = 0;
        for (int i = 0; i < 20; ++i) {
            const XINPUT_GAMEPAD pad = rig.poll();
            const bool gear = (pad.wButtons & XINPUT_GAMEPAD_A) != 0;
            const bool clutch = pad.sThumbRY == cfg.clutch.pressValue;
            if (gear && firstGearPoll < 0) firstGearPoll = i;
            if (gear && clutch) ++overlap;
        }
        CHECK_MSG(firstGearPoll >= 0, "gear never appeared");
        CHECK_MSG(overlap >= 3, "game sees gear + clutch together for only " << overlap
                                    << " frame(s); 3 or more never missed in testing");
        return;
    }
    const Recording rec = recordShift(cfg);
    for (const double hz : {30.0, 60.0, 120.0, 144.0}) {
        const PollStats s = analyze(rec.snaps, rec.baseline, 1000.0 / hz, cfg);
        std::cerr << "    " << hz << "Hz poller: gear seen without clutch in "
                  << int(s.unsafeFraction() * 100 + 0.5) << "% of phases\n";
        CHECK_MSG(s.gearSeenWithoutClutch == 0,
                  "with press_delay=" << pressMs(cfg) << "ms release_delay=" << releaseMs(cfg)
                      << "ms, a " << hz << "Hz game sees the gear button with the clutch already "
                      << "released in " << int(s.unsafeFraction() * 100 + 0.5)
                      << "% of poll phases (needs release_delay >= " << 1000.0 / hz << "ms)");
    }
}

int main(int argc, char** argv) { return harness::runAll(argc, argv); }
