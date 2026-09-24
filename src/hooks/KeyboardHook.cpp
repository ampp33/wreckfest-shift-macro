#include "hooks/KeyboardHook.h"

#include <atomic>
#include <bitset>
#include <string>

#include "hooks/ImportHook.h"
#include "logging/Logger.h"

namespace vcontroller::KeyboardHook {

namespace {

using SetHookAFn = HHOOK(WINAPI*)(int, HOOKPROC, HINSTANCE, DWORD);
using SetHookWFn = HHOOK(WINAPI*)(int, HOOKPROC, HINSTANCE, DWORD);

SetHookAFn gRealSetHookA = nullptr;
SetHookWFn gRealSetHookW = nullptr;

KeyEventCallback gCallback;
std::atomic<HOOKPROC> gGameProc{nullptr};

// Keys whose most recent press was hidden from the game. Only ever
// touched on the game's window thread (the hook runs there), so no lock.
std::bitset<256> gHidden;

/// WH_KEYBOARD reports generic VK_SHIFT/VK_CONTROL/VK_MENU; split them
/// into left/right so bindings like KEY_LEFTSHIFT mean exactly that.
std::uint16_t normalizeKey(WPARAM wParam, LPARAM lParam) {
    const auto vk = static_cast<std::uint16_t>(wParam & 0xFF);
    const auto scanCode = static_cast<unsigned>((lParam >> 16) & 0xFF);
    const bool extended = (lParam & (1 << 24)) != 0;
    switch (vk) {
        case VK_SHIFT: return scanCode == 0x36 ? VK_RSHIFT : VK_LSHIFT;
        case VK_CONTROL: return extended ? VK_RCONTROL : VK_LCONTROL;
        case VK_MENU: return extended ? VK_RMENU : VK_LMENU;
        default: return vk;
    }
}

LRESULT callGame(int nCode, WPARAM wParam, LPARAM lParam) {
    const HOOKPROC gameProc = gGameProc.load();
    return gameProc ? gameProc(nCode, wParam, lParam) : CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK keyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0) {
        return callGame(nCode, wParam, lParam);
    }

    const std::uint16_t code = normalizeKey(wParam, lParam);
    const bool released = (lParam & (1u << 31)) != 0;
    const bool wasDown = (lParam & (1 << 30)) != 0;

    bool hide = gHidden[code];
    // HC_NOREMOVE is a peek: the same message comes back as HC_ACTION
    // when it's actually removed from the queue, so only act on that one.
    if (nCode == HC_ACTION) {
        if (released) {
            gCallback(KeyEvent{code, false});
            gHidden[code] = false;
        } else if (!wasDown) {
            hide = gCallback(KeyEvent{code, true});
            gHidden[code] = hide;
        }
        // else: autorepeat — hidden iff the original press was.
    }

    // Returning nonzero without calling the game's procedure discards the
    // message: neither the game's hook nor its window procedure see it.
    return hide ? 1 : callGame(nCode, wParam, lParam);
}

/// If this is the game installing its keyboard hook, remembers the
/// game's procedure so the caller can install keyboardProc in its place.
bool captureGameProc(int idHook, HOOKPROC proc, const char* variant) {
    if (idHook != WH_KEYBOARD) {
        return false;
    }
    gGameProc.store(proc);
    Logger::instance().info(std::string("Game installed its keyboard hook (SetWindowsHookEx") +
                            variant + "); key interception active");
    return true;
}

HHOOK WINAPI hookedSetHookA(int idHook, HOOKPROC proc, HINSTANCE module, DWORD threadId) {
    if (captureGameProc(idHook, proc, "A")) {
        return gRealSetHookA(idHook, &keyboardProc, module, threadId);
    }
    return gRealSetHookA(idHook, proc, module, threadId);
}

HHOOK WINAPI hookedSetHookW(int idHook, HOOKPROC proc, HINSTANCE module, DWORD threadId) {
    if (captureGameProc(idHook, proc, "W")) {
        return gRealSetHookW(idHook, &keyboardProc, module, threadId);
    }
    return gRealSetHookW(idHook, proc, module, threadId);
}

} // namespace

bool install(HMODULE gameModule, KeyEventCallback callback) {
    gCallback = std::move(callback);
    gRealSetHookA = reinterpret_cast<SetHookAFn>(patchImport(
        gameModule, "user32.dll", {"SetWindowsHookExA", 0}, reinterpret_cast<void*>(&hookedSetHookA)));
    gRealSetHookW = reinterpret_cast<SetHookWFn>(patchImport(
        gameModule, "user32.dll", {"SetWindowsHookExW", 0}, reinterpret_cast<void*>(&hookedSetHookW)));
    return gRealSetHookA != nullptr || gRealSetHookW != nullptr;
}

bool isActive() { return gGameProc.load() != nullptr; }

} // namespace vcontroller::KeyboardHook
