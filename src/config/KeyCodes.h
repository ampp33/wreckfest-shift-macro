#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vwheel {

/// Translates between the Linux kernel's KEY_*/BTN_*/ABS_* symbolic names
/// (as used in linux/input-event-codes.h, and thus in TOML config files)
/// and their numeric evdev/uinput codes.
///
/// Keeping this table separate from Config.cpp lets both the keyboard
/// binding parser and any future tooling (e.g. a `--list-keys` CLI flag)
/// share a single source of truth.
namespace KeyCodes {

/// Looks up a KEY_* name (e.g. "KEY_Q", "KEY_F11"). Returns nullopt if the
/// name is not recognized.
std::optional<std::uint16_t> lookupKey(std::string_view name);

/// Looks up an ABS_* axis name (e.g. "ABS_X", "ABS_RY").
std::optional<std::uint16_t> lookupAxis(std::string_view name);

/// Returns the symbolic KEY_* name for a numeric code, or "KEY_<n>" if
/// unknown. Used for diagnostic logging.
std::string keyName(std::uint16_t code);

} // namespace KeyCodes

} // namespace vwheel
