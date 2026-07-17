#include "config/KeyCodes.h"

#include <linux/input-event-codes.h>

#include <unordered_map>

namespace vwheel::KeyCodes {

namespace {

// X-macro list of the keyboard keys we support by name in config files.
// This intentionally covers the practical set (letters, digits, function
// keys, navigation, numpad, modifiers, punctuation) rather than every
// exotic multimedia key defined by the kernel header.
#define VWHEEL_KEY_TABLE(X)                                                                      \
    X(KEY_ESC) X(KEY_1) X(KEY_2) X(KEY_3) X(KEY_4) X(KEY_5) X(KEY_6) X(KEY_7) X(KEY_8) X(KEY_9)   \
        X(KEY_0) X(KEY_MINUS) X(KEY_EQUAL) X(KEY_BACKSPACE) X(KEY_TAB) X(KEY_Q) X(KEY_W) X(KEY_E) \
            X(KEY_R) X(KEY_T) X(KEY_Y) X(KEY_U) X(KEY_I) X(KEY_O) X(KEY_P) X(KEY_LEFTBRACE)       \
                X(KEY_RIGHTBRACE) X(KEY_ENTER) X(KEY_LEFTCTRL) X(KEY_A) X(KEY_S) X(KEY_D) X(KEY_F)\
                    X(KEY_G) X(KEY_H) X(KEY_J) X(KEY_K) X(KEY_L) X(KEY_SEMICOLON)                 \
                        X(KEY_APOSTROPHE) X(KEY_GRAVE) X(KEY_LEFTSHIFT) X(KEY_BACKSLASH) X(KEY_Z) \
                            X(KEY_X) X(KEY_C) X(KEY_V) X(KEY_B) X(KEY_N) X(KEY_M) X(KEY_COMMA)     \
                                X(KEY_DOT) X(KEY_SLASH) X(KEY_RIGHTSHIFT) X(KEY_KPASTERISK)        \
                                    X(KEY_LEFTALT) X(KEY_SPACE) X(KEY_CAPSLOCK) X(KEY_F1)          \
                                        X(KEY_F2) X(KEY_F3) X(KEY_F4) X(KEY_F5) X(KEY_F6)          \
                                            X(KEY_F7) X(KEY_F8) X(KEY_F9) X(KEY_F10) X(KEY_NUMLOCK)\
                                                X(KEY_SCROLLLOCK) X(KEY_KP7) X(KEY_KP8) X(KEY_KP9) \
                                                    X(KEY_KPMINUS) X(KEY_KP4) X(KEY_KP5)           \
                                                        X(KEY_KP6) X(KEY_KPPLUS) X(KEY_KP1)        \
                                                            X(KEY_KP2) X(KEY_KP3) X(KEY_KP0)       \
                                                                X(KEY_KPDOT) X(KEY_F11) X(KEY_F12) \
    X(KEY_F13) X(KEY_F14) X(KEY_F15) X(KEY_F16) X(KEY_F17) X(KEY_F18) X(KEY_F19) X(KEY_F20)        \
        X(KEY_F21) X(KEY_F22) X(KEY_F23) X(KEY_F24) X(KEY_KPENTER) X(KEY_RIGHTCTRL)                \
            X(KEY_KPSLASH) X(KEY_SYSRQ) X(KEY_RIGHTALT) X(KEY_HOME) X(KEY_UP) X(KEY_PAGEUP)        \
                X(KEY_LEFT) X(KEY_RIGHT) X(KEY_END) X(KEY_DOWN) X(KEY_PAGEDOWN) X(KEY_INSERT)      \
                    X(KEY_DELETE) X(KEY_MUTE) X(KEY_VOLUMEDOWN) X(KEY_VOLUMEUP) X(KEY_PAUSE)       \
                        X(KEY_LEFTMETA) X(KEY_RIGHTMETA) X(KEY_COMPOSE) X(KEY_MENU)

#define VWHEEL_KEY_MAP_ENTRY(name) {#name, name},

const std::unordered_map<std::string_view, std::uint16_t> kKeyByName = {
    VWHEEL_KEY_TABLE(VWHEEL_KEY_MAP_ENTRY)
};

#undef VWHEEL_KEY_MAP_ENTRY
#undef VWHEEL_KEY_TABLE

#define VWHEEL_AXIS_TABLE(X) \
    X(ABS_X) X(ABS_Y) X(ABS_Z) X(ABS_RX) X(ABS_RY) X(ABS_RZ) X(ABS_HAT0X) X(ABS_HAT0Y)

#define VWHEEL_AXIS_MAP_ENTRY(name) {#name, name},

const std::unordered_map<std::string_view, std::uint16_t> kAxisByName = {
    VWHEEL_AXIS_TABLE(VWHEEL_AXIS_MAP_ENTRY)
};

#undef VWHEEL_AXIS_MAP_ENTRY
#undef VWHEEL_AXIS_TABLE

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
    return "KEY_" + std::to_string(code);
}

} // namespace vwheel::KeyCodes
