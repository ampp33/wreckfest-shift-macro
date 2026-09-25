# Test harness

Runs the plugin's real `ModeManager`, `ClutchController` and `VirtualController`
natively on Linux (Windows types come from `tests/shim/`, the Win32 logger is
replaced by an in-memory one) and records every controller state the game could
observe, with timestamps.

```sh
cmake -S . -B build/tests -DWRECKFEST_TESTS=ON
cmake --build build/tests -j
ctest --test-dir build/tests --output-on-failure   # or run build/tests/tests/<name> directly
```

Add `-DWRECKFEST_TSAN=ON` (separate build dir) for ThreadSanitizer.

| Executable | What it checks |
|---|---|
| `test_clutch_sequences` | Exact key -> event sequences and delays for gear shifts, queuing, manual clutch, mode switches |
| `test_modes_and_analog` | Which keys are hidden per mode; throttle/brake/handbrake/reset/steering |
| `test_fuzz_invariants` | Random key mashing; gear never pressed without clutch engaged >= press_delay |
| `test_game_polling` | Replays shifts against a simulated game polling XInput at 30/60/120/144 Hz |

Tests read `config/default.toml`, so they verify what you ship. `default_config_key_to_button_map`
pins the default key layout; update it when you deliberately rebind. A failing test prints the
plugin's own debug log. Fuzz failures print a seed: `WRECKFEST_FUZZ_SEED=<n> WRECKFEST_FUZZ_ITERATIONS=1`.
