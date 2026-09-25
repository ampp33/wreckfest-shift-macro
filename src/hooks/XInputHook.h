#pragma once

#include <windows.h>
#include <xinput.h>

#include <cstdint>
#include <functional>

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

/// Optional callbacks for the game's calls on the virtual slot. They run
/// on whichever game thread made the call, so they must be quick.
struct Listeners {
    /// Just before a poll reads the published state — whatever it
    /// publishes is what this very poll returns.
    std::function<void()> beforePoll;
    /// With the state the poll actually returned.
    std::function<void(const XINPUT_GAMEPAD&)> afterPoll;
};

/// Patches `gameModule`'s XInput imports. `controller` must outlive the
/// process (the plugin never unloads). Returns false — and patches
/// nothing — if the module doesn't import XInputGetState from any known
/// XInput DLL.
bool install(HMODULE gameModule, VirtualController& controller, std::uint32_t slot,
             Listeners listeners = {});

/// Changes which slot the virtual controller occupies (config reload).
void setSlot(std::uint32_t slot);

/// Logs (at debug level) how often the game has polled the virtual
/// controller since the previous call — call count, min/avg/max gap
/// between polls, and a histogram of the gaps — then resets the counters.
/// Logs nothing if the game didn't poll at all. This is the game's real
/// input sampling rate, which bounds how short release_delay_ms can be
/// before the game can miss the clutch-and-gear overlap entirely.
void logPollTiming();

} // namespace XInputHook

} // namespace vcontroller
