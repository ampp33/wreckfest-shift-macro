#pragma once
// Minimal stand-in for <windows.h> so the plugin's portable logic
// (ModeManager, ClutchController, VirtualController, Config, KeyCodes)
// builds natively on Linux for the test harness. Only what those files
// touch is declared, with the real Windows values.

#include <cstdint>

using BYTE = std::uint8_t;
using WORD = std::uint16_t;
using SHORT = std::int16_t;
using DWORD = std::uint32_t;
using HMODULE = void*;

// Virtual-key codes used by src/config/KeyCodes.cpp.
enum : unsigned {
    VK_BACK = 0x08, VK_TAB = 0x09, VK_RETURN = 0x0D, VK_PAUSE = 0x13, VK_CAPITAL = 0x14,
    VK_ESCAPE = 0x1B, VK_SPACE = 0x20, VK_PRIOR = 0x21, VK_NEXT = 0x22, VK_END = 0x23,
    VK_HOME = 0x24, VK_LEFT = 0x25, VK_UP = 0x26, VK_RIGHT = 0x27, VK_DOWN = 0x28,
    VK_SNAPSHOT = 0x2C, VK_INSERT = 0x2D, VK_DELETE = 0x2E,
    VK_LWIN = 0x5B, VK_RWIN = 0x5C, VK_APPS = 0x5D,
    VK_NUMPAD0 = 0x60, VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD4, VK_NUMPAD5, VK_NUMPAD6,
    VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9,
    VK_MULTIPLY = 0x6A, VK_ADD = 0x6B, VK_SUBTRACT = 0x6D, VK_DECIMAL = 0x6E, VK_DIVIDE = 0x6F,
    VK_F1 = 0x70, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12,
    VK_F13, VK_F14, VK_F15, VK_F16, VK_F17, VK_F18, VK_F19, VK_F20, VK_F21, VK_F22, VK_F23, VK_F24,
    VK_NUMLOCK = 0x90, VK_SCROLL = 0x91,
    VK_LSHIFT = 0xA0, VK_RSHIFT = 0xA1, VK_LCONTROL = 0xA2, VK_RCONTROL = 0xA3, VK_LMENU = 0xA4,
    VK_RMENU = 0xA5,
    VK_VOLUME_MUTE = 0xAD, VK_VOLUME_DOWN = 0xAE, VK_VOLUME_UP = 0xAF,
    VK_OEM_1 = 0xBA, VK_OEM_PLUS = 0xBB, VK_OEM_COMMA = 0xBC, VK_OEM_MINUS = 0xBD,
    VK_OEM_PERIOD = 0xBE, VK_OEM_2 = 0xBF, VK_OEM_3 = 0xC0,
    VK_OEM_4 = 0xDB, VK_OEM_5 = 0xDC, VK_OEM_6 = 0xDD, VK_OEM_7 = 0xDE,
};
