#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>

namespace vcontroller {

/// Taps the game's own keyboard input and lets the plugin hide individual
/// keys from it.
///
/// Wreckfest reads gameplay keys through a thread-local WH_KEYBOARD hook
/// that it installs on its window thread at startup. KeyboardHook
/// redirects the game's SetWindowsHookExA/W imports so that when the game
/// installs that hook, our procedure is installed in its place and wraps
/// the game's: every key event reaches the plugin first, and the plugin
/// decides whether the game (and its window procedure) sees it at all.
///
/// This replaces the Linux version's exclusive EVIOCGRAB. Because the
/// hook is the game's own, it only sees keys while the game window has
/// focus, and only the specific keys the plugin claims are hidden,
/// rather than the whole keyboard.
namespace KeyboardHook {

struct KeyEvent {
    std::uint16_t code = 0; // Windows virtual-key code, left/right modifiers distinguished
    bool pressed = false;   // true = key down, false = key up
};

/// Invoked on the game's window thread for every key press (autorepeat
/// excluded) and release while the game window has focus. For a press,
/// the return value decides whether the game sees it: true hides it. A
/// release is hidden exactly when its press was, whatever the callback
/// returns, so the game never sees half of a keystroke.
using KeyEventCallback = std::function<bool(const KeyEvent&)>;

/// Patches `gameModule`'s SetWindowsHookEx imports. Returns false if the
/// module doesn't import either variant.
bool install(HMODULE gameModule, KeyEventCallback callback);

/// True once the game has actually installed its keyboard hook through
/// us. Used only for diagnostics.
bool isActive();

} // namespace KeyboardHook

} // namespace vcontroller
