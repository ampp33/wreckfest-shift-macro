# Wreckfest Shift Macro

An [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)
plugin for Wreckfest that turns your keyboard into a virtual Xbox 360
controller, with analog clutch emulation for fast manual-with-clutch
shifting.

It runs inside the game process: there's no separate daemon to start or
stop, no device to pick, and no permissions to set up. Drop two files
into the game's `scripts/` folder and launch the game. It works the same
under Steam/Proton on Linux and on Windows.

## How it works

The plugin patches a few entries in the game executable's import table
when the ASI loader loads it, before any game code runs:

- **`XInputGetState` / `XInputGetCapabilities` / `XInputSetState`**: the
  game's queries for one XInput slot (default 0) are answered from an
  in-memory controller state instead of real hardware. To the game, it
  looks like a wired Xbox 360 pad is plugged in. Other slots pass through
  to the real XInput.
- **`SetWindowsHookExA/W`**: Wreckfest reads gameplay keys through a
  `WH_KEYBOARD` hook on its window thread. When the game installs that
  hook, the plugin installs its own procedure in its place, wrapping the
  game's. Every keystroke reaches the plugin first, and the plugin can
  hide it from the game entirely.

Only the game's own imports are touched, and no code bytes are rewritten.

```
src/
    dllmain.cpp        plugin entry point; wires everything, runs the 250 Hz tick thread
    hooks/             ImportHook (IAT patching), XInputHook, KeyboardHook
    controller/        VirtualController — the in-memory Xbox pad state
    timing/            ClutchController — async clutch/gear-shift timing
    modes/             ModeManager — Driving/Chat/Keybinding state machine, bindings
    config/            Config, KeyCodes — TOML config + KEY_*/ABS_* lookup
    logging/           Logger (writes to a file next to the plugin)
```

Key events are handled on the game's window thread as they arrive. A
plugin thread ticks at 250 Hz for the steering ramp, focus tracking, and
config reloads. `ClutchController`'s worker thread runs the
multi-millisecond clutch sequence so the game thread never blocks.

## Install

1. **Ultimate ASI Loader.** Download the x64 release and put it in the
   Wreckfest install folder (next to `Wreckfest_x64.exe`) as
   `version.dll`. Wreckfest imports `version.dll`, so it's loaded
   automatically. Under Proton, add this to Wreckfest's Steam
   **Launch Options** so Wine loads it instead of its built-in copy:

   ```
   WINEDLLOVERRIDES="version=n,b" %command%
   ```

2. **The plugin.** Build it (below) and copy the contents of `out/` into
   the Wreckfest install folder:

   ```
   Wreckfest/
       version.dll                          <- Ultimate ASI Loader
       scripts/wreckfest-shift-macro.asi
       scripts/wreckfest-shift-macro.toml   <- config
   ```

3. **Steam Input.** In Steam → Library → Wreckfest → Properties →
   Controller, disable Steam Input, so it doesn't add its own pad on top
   of the virtual one.

The plugin writes `scripts/wreckfest-shift-macro.log` on every launch. If
anything doesn't work, check there first. A config error disables the
plugin and logs a clear `[ERROR]` line, and the game runs normally.

This targets the 64-bit Wreckfest (`Wreckfest_x64.exe`). Wreckfest 2 reads
input differently (DirectInput/Raw Input, `xinput1_4`) and is not
supported.

## Build

### Docker build (recommended)

Cross-compiles with MinGW-w64 in a clean container, so you don't need a
toolchain on the host:

```sh
DOCKER_BUILDKIT=1 docker build --output out .
```

The result is `out/scripts/wreckfest-shift-macro.asi` plus a copy of
[`config/default.toml`](config/default.toml) named
`wreckfest-shift-macro.toml`. The `.asi` is fully statically linked and
depends only on system DLLs.

### Native cross-compile

```sh
sudo apt install cmake g++-mingw-w64-x86-64-posix

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
cmake --build build -j"$(nproc)"
```

You need the `-posix` MinGW variant: it provides `std::thread`/`std::mutex`.

