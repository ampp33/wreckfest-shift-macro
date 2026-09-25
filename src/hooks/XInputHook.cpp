#include "hooks/XInputHook.h"

#include <xinput.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <mutex>
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
Listeners gListeners; // set once in install(), before any hook can run
std::atomic<std::uint32_t> gSlot{0};

GetStateFn gRealGetState = nullptr;
SetStateFn gRealSetState = nullptr;
GetCapabilitiesFn gRealGetCapabilities = nullptr;

// Poll-timing stats for logPollTiming(). Gap histogram bucket upper
// bounds in ms; the last bucket catches everything above.
constexpr double kGapBucketsMs[] = {1, 3, 5, 8, 12, 18, 35};
constexpr std::size_t kGapBucketCount = std::size(kGapBucketsMs) + 1;

struct PollTiming {
    std::uint64_t polls = 0;
    std::uint64_t gaps = 0;
    double minGapMs = 0;
    double maxGapMs = 0;
    double totalGapMs = 0;
    std::array<std::uint64_t, kGapBucketCount> histogram{};
};

std::mutex gTimingMutex;
PollTiming gTiming;
// Kept across logPollTiming() resets so the first gap after a report is
// still measured.
std::chrono::steady_clock::time_point gLastPoll;
bool gHaveLastPoll = false;

void recordPoll() {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(gTimingMutex);
    ++gTiming.polls;
    if (gHaveLastPoll) {
        const double gapMs = std::chrono::duration<double, std::milli>(now - gLastPoll).count();
        if (gTiming.gaps == 0 || gapMs < gTiming.minGapMs) gTiming.minGapMs = gapMs;
        if (gTiming.gaps == 0 || gapMs > gTiming.maxGapMs) gTiming.maxGapMs = gapMs;
        ++gTiming.gaps;
        gTiming.totalGapMs += gapMs;
        std::size_t bucket = 0;
        while (bucket < std::size(kGapBucketsMs) && gapMs >= kGapBucketsMs[bucket]) ++bucket;
        ++gTiming.histogram[bucket];
    }
    gLastPoll = now;
    gHaveLastPoll = true;
}

DWORD WINAPI hookedGetState(DWORD userIndex, XINPUT_STATE* state) {
    if (userIndex == gSlot.load(std::memory_order_relaxed)) {
        if (state == nullptr) {
            return ERROR_BAD_ARGUMENTS;
        }
        recordPoll();
        if (gListeners.beforePoll) gListeners.beforePoll();
        gController->readState(*state);
        if (gListeners.afterPoll) gListeners.afterPoll(state->Gamepad);
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

bool install(HMODULE gameModule, VirtualController& controller, std::uint32_t slot,
             Listeners listeners) {
    gController = &controller;
    gListeners = std::move(listeners);
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

void logPollTiming() {
    PollTiming timing;
    {
        std::lock_guard<std::mutex> lock(gTimingMutex);
        timing = gTiming;
        gTiming = PollTiming{};
    }
    if (timing.polls == 0 || Logger::instance().level() < LogLevel::Debug) {
        return;
    }

    char line[160];
    std::string message = "Game poll timing: " + std::to_string(timing.polls) + " polls";
    if (timing.gaps > 0) {
        const double avgGapMs = timing.totalGapMs / double(timing.gaps);
        std::snprintf(line, sizeof(line),
                      ", gap min %.2f / avg %.2f / max %.2f ms (~%.0f Hz); gaps ms:", timing.minGapMs,
                      avgGapMs, timing.maxGapMs, 1000.0 / avgGapMs);
        message += line;
        for (std::size_t i = 0; i < kGapBucketCount; ++i) {
            if (i < std::size(kGapBucketsMs)) {
                std::snprintf(line, sizeof(line), " <%g:%llu", kGapBucketsMs[i],
                              static_cast<unsigned long long>(timing.histogram[i]));
            } else {
                std::snprintf(line, sizeof(line), " >=%g:%llu", kGapBucketsMs[i - 1],
                              static_cast<unsigned long long>(timing.histogram[i]));
            }
            message += line;
        }
    }
    Logger::instance().debug(message);
}

} // namespace vcontroller::XInputHook
