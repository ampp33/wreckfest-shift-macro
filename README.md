# Wreckfest Virtual Wheel

A low-latency Linux daemon that turns a keyboard into a virtual Xbox 360
controller, with analog clutch emulation for fast manual-with-clutch
shifting. Built for Wreckfest under Steam/Proton, but nothing about it is
Wreckfest-specific except the default key bindings.

It reads your keyboard via `evdev`, grabs it exclusively while you're
driving so raw keystrokes never leak to the game, and emits a standard
Xbox 360 gamepad via `uinput` that Linux, SDL2, Steam, and Proton all
recognize natively.

## How it works

- **evdev** is the kernel's generic input event interface — every input
  device shows up as `/dev/input/eventN`.
- **libevdev** wraps evdev and provides `EVIOCGRAB`, the ioctl for
  exclusive device access — this is what lets Driving Mode intercept raw
  keystrokes instead of letting them fall through to the game/desktop.
- **uinput** lets a userspace process create a virtual device. This
  project creates a fake gamepad and reports it with the real Microsoft
  Xbox 360 Controller USB IDs (`045e:028e`), so the kernel, SDL2, and
  Steam Input all recognize it with no extra configuration.

`KeyboardReader` grabs your keyboard, `VirtualController` creates the fake
Xbox pad, and everything else turns one stream of events into the other.

```
src/
    main.cpp          epoll event loop wiring everything together
    input/            KeyboardReader — grabs keyboard, decodes key events
    controller/        VirtualController — drives the uinput Xbox pad
    timing/            ClutchController — async clutch/gear-shift timing
    modes/              ModeManager — Driving/Chat state machine, bindings
    config/             Config, KeyCodes — TOML config + KEY_*/ABS_* lookup
    logging/            Logger
```

Nothing does blocking I/O on the main thread except a single
`epoll_wait()` (keyboard fd, signalfd, and a 250 Hz timerfd for the
steering ramp). The only other thread is `ClutchController`'s worker, so a
multi-millisecond clutch sequence never blocks the event loop.

## Install & build

```sh
sudo apt install build-essential cmake libevdev-dev pkg-config evtest joystick

mkdir build && cd build
cmake .. && make -j"$(nproc)"
./virtual-wheel ../config/default.toml
```

