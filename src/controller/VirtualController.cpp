#include "controller/VirtualController.h"

#include <algorithm>

namespace vcontroller {

namespace {

SHORT toStick(std::int32_t value) {
    return static_cast<SHORT>(
        std::clamp(value, VirtualController::kStickMin, VirtualController::kStickMax));
}

BYTE toTrigger(std::int32_t value) {
    return static_cast<BYTE>(
        std::clamp(value, VirtualController::kTriggerMin, VirtualController::kTriggerMax));
}

/// Writes a -1/0/1 hat value onto a pair of D-pad button bits.
void setDpadPair(WORD& buttons, WORD negativeBit, WORD positiveBit, std::int32_t value) {
    buttons &= static_cast<WORD>(~(negativeBit | positiveBit));
    if (value < 0) {
        buttons |= negativeBit;
    } else if (value > 0) {
        buttons |= positiveBit;
    }
}

} // namespace

void VirtualController::setButton(std::uint16_t code, bool pressed) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pressed) {
        staged_.wButtons |= code;
    } else {
        staged_.wButtons &= static_cast<WORD>(~code);
    }
}

void VirtualController::setAxis(std::uint16_t code, std::int32_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (code) {
        case kLeftStickX: staged_.sThumbLX = toStick(value); break;
        case kLeftStickY: staged_.sThumbLY = toStick(value); break;
        case kRightStickX: staged_.sThumbRX = toStick(value); break;
        case kRightStickY: staged_.sThumbRY = toStick(value); break;
        case kLeftTrigger: staged_.bLeftTrigger = toTrigger(value); break;
        case kRightTrigger: staged_.bRightTrigger = toTrigger(value); break;
        case kDpadX:
            setDpadPair(staged_.wButtons, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT, value);
            break;
        case kDpadY:
            setDpadPair(staged_.wButtons, XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, value);
            break;
        default: break;
    }
}

void VirtualController::syncReport() {
    std::lock_guard<std::mutex> lock(mutex_);
    published_ = staged_;
    ++packetNumber_;
}

void VirtualController::resetAllInputs() {
    std::lock_guard<std::mutex> lock(mutex_);
    staged_ = XINPUT_GAMEPAD{};
    published_ = staged_;
    ++packetNumber_;
}

void VirtualController::readState(XINPUT_STATE& state) {
    std::lock_guard<std::mutex> lock(mutex_);
    state.dwPacketNumber = packetNumber_;
    state.Gamepad = published_;
}

} // namespace vcontroller
