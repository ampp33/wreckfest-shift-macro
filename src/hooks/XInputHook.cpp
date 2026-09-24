#include "hooks/XInputHook.h"

#include <xinput.h>

#include <atomic>
#include <string>

#include "controller/VirtualController.h"
#include "hooks/ImportHook.h"
#include "logging/Logger.h"

namespace vcontroller::XInputHook {

namespace {

using GetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
using SetStateFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
using GetCapabilitiesFn = DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*);

VirtualController* gController = nullptr;
std::atomic<std::uint32_t> gSlot{0};

GetStateFn gRealGetState = nullptr;
SetStateFn gRealSetState = nullptr;
GetCapabilitiesFn gRealGetCapabilities = nullptr;

DWORD WINAPI hookedGetState(DWORD userIndex, XINPUT_STATE* state) {
    if (userIndex == gSlot.load(std::memory_order_relaxed)) {
        if (state == nullptr) {
            return ERROR_BAD_ARGUMENTS;
        }
        gController->readState(*state);
        return ERROR_SUCCESS;
    }
    return gRealGetState ? gRealGetState(userIndex, state) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI hookedSetState(DWORD userIndex, XINPUT_VIBRATION* vibration) {
    if (userIndex == gSlot.load(std::memory_order_relaxed)) {
        return ERROR_SUCCESS; // no motors to drive; accept and ignore rumble
    }
    return gRealSetState ? gRealSetState(userIndex, vibration) : ERROR_DEVICE_NOT_CONNECTED;
}

DWORD WINAPI hookedGetCapabilities(DWORD userIndex, DWORD flags, XINPUT_CAPABILITIES* caps) {
    if (userIndex == gSlot.load(std::memory_order_relaxed)) {
        if (caps == nullptr) {
            return ERROR_BAD_ARGUMENTS;
        }
        // Report a full-featured wired Xbox 360 gamepad: every button
        // present and every analog axis at full resolution.
        *caps = XINPUT_CAPABILITIES{};
        caps->Type = XINPUT_DEVTYPE_GAMEPAD;
        caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
        caps->Gamepad.wButtons = 0xF3FF;
        caps->Gamepad.bLeftTrigger = 0xFF;
        caps->Gamepad.bRightTrigger = 0xFF;
        caps->Gamepad.sThumbLX = static_cast<SHORT>(0xFFC0);
        caps->Gamepad.sThumbLY = static_cast<SHORT>(0xFFC0);
        caps->Gamepad.sThumbRX = static_cast<SHORT>(0xFFC0);
        caps->Gamepad.sThumbRY = static_cast<SHORT>(0xFFC0);
        caps->Vibration.wLeftMotorSpeed = 0xFF;
        caps->Vibration.wRightMotorSpeed = 0xFF;
        return ERROR_SUCCESS;
    }
    return gRealGetCapabilities ? gRealGetCapabilities(userIndex, flags, caps)
                                : ERROR_DEVICE_NOT_CONNECTED;
}

// Every XInput DLL name a game might link against. Wreckfest uses
// xinput1_3; the rest are cheap to check and keep this reusable.
constexpr const char* kXInputDlls[] = {
    "xinput1_3.dll", "xinput1_4.dll", "xinput9_1_0.dll", "xinput1_2.dll", "xinput1_1.dll",
};

} // namespace

bool install(HMODULE gameModule, VirtualController& controller, std::uint32_t slot) {
    gController = &controller;
    gSlot.store(slot);

    for (const char* dll : kXInputDlls) {
        // Ordinals are XInput's documented export ordinals: 2 = GetState,
        // 3 = SetState, 4 = GetCapabilities. Wreckfest imports them by
        // ordinal only.
        void* realGetState = patchImport(gameModule, dll, {"XInputGetState", 2},
                                         reinterpret_cast<void*>(&hookedGetState));
        if (realGetState == nullptr) {
            continue;
        }
        gRealGetState = reinterpret_cast<GetStateFn>(realGetState);
        gRealSetState = reinterpret_cast<SetStateFn>(patchImport(
            gameModule, dll, {"XInputSetState", 3}, reinterpret_cast<void*>(&hookedSetState)));
        gRealGetCapabilities = reinterpret_cast<GetCapabilitiesFn>(
            patchImport(gameModule, dll, {"XInputGetCapabilities", 4},
                        reinterpret_cast<void*>(&hookedGetCapabilities)));

        Logger::instance().info("Hooked " + std::string(dll) +
                                "; virtual controller is XInput slot " + std::to_string(slot));
        return true;
    }
    return false;
}

void setSlot(std::uint32_t slot) {
    if (gSlot.exchange(slot) != slot) {
        Logger::instance().info("Virtual controller moved to XInput slot " + std::to_string(slot));
    }
}

} // namespace vcontroller::XInputHook