TOML is parsed with a vendored single-header copy of
[toml++](https://github.com/marzer/tomlplusplus), so no extra package is
needed.

## Configuration

Everything lives in `scripts/wreckfest-shift-macro.toml`. See
[`config/default.toml`](config/default.toml) for the fully commented
reference. Highlights:

- Keys use the Linux `KEY_*` names (`"KEY_Q"`, `"KEY_F11"`) and axes use
  `ABS_*` names (`"ABS_RY"`), kept from the Linux daemon version of this
  project so existing configs still work. Unknown settings from that
  version (`[keyboard].device`, `notifications`) are ignored.
- `[controller].slot` sets the XInput slot (0–3) for the virtual pad.
- `[bindings]` maps keys to actions. Omit a binding to leave it unbound.
- `[clutch]` and `[steering]` are documented inline in the default config.
- **Live reload:** save the file while the game is running and it's
  re-read within a second.

Gears map to fixed Xbox buttons (see `VirtualController.h`), since a pad
only has so many:

| Gear 1–6 | A · B · X · Y · LB · RB |
|---|---|
| Reverse | Left stick click |
| Handbrake | Right stick click |
| Reset | D-pad up |

Back/Start are left unbound so menus still work.

## Operating modes

There are three modes, each with its own hotkey. The hotkeys aren't
toggles, so you always know which mode a keypress lands in, and they're
never passed on to the game.

- **`driving_hotkey`** (default `F11`) → **Driving Mode**: bound keys are
  hidden from the game and drive the virtual controller, with full
  clutch-assist timing. Unbound keys (Esc for the pause menu, etc.) still
  reach the game.
- **`chat_hotkey`** (default `F12`) → **Chat Mode**: every key goes to
  the game untouched, for in-game chat. The controller sits centered.
- **`keybind_hotkey`** (default `F10`) → **Keybinding Mode**: like
  Driving Mode, but every binding fires immediately and literally. Gear
  keys press and release their button directly with no clutch pulse, and
  steering snaps to full deflection. Use it while binding controls in
  Wreckfest's settings. Otherwise the clutch pulse or steering ramp tends
  to be what the game's "press any input" detection catches. Bind
  everything, then switch back to Driving Mode to drive.

The plugin **starts in Chat Mode**. It only ever sees keys while the game
window has focus. If the game loses focus (Alt+Tab), every held input is
released, so nothing is left stuck on.

## The analog clutch

Holding a gear key runs a short automated clutch-in → shift → clutch-out
sequence, then holds the gear button down for as long as the key is held.
That's closer to a real manual-with-clutch bind than a raw passthrough:

```
key down → clutch engages → (press_delay_ms) → gear button presses
    → (release_delay_ms) → clutch releases → ... held ... → key up
    → gear button releases
```

All four values are configurable under `[clutch]`. Set `enabled = false`
to skip the clutch axis entirely (gear keys still get the press/hold/
release timing). A tap faster than the two delays combined cancels
cleanly rather than pressing a gear nobody asked for.

`[bindings].clutch` (default `KEY_LEFTSHIFT`) is a separate clutch key
you hold manually. Holding it engages the axis directly, independent of
any gear shift.

## Wreckfest setup

1. Launch Wreckfest, switch to **Keybinding Mode** (`F10`), and in its
   controller settings bind gears, throttle, brake, steering, handbrake,
   clutch (right stick Y), and reset (D-pad up).
2. Switch to **Driving Mode** (`F11`) and drive.

## Troubleshooting

- **No `.log` file appears**: the ASI loader isn't loading. Check that
  `version.dll` is next to `Wreckfest_x64.exe` and that the
  `WINEDLLOVERRIDES` launch option is set (Proton).
- **Log says the game hasn't installed its keyboard hook**: hotkeys won't
  work. This would mean a game update changed how Wreckfest reads the
  keyboard; please open an issue with the log.
- **Game doesn't see the controller**: confirm Steam Input is disabled.
  Try another `[controller].slot` if a physical pad is in slot 0.
- **Bound key still does its keyboard action in-game**: only keys that
  reach the game through its keyboard hook can be hidden. Clear that
  key's keyboard binding in Wreckfest's settings.

## Future expansion

Not currently implemented, but the architecture leaves room for:

- an on-screen mode indicator (the Linux version used keyboard LEDs and
  desktop notifications, which aren't available inside the game)
- multiple game profiles / per-game configs
- mouse steering, analog input curves for steering/throttle/brake
- automatic clutch timing tuning
- force feedback support

## License

MIT — see [LICENSE](LICENSE). The vendored `third_party/toml.hpp`
(toml++) is separately MIT licensed; see `third_party/README.md`.
