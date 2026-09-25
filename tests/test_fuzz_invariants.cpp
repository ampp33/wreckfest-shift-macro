// Randomised key mashing with real timing, checking the invariants that
// must hold no matter how keys interleave with the clutch worker thread:
//
//   I1  never more than one gear/reverse button down at once
//   I2  a gear button only ever goes down while the clutch axis is engaged
//       in that same published state
//   I3  the clutch axis has been engaged for at least press_delay before
//       any gear button goes down
//   I4  once every key is released and things settle, the pad is neutral
//       (no stuck button, no stuck clutch)
//
// Every failure prints its seed; replay one with
//   WRECKFEST_FUZZ_SEED=<seed> WRECKFEST_FUZZ_ITERATIONS=1 ./test_fuzz_invariants
// Iteration count defaults to 30 (WRECKFEST_FUZZ_ITERATIONS to change).

#include <cstdlib>
#include <random>
#include <set>

#include "TestHarness.h"
#include "Helpers.h"

using namespace rig;

namespace {

long envLong(const char* name, long fallback) {
    const char* v = std::getenv(name);
    return v ? std::atol(v) : fallback;
}

struct Timing { int press, release; };

} // namespace

TEST(random_key_mashing_never_violates_clutch_invariants) {
    const long iterations = envLong("WRECKFEST_FUZZ_ITERATIONS", 30);
    const long baseSeed = envLong("WRECKFEST_FUZZ_SEED", 12345);
    const Timing timings[] = {{60, 10}, {60, 10}, {20, 5}, {8, 3}, {40, 25}};

    for (long iter = 0; iter < iterations; ++iter) {
        const long seed = baseSeed + iter;
        std::mt19937 rng(static_cast<unsigned>(seed));
        const Timing t = timings[rng() % (sizeof timings / sizeof *timings)];
        Rig rig(withTimings(t.press, t.release));
        const Config& cfg = rig.config();
        rig.driving();
        rig.resetRecording();

        std::vector<std::uint16_t> keys;
        for (const auto& g : gearKeys(cfg)) keys.push_back(g.key);
        keys.push_back(cfg.bindings.clutch);

        std::set<std::uint16_t> held;
        std::string script;
        const int steps = 14;
        for (int s = 0; s < steps; ++s) {
            const std::uint16_t key = keys[rng() % keys.size()];
            const bool press = held.count(key) == 0; // no autorepeat: strictly alternate
            rig.keyCode(key, press);
            if (press) held.insert(key); else held.erase(key);
            script += (press ? "+" : "-") + KeyCodes::keyName(key) + " ";

            // Bias sleeps toward the interesting boundaries of the sequence.
            const double choices[] = {0, 0.5, t.press - 1.0, double(t.press), t.press + 0.5,
                                      double(t.press + t.release), t.press + t.release + 2.0,
                                      double(rng() % 40)};
            const double d = choices[rng() % (sizeof choices / sizeof *choices)];
            script += "(" + std::to_string(d) + "ms) ";
            Rig::sleepMs(d);
        }
        for (const auto key : std::vector<std::uint16_t>(held.begin(), held.end())) {
            rig.keyCode(key, false);
            script += "-" + KeyCodes::keyName(key) + " ";
        }
        rig.settle(80);

        const auto snaps = rig.snapshots();
        XINPUT_GAMEPAD prev = rig.baseline();
        double engagedSince = -1e9;
        bool engaged = false;
        std::string violation;
        for (const auto& snap : snaps) {
            const bool nowEngaged = snap.pad.sThumbRY == cfg.clutch.pressValue;
            if (nowEngaged && !engaged) engagedSince = snap.ms;
            engaged = nowEngaged;

            const WORD gearBits = snap.pad.wButtons & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B |
                XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y | XINPUT_GAMEPAD_LEFT_SHOULDER |
                XINPUT_GAMEPAD_RIGHT_SHOULDER | XINPUT_GAMEPAD_LEFT_THUMB);
            const WORD risen = gearBits & ~prev.wButtons;
            if (__builtin_popcount(gearBits) > 1) {
                violation = "I1: two gear buttons down at once at " + std::to_string(snap.ms) + "ms";
            } else if (risen && !nowEngaged) {
                violation = "I2: gear " + describeBit(risen) + " pressed with clutch NOT engaged at " +
                            std::to_string(snap.ms) + "ms";
            } else if (risen && snap.ms - engagedSince < t.press - 0.5) {
                violation = "I3: gear " + describeBit(risen) + " pressed only " +
                            std::to_string(snap.ms - engagedSince) + "ms after clutch (need " +
                            std::to_string(t.press) + "ms)";
            }
            if (!violation.empty()) break;
            prev = snap.pad;
        }
        if (violation.empty()) {
            const XINPUT_GAMEPAD end = rig.pad();
            if (end.wButtons != 0 || end.sThumbRY != 0) {
                violation = "I4: pad not neutral after all keys released (buttons=" +
                            std::to_string(end.wButtons) + ", RY=" + std::to_string(end.sThumbRY) + ")";
            }
        }
        CHECK_MSG(violation.empty(), "seed " << seed << " (press=" << t.press << "ms release="
                                             << t.release << "ms): " << violation
                                             << "\n      script: " << script
                                             << "\n      timeline:\n" << rig.timeline());
    }
}

int main(int argc, char** argv) { return harness::runAll(argc, argv); }