TOML parsing uses a vendored single-header [toml++](https://github.com/marzer/tomlplusplus) — no extra package needed.

### Permissions

The daemon needs read/write on your keyboard's `/dev/input/eventN` and on
`/dev/uinput`. Either run it as root, or grant your user access:

```sh
sudo usermod -aG input "$USER"
echo 'KERNEL=="uinput", MODE="0660", GROUP="input"' | \
    sudo tee /etc/udev/rules.d/99-wreckfest-virtual-wheel.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

Log out and back in for the group change to apply.

## Configuration

Everything lives in one TOML file — see
[`config/default.toml`](config/default.toml) for the fully-commented
reference. Highlights:

- Keys use kernel `KEY_*` names (`"KEY_Q"`, `"KEY_F11"`); axes use
  `ABS_*` names (`"ABS_RY"`).
- `[keyboard].device` — leave `""` to auto-detect, or set explicitly if
  auto-detect picks the wrong device (check with `evtest` or
  `ls /dev/input/by-id/`).
- `[bindings]` maps keys to actions; omit any binding to leave it unbound.
- `[clutch]` and `[steering]` are documented inline in the default config.
- Reload without restarting: `kill -HUP $(pgrep virtual-wheel)`. Everything
  reloads except `[keyboard].device`, which needs a restart.

Gears map to fixed Xbox buttons (see `VirtualController.h`), since a
physical pad only has 11 buttons:

| Gear 1–6 | A · B · X · Y · LB · RB |
|---|---|
| Reverse | Left stick click |
| Handbrake | Right stick click |

Back/Start/Guide are left unbound so pause menus and the Steam overlay
still work.

## Operating modes

Three modes, switched by three dedicated hotkeys (not a toggle, so you
always know which mode a keypress lands in):

- **`driving_hotkey`** (default `F11`) → **Driving Mode**: grabs the
  keyboard, enables controller emulation with the full clutch-assist
  timing, turns on Scroll Lock as an indicator.
- **`chat_hotkey`** (default `F12`) → **Chat Mode**: releases the
  keyboard, centers the controller, normal typing/Alt+Tab/overlay work.
- **`keybind_hotkey`** (default `F10`) → **Keybinding Mode**: grabs the
  keyboard like Driving Mode, but every binding fires immediately and
  literally — gear keys press/release their button directly with no
  clutch pulse, steering snaps straight to full deflection instead of
  ramping. Turns on Num Lock as an indicator. Use this instead of Driving
  Mode while binding controls in Wreckfest's control settings: with the
  clutch's automatic axis pulse or the steering ramp in the way, a game's
  "press any input" bind-detection tends to catch the wrong signal —
  switch to Keybinding Mode, bind everything, then switch back to Driving
  Mode to actually drive.

The daemon **starts in Chat Mode**. **Ctrl+Alt+Esc** works from any mode
and immediately releases the keyboard, destroys the virtual controller,
and exits — the same clean-shutdown path as `SIGINT`/`SIGTERM`.

## The analog clutch

Holding a gear key runs a short automated clutch-in → shift → clutch-out
sequence, then holds the gear button down for as long as the key is held —
closer to a real manual-with-clutch bind than a raw passthrough:

```
key down → clutch engages → (press_delay_ms) → gear button presses
    → (release_delay_ms) → clutch releases → ... held ... → key up
    → gear button releases
```

All four values are configurable under `[clutch]`; set `enabled = false`
to skip the clutch axis entirely (gear keys still get the press/hold/
release timing). A tap faster than the two delays combined cancels
cleanly rather than pressing a gear nobody asked for.

`[bindings].clutch` (default `KEY_LEFTSHIFT`) is a separate, manually-held
clutch key — hold it to engage the axis directly, independent of any gear
shift. In Driving Mode the automatic per-shift pulse is usually too fast
for a game's bind-detection to catch on its own; use Keybinding Mode (see
[Operating modes](#operating-modes)) to bind everything cleanly instead.

## Testing the virtual controller

With the daemon running and Driving Mode active (`F11`):

```sh
evtest                  # look for "Virtual Xbox 360 Controller ..."
jstest /dev/input/js0   # device number may differ
sdl2-jstest --test /dev/input/js0
```

## Steam / Proton setup

1. **Steam → Library → Wreckfest → Properties → Controller** — disable
   Steam Input for this game (it otherwise fights the virtual pad).
2. Launch Wreckfest, switch the daemon to **Keybinding Mode** (`F10`), and
   bind gears/throttle/brake/steering/handbrake/clutch (`ABS_RY`) in its
   own control settings — Keybinding Mode sends one clean signal per key
   so bind-detection catches the right input (see
   [Operating modes](#operating-modes)). Switch back to Driving Mode
   (`F11`) before actually driving.

To auto-start/stop the daemon with the game, set **Launch Options** to:

```
/path/to/virtual-wheel /path/to/config.toml & VWHEEL_PID=$!; %command%; kill $VWHEEL_PID
```

Use absolute paths — Steam doesn't launch from your shell's working
directory. If it's not working, redirect logs to a file and `tail -f` it
while launching (`>> /tmp/virtual-wheel.log 2>&1`) — startup failures log
a clear `[ERROR]` line.

## Troubleshooting

- **"failed to open /dev/uinput: Permission denied"** — see
  [Permissions](#permissions).
- **Wrong device / nothing happens when driving** — set
  `[keyboard].device` explicitly after checking `evtest`.
- **Game doesn't see the controller** — confirm Steam Input is disabled,
  and check the device shows up in `evtest`/`sdl2-jstest` outside the game
  first.
- **Keyboard stuck grabbed** — press `Ctrl+Alt+Esc`. If the process has
  already died, the kernel releases the grab automatically when its file
  descriptor closes.

## Future expansion

Not currently implemented, but the architecture leaves room for:

- multiple game profiles / per-game configs
- mouse steering, analog input curves for steering/throttle/brake
- automatic clutch timing tuning
- input recording and replay
- a GUI configuration editor
- force feedback support
- real wheel/pedal hardware passthrough
- additional racing game presets

## License

MIT — see [LICENSE](LICENSE). The vendored `third_party/toml.hpp`
(toml++) is separately MIT licensed; see `third_party/README.md`.
