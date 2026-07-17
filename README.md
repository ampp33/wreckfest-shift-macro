# Wreckfest Virtual Wheel

A low-latency Linux daemon that turns a keyboard into a virtual Xbox 360
controller, with analog clutch emulation for fast manual-with-clutch
shifting. Built for Wreckfest under Steam/Proton on Wayland, but it's a
general-purpose keyboard-to-gamepad translator — nothing about it is
Wreckfest-specific except the default key bindings.

It reads your keyboard directly via `evdev`, grabs it exclusively while
you're driving so raw keystrokes never leak to the game, and emits a
standard Xbox 360 gamepad via `uinput` that Linux, SDL2, Steam, and Proton
all recognize natively.

## Contents

- [How this works: evdev, libevdev, uinput](#how-this-works-evdev-libevdev-uinput)
- [Architecture](#architecture)
- [Installation](#installation)
- [Build](#build)
- [Configuration](#configuration)
- [Finding your keyboard device](#finding-your-keyboard-device)
- [Operating modes](#operating-modes)
- [The analog clutch](#the-analog-clutch)
- [Testing the virtual controller](#testing-the-virtual-controller)
- [Steam / Proton setup](#steam--proton-setup)
- [Running with Steam](#running-with-steam)
- [Reloading configuration](#reloading-configuration)
- [Troubleshooting](#troubleshooting)
- [Future expansion](#future-expansion)

## How this works: evdev, libevdev, uinput

Three pieces of Linux input infrastructure make this possible:

- **evdev** ("event device") is the kernel's generic input event
  interface. Every input device — keyboards, mice, gamepads — shows up as
  a `/dev/input/eventN` node that streams `struct input_event` records
  (type/code/value triples like "key 16 (Q) went down").
- **libevdev** is a small wrapper library around evdev that handles the
  fiddly bits: device capability discovery, event parsing, and
  `EVIOCGRAB`, the ioctl that requests *exclusive* access to a device. A
  process holding the grab is the only one that receives the device's
  events — the X server, the Wayland compositor, and every other
  application (including the game) see nothing from a grabbed keyboard.
  This is what lets Driving Mode intercept raw key presses instead of
  letting them fall through to whatever window has focus.
- **uinput** is the mirror image: instead of *reading* from a device, it
  lets a userspace process *create* one. Writing to `/dev/uinput` with the
  right ioctls (`UI_DEV_SETUP`, `UI_SET_KEYBIT`, `UI_ABS_SETUP`,
  `UI_DEV_CREATE`, ...) makes the kernel materialize a new
  `/dev/input/eventN` node that behaves exactly like a real device would.
  This project uses it to create a virtual gamepad and reports it with
  the real Microsoft Xbox 360 Controller USB vendor/product IDs
  (`045e:028e`), so the kernel's `xpad` driver quirks, SDL2's
  `gamecontrollerdb.txt`, and Steam Input all recognize it without any
  extra configuration.

In short: **KeyboardReader** grabs your keyboard via libevdev/EVIOCGRAB,
**VirtualController** creates a fake Xbox pad via uinput, and everything
else in this project is the logic that turns one stream of events into
the other.

## Architecture

```
src/
    main.cpp              epoll event loop: wires everything together,
                           owns the signalfd/timerfd, top-level exception
                           handling and clean shutdown

    input/
        KeyboardReader     opens/grabs a keyboard via libevdev, decodes
                           key events, drives keyboard LEDs

    controller/
        VirtualController  creates and drives the uinput Xbox 360 pad

    timing/
        ClutchController   worker thread that runs the clutch-assisted
                           gear-shift timing sequence asynchronously

    modes/
        ModeManager        Driving/Chat mode state machine, key binding
                           dispatch, digital steering ramp, emergency
                           escape detection, desktop notifications

    config/
        Config             TOML parsing and validation
        KeyCodes           KEY_*/ABS_* name <-> numeric code lookup

    logging/
        Logger             thread-safe leveled console logger
```

Nothing here does blocking I/O on the main thread except a single
`epoll_wait()`. The keyboard fd, a signalfd (SIGINT/SIGTERM/SIGHUP), and a
timerfd (steering ramp ticks, 250 Hz) are all registered with one epoll
instance. The only other thread is `ClutchController`'s worker, which
exists solely so a multi-millisecond clutch sequence never blocks the
event loop or delays the next keystroke.

## Installation

```sh
sudo apt install \
    build-essential \
    cmake \
    libevdev-dev \
    pkg-config \
    evtest \
    joystick
```

- `build-essential`, `cmake`, `pkg-config` — the C++20 toolchain and build
  system.
- `libevdev-dev` — headers/pkg-config file for libevdev (the runtime
  library, `libevdev2`, is usually already installed).
- `evtest` — interactive tool for identifying and testing input devices
  (used below to find your keyboard).
- `joystick` — provides `jstest`, used to verify the virtual controller.

Desktop notifications (mode-switch feedback) use `notify-send` if it's
available (typically part of `libnotify-bin`); it's optional; the daemon
runs fine without it and simply skips notifications.

### Permissions

Two device nodes need to be accessible to whatever user runs the daemon:

- `/dev/input/eventN` (your keyboard) — for reading and grabbing
- `/dev/uinput` — for creating the virtual controller

The simplest option is running the daemon as root (`sudo ./virtual-wheel`).
For a normal desktop session you'll usually prefer granting your user
access instead:

```sh
# Add yourself to the group that owns /dev/input/event* (varies by distro,
# commonly "input"):
sudo usermod -aG input "$USER"

# Grant access to /dev/uinput via a udev rule:
echo 'KERNEL=="uinput", MODE="0660", GROUP="input"' | \
    sudo tee /etc/udev/rules.d/99-wreckfest-virtual-wheel.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Log out and back in (group membership is applied at login) before running
the daemon as your normal user.

## Build

```sh
mkdir build
cd build
cmake ..
make -j"$(nproc)"
```

This produces `build/virtual-wheel`. TOML parsing uses a vendored
single-header copy of [toml++](https://github.com/marzer/tomlplusplus)
(`third_party/toml.hpp`, MIT licensed) so there's no extra package to
install for it.

Run it against the sample config:

```sh
./virtual-wheel ../config/default.toml
```

(With no argument, it looks for `config/default.toml` relative to the
current directory, then `/etc/wreckfest-virtual-wheel/config.toml`, then
next to the executable.)

## Configuration

Configuration is a single TOML file — see
[`config/default.toml`](config/default.toml) for the fully-commented
reference. Key points:

- Key names are the Linux kernel's `KEY_*` constants
  (`linux/input-event-codes.h`), e.g. `"KEY_Q"`, `"KEY_F11"`,
  `"KEY_LEFT"`. Axis names are the `ABS_*` constants, e.g. `"ABS_RY"`.
- `[keyboard].device` can be left empty (`""`) to auto-detect the first
  device that looks like a keyboard, or set explicitly — see below.
- `[bindings]` maps keys to actions. Any binding can be left out of the
  file entirely to leave that action unbound.
- `[clutch]` and `[steering]` are documented inline in the default config
  and in `ClutchController.h`/`ModeManager.cpp` respectively.
- `[logging].level` is one of `"error"`, `"warning"`, `"info"`, `"debug"`.

### Gear-to-button mapping

Wreckfest lets you bind each gear to an arbitrary controller button, but a
physical Xbox pad only has 11 buttons, so this project fixes an internal
mapping (see `VirtualController.h`) rather than exposing it as a keyboard
binding:

| Action    | Xbox button |
|-----------|-------------|
| Gear 1    | A           |
| Gear 2    | B           |
| Gear 3    | X           |
| Gear 4    | Y           |
| Gear 5    | LB          |
| Gear 6    | RB          |
| Reverse   | Left stick click |
| Handbrake | Right stick click |

Back, Start, and Guide are deliberately left unbound so they stay free for
pause menus and the Steam overlay.

## Finding your keyboard device

If auto-detection (`device = ""`) picks the wrong device — common if you
have more than one keyboard-like input device, e.g. a keyboard with
built-in macro/media keys enumerating twice — find the right one
explicitly:

```sh
ls /dev/input/by-id/       # look for *-event-kbd
evtest                     # interactively lists devices and echoes events
```

Run `evtest`, pick your keyboard from the numbered list, and press a few
keys — it'll print the events live so you can confirm you picked the
right device. Note the `/dev/input/eventN` path and set it as
`[keyboard].device` in your config. `libinput list-devices` is another
good way to cross-check device names if you have it installed.

## Operating modes

There are exactly two modes, switched by two dedicated hotkeys — **not**
a single toggle key, so you always know which mode a keypress will put
you in regardless of what mode you were already in:

- **`driving_hotkey`** (default `F11`) → **Driving Mode**: grabs the
  keyboard exclusively, enables controller emulation and clutch
  automation, and shows a "Driving Mode" notification. Scroll Lock turns
  on as a hardware-level indicator (best-effort; not all keyboards
  support software LED control).
- **`chat_hotkey`** (default `F12`) → **Chat Mode**: releases the
  keyboard grab, centers/idles the virtual controller, and shows a "Chat
  Mode" notification. Normal typing, Steam overlay, and Alt+Tab all work
  as usual. Scroll Lock turns off.

The daemon **starts in Chat Mode** — it never grabs your keyboard until
you explicitly press the driving hotkey. Both hotkeys and the emergency
escape sequence below are always recognized, in either mode.

### Emergency escape

**Ctrl+Alt+Esc**, from either mode, immediately:

1. releases the keyboard grab,
2. destroys the virtual controller device,
3. restores normal keyboard behavior, and
4. exits the process.

This uses the same clean-shutdown path as `SIGINT`/`SIGTERM` — RAII
destructors on `KeyboardReader`/`VirtualController` guarantee the grab is
released and the uinput device is destroyed no matter which of the three
triggers it. (A hard crash like `SIGSEGV` bypasses C++ destructors
entirely, as it would for any process — there is no way to guarantee
cleanup after that class of failure, only after graceful shutdown paths.)

## The analog clutch

This is the headline feature: a gear key doesn't just forward a button
press straight through. Holding it down runs a short automated
clutch-in/shift sequence and then **holds the gear button down for as
long as you hold the key** — mirroring a manual-with-clutch binding in
Wreckfest much more convincingly than a raw passthrough would:

```
key down
        |
        v
clutch axis -> press_value
        |
        | wait press_delay_ms
        v
gear button -> pressed   ────────────────┐
        |                                │ stays pressed for as
        | wait release_delay_ms          │ long as the key is held
        v                                │
clutch axis -> release_value             │
        |                                │
        .   (key held here)              │
        |                                │
key up  ◄─────────────────────────────────┘
        |
        v
gear button -> released
```

All four clutch values (`press_value`, `release_value`, `press_delay_ms`,
`release_delay_ms`) are configurable under `[clutch]`. The engage sequence
(clutch press through clutch release) runs on a dedicated worker thread
(`ClutchController`) so it never blocks the input event loop, even with
long delays; the gear button's release is driven directly by the key-up
event, whenever it arrives.

A tap faster than `press_delay_ms + release_delay_ms` is handled cleanly:
releasing the key before the gear button was ever pressed cancels the
in-flight engage sequence and releases the clutch immediately, rather
than pressing a gear button nobody asked for anymore.

Pressing a different gear key while one is still held force-releases the
previously held gear button immediately (no clutch pulse) before starting
the new one — a safety net for overlapping presses, not the primary
expected flow of releasing one gear before selecting the next.

Set `[clutch].enabled = false` to disable the clutch axis entirely; gear
keys still go through the same press_delay_ms-then-hold-then-release
timing, just without touching the clutch axis.

### Manual clutch key

`[bindings].clutch` (default `KEY_LEFTSHIFT`) binds a dedicated clutch
key, independent of any gear shift. Holding it engages the clutch axis
directly for exactly as long as it's held; releasing it releases the
axis — unless a gear shift is also currently holding it engaged, in
which case the axis stays down until *both* sources let go. This is what
lets you drive with a real clutch-pedal feel (hold clutch, tap a gear,
release clutch yourself on your own timing) instead of only ever getting
the automatic press_delay_ms/release_delay_ms pulse.

It's also the easiest way to **bind the clutch axis inside Wreckfest**:
the automatic per-shift pulse only lasts `press_delay_ms + release_delay_ms`
(a few milliseconds by default), which is usually too fast for a game's
"press any input" bind-detection to catch. Instead, enter Driving Mode,
open the Clutch field in Wreckfest's control settings, and press and hold
the clutch key for a second — no need to touch your `[clutch]` timing
values just to get through the binding step.

## Testing the virtual controller

With the daemon running and Driving Mode active (press `F11`):

```sh
# Confirm it enumerated and see its capabilities:
evtest
# (look for "Virtual Xbox 360 Controller (Wreckfest Virtual Wheel)")

# Classic joystick-API test — press keys bound to gears/throttle/steering
# and watch the axes/buttons respond:
jstest /dev/input/js0   # device number may differ; check `ls /dev/input/js*`

# SDL2's own test tool, if you have SDL2 installed, gives the clearest
# picture of what games will actually see (SDL2's GameController API is
# what most modern titles, including Wreckfest, use):
sdl2-jstest --list
sdl2-jstest --test /dev/input/js0
```

## Steam / Proton setup

1. Open **Steam → Library → Wreckfest → Properties → Controller**.
2. Set **Controller Support** to disable Steam Input for this game (or
   set it to a mode that passes raw gamepad input through). Steam Input
   remaps controller input by default, which will fight with a
   from-scratch keyboard-to-axis mapping like this one.
3. Launch Wreckfest and open its own control settings. With the daemon in
   Driving Mode, it should detect "Xbox 360 Controller" as a connected
   device the same way it would a real one.
4. Bind gears, throttle/brake, steering, handbrake, and clutch (`ABS_RY`,
   i.e. the right stick's vertical axis) inside Wreckfest's control
   settings, matching the layout described above.

Proton passes uinput-created devices through to the game unmodified as
long as Steam Input isn't intercepting them first — step 2 is the part
people most often miss.

## Running with Steam

Steam's per-game **Launch Options** let you start the daemon automatically
whenever you launch Wreckfest, and stop it automatically when the game
closes — no need to start it by hand first.

Set this in **Steam → Library → Wreckfest → right-click → Properties →
General → Launch Options**:

```
/path/to/virtual-wheel /path/to/config.toml & VWHEEL_PID=$!; %command%; kill $VWHEEL_PID
```

Steam runs the launch options string through a shell with `%command%`
substituted for the game's real launch command (Proton included), so
shell syntax like `&` and `;` works as shown:

1. `/path/to/virtual-wheel /path/to/config.toml &` starts the daemon in
   the background.
2. `VWHEEL_PID=$!` captures its process ID.
3. `%command%` runs the game (through Proton, if applicable) and blocks
   until it exits.
4. `kill $VWHEEL_PID` sends `SIGTERM` once the game closes — the same
   clean shutdown path as `Ctrl+Alt+Esc` or a manual `kill`, releasing the
   keyboard grab and destroying the virtual controller.

Use **absolute paths** for both the binary and the config file — Steam
doesn't launch from your shell's working directory, so the relative
`config/default.toml` default won't resolve.

The daemon is a plain Linux process, not something that runs "inside"
Proton: it talks to `/dev/input` and `/dev/uinput` directly on the host,
the same as if you'd started it from a terminal yourself, while the game
runs sandboxed under Proton alongside it. It still needs the permissions
described in [Permissions](#permissions) — group membership isn't
something Steam grants for you.

### Debugging startup issues

Steam usually doesn't surface the daemon's console output anywhere
visible, so if it isn't working, redirect its logs to a file instead of
troubleshooting blind:

```
/path/to/virtual-wheel /path/to/config.toml >> /tmp/virtual-wheel.log 2>&1 & VWHEEL_PID=$!; %command%; kill $VWHEEL_PID
```

Then `tail -f /tmp/virtual-wheel.log` while launching the game from
Steam. Common failures (missing config, permission errors, no keyboard
detected) all log a clear `[ERROR]` line before the process exits — see
[Troubleshooting](#troubleshooting).

### Using a wrapper script

For anything more elaborate than the one-liner above — e.g. picking a
config file based on which game is launching, or waiting for the virtual
controller to enumerate before starting the game — put the logic in a
small script and point Launch Options at that instead:

```sh
#!/bin/sh
# ~/.local/bin/wreckfest-with-wheel.sh
/path/to/virtual-wheel /path/to/config.toml >> /tmp/virtual-wheel.log 2>&1 &
VWHEEL_PID=$!
"$@"
kill "$VWHEEL_PID"
```

```sh
chmod +x ~/.local/bin/wreckfest-with-wheel.sh
```

Launch Options becomes:

```
/home/you/.local/bin/wreckfest-with-wheel.sh %command%
```

`"$@"` forwards Steam's full launch command (Proton and all) through to
the script unchanged.

## Reloading configuration

Send `SIGHUP` to apply a config change without restarting:

```sh
kill -HUP "$(pgrep virtual-wheel)"
```

Everything reloads except `[keyboard].device` — switching which physical
device is grabbed requires a restart, and a reload with a changed device
path logs a warning and keeps using the original device.

## Troubleshooting

- **"failed to open /dev/uinput: Permission denied"** — see
  [Permissions](#permissions) above.
- **Auto-detection grabs the wrong device / nothing happens when
  driving** — set `[keyboard].device` explicitly after checking `evtest`.
- **Game doesn't see the controller** — confirm Steam Input is disabled
  for the game (see [Steam / Proton setup](#steam--proton-setup)), and
  confirm the device shows up in `evtest`/`sdl2-jstest` first, outside
  the game, to isolate whether the problem is the daemon or Steam.
- **Keyboard stays grabbed / stuck after a crash** — press
  `Ctrl+Alt+Esc`. If the process has already died, the kernel releases
  the grab automatically when the file descriptor closes (process exit
  always closes its fds), so a keyboard cannot stay grabbed by a dead
  process.

## Future expansion

The architecture leaves room for, but does not currently implement:

- multiple game profiles / per-game configurations
- mouse steering
- analog input curves for steering/throttle/brake
- automatic clutch timing tuning
- input recording and replay
- a GUI configuration editor
- force feedback support
- real wheel/pedal hardware passthrough
- additional racing game presets beyond the default Wreckfest bindings

## License

MIT — see [LICENSE](LICENSE). The vendored `third_party/toml.hpp`
(toml++) is separately MIT licensed; see `third_party/README.md`.
