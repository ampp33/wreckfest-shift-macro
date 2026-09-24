#pragma once

#include <windows.h>

#include <cstdint>

namespace vcontroller {

class VirtualController;

/// Makes `controller` appear to the game as a connected Xbox 360 pad in
/// one XInput slot, by redirecting the game executable's imports of
/// XInputGetState / XInputSetState / XInputGetCapabilities.
///
/// Calls for every other slot go straight through to the real XInput, so
/// physical controllers in other slots keep working. A physical
/// controller in the *same* slot is hidden while the plugin is loaded.
namespace XInputHook {

/// Patches `gameModule`'s XInput imports. `controller` must outlive the
/// process (the plugin never unloads). Returns false — and patches
/// nothing — if the module doesn't import XInputGetState from any known
/// XInput DLL.
bool install(HMODULE gameModule, VirtualController& controller, std::uint32_t slot);

/// Changes which slot the virtual controller occupies (config reload).
void setSlot(std::uint32_t slot);

} // namespace XInputHook

} // namespace vcontroller
