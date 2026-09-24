#include "config/KeyCodes.h"

#include <windows.h>

#include <unordered_map>

#include "controller/VirtualController.h"

namespace vcontroller::KeyCodes {

namespace {

// Config files name keys with the Linux kernel's KEY_* names (so configs
// from the Linux daemon version keep working); each maps to the Windows
// virtual-key code the game's keyboard hook reports. This covers the
// practical set (letters, digits, function keys, navigation, numpad,
// modifiers, punctuation). Punctuation VKs are for a US layout.
//
// Numpad caveat: with Num Lock off, Windows reports the numpad's
// navigation keys as the ordinary arrow/Home/End/... keys, so KEY_KP8 and
// KEY_UP are only distinguishable with Num Lock on. There is no separate
// VK for the numpad Enter key, so KEY_KPENTER isn't supported.
const std::unordered_map<std::string_view, std::uint16_t> kKeyByName = {
    {"KEY_ESC", VK_ESCAPE},
    {"KEY_1", '1'}, {"KEY_2", '2'}, {"KEY_3", '3'}, {"KEY_4", '4'}, {"KEY_5", '5'},
    {"KEY_6", '6'}, {"KEY_7", '7'}, {"KEY_8", '8'}, {"KEY_9", '9'}, {"KEY_0", '0'},
    {"KEY_MINUS", VK_OEM_MINUS}, {"KEY_EQUAL", VK_OEM_PLUS}, {"KEY_BACKSPACE", VK_BACK},
    {"KEY_TAB", VK_TAB},
    {"KEY_Q", 'Q'}, {"KEY_W", 'W'}, {"KEY_E", 'E'}, {"KEY_R", 'R'}, {"KEY_T", 'T'},
    {"KEY_Y", 'Y'}, {"KEY_U", 'U'}, {"KEY_I", 'I'}, {"KEY_O", 'O'}, {"KEY_P", 'P'},
    {"KEY_LEFTBRACE", VK_OEM_4}, {"KEY_RIGHTBRACE", VK_OEM_6}, {"KEY_ENTER", VK_RETURN},
    {"KEY_LEFTCTRL", VK_LCONTROL},
    {"KEY_A", 'A'}, {"KEY_S", 'S'}, {"KEY_D", 'D'}, {"KEY_F", 'F'}, {"KEY_G", 'G'},
    {"KEY_H", 'H'}, {"KEY_J", 'J'}, {"KEY_K", 'K'}, {"KEY_L", 'L'},
    {"KEY_SEMICOLON", VK_OEM_1}, {"KEY_APOSTROPHE", VK_OEM_7}, {"KEY_GRAVE", VK_OEM_3},
    {"KEY_LEFTSHIFT", VK_LSHIFT}, {"KEY_BACKSLASH", VK_OEM_5},
    {"KEY_Z", 'Z'}, {"KEY_X", 'X'}, {"KEY_C", 'C'}, {"KEY_V", 'V'}, {"KEY_B", 'B'},
    {"KEY_N", 'N'}, {"KEY_M", 'M'},
    {"KEY_COMMA", VK_OEM_COMMA}, {"KEY_DOT", VK_OEM_PERIOD}, {"KEY_SLASH", VK_OEM_2},
    {"KEY_RIGHTSHIFT", VK_RSHIFT}, {"KEY_LEFTALT", VK_LMENU}, {"KEY_SPACE", VK_SPACE},
    {"KEY_CAPSLOCK", VK_CAPITAL},
    {"KEY_F1", VK_F1}, {"KEY_F2", VK_F2}, {"KEY_F3", VK_F3}, {"KEY_F4", VK_F4},
    {"KEY_F5", VK_F5}, {"KEY_F6", VK_F6}, {"KEY_F7", VK_F7}, {"KEY_F8", VK_F8},
    {"KEY_F9", VK_F9}, {"KEY_F10", VK_F10}, {"KEY_F11", VK_F11}, {"KEY_F12", VK_F12},
    {"KEY_F13", VK_F13}, {"KEY_F14", VK_F14}, {"KEY_F15", VK_F15}, {"KEY_F16", VK_F16},
    {"KEY_F17", VK_F17}, {"KEY_F18", VK_F18}, {"KEY_F19", VK_F19}, {"KEY_F20", VK_F20},
    {"KEY_F21", VK_F21}, {"KEY_F22", VK_F22}, {"KEY_F23", VK_F23}, {"KEY_F24", VK_F24},
    {"KEY_NUMLOCK", VK_NUMLOCK}, {"KEY_SCROLLLOCK", VK_SCROLL},
    {"KEY_KP0", VK_NUMPAD0}, {"KEY_KP1", VK_NUMPAD1}, {"KEY_KP2", VK_NUMPAD2},
    {"KEY_KP3", VK_NUMPAD3}, {"KEY_KP4", VK_NUMPAD4}, {"KEY_KP5", VK_NUMPAD5},
    {"KEY_KP6", VK_NUMPAD6}, {"KEY_KP7", VK_NUMPAD7}, {"KEY_KP8", VK_NUMPAD8},
    {"KEY_KP9", VK_NUMPAD9},
    {"KEY_KPASTERISK", VK_MULTIPLY}, {"KEY_KPMINUS", VK_SUBTRACT}, {"KEY_KPPLUS", VK_ADD},
    {"KEY_KPDOT", VK_DECIMAL}, {"KEY_KPSLASH", VK_DIVIDE},
    {"KEY_RIGHTCTRL", VK_RCONTROL}, {"KEY_SYSRQ", VK_SNAPSHOT}, {"KEY_RIGHTALT", VK_RMENU},
    {"KEY_HOME", VK_HOME}, {"KEY_UP", VK_UP}, {"KEY_PAGEUP", VK_PRIOR}, {"KEY_LEFT", VK_LEFT},
    {"KEY_RIGHT", VK_RIGHT}, {"KEY_END", VK_END}, {"KEY_DOWN", VK_DOWN},
    {"KEY_PAGEDOWN", VK_NEXT}, {"KEY_INSERT", VK_INSERT}, {"KEY_DELETE", VK_DELETE},
    {"KEY_MUTE", VK_VOLUME_MUTE}, {"KEY_VOLUMEDOWN", VK_VOLUME_DOWN},
    {"KEY_VOLUMEUP", VK_VOLUME_UP}, {"KEY_PAUSE", VK_PAUSE},
    {"KEY_LEFTMETA", VK_LWIN}, {"KEY_RIGHTMETA", VK_RWIN}, {"KEY_MENU", VK_APPS},
    {"KEY_COMPOSE", VK_APPS},
};

const std::unordered_map<std::string_view, std::uint16_t> kAxisByName = {
    {"ABS_X", VirtualController::kLeftStickX},    {"ABS_Y", VirtualController::kLeftStickY},
    {"ABS_Z", VirtualController::kLeftTrigger},   {"ABS_RX", VirtualController::kRightStickX},
    {"ABS_RY", VirtualController::kRightStickY},  {"ABS_RZ", VirtualController::kRightTrigger},
    {"ABS_HAT0X", VirtualController::kDpadX},     {"ABS_HAT0Y", VirtualController::kDpadY},
};

} // namespace

std::optional<std::uint16_t> lookupKey(std::string_view name) {
    const auto it = kKeyByName.find(name);
    if (it == kKeyByName.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::uint16_t> lookupAxis(std::string_view name) {
    const auto it = kAxisByName.find(name);
    if (it == kAxisByName.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string keyName(std::uint16_t code) {
    for (const auto& [name, value] : kKeyByName) {
        if (value == code) {
            return std::string(name);
        }
    }
    return "VK_" + std::to_string(code);
}

} // namespace vcontroller::KeyCodes
