#include "controller/VirtualController.h"

#include <linux/input.h>
#include <linux/uinput.h>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "logging/Logger.h"

namespace vwheel {

namespace {

/// Registers one absolute axis's value range with the in-progress uinput
/// device via the modern UI_ABS_SETUP ioctl (kernel >= 4.5).
void setupAbsAxis(int fd, std::uint16_t code, std::int32_t minimum, std::int32_t maximum,
                   std::int32_t fuzz, std::int32_t flat) {
    if (ioctl(fd, UI_SET_ABSBIT, code) < 0) {
        throw std::runtime_error("UI_SET_ABSBIT failed: " + std::string(std::strerror(errno)));
    }

    uinput_abs_setup absSetup{};
    absSetup.code = code;
    absSetup.absinfo.minimum = minimum;
    absSetup.absinfo.maximum = maximum;
    absSetup.absinfo.fuzz = fuzz;
    absSetup.absinfo.flat = flat;
    absSetup.absinfo.value = 0;

    if (ioctl(fd, UI_ABS_SETUP, &absSetup) < 0) {
        throw std::runtime_error("UI_ABS_SETUP failed for axis " + std::to_string(code) + ": " +
                                  std::string(std::strerror(errno)));
    }
}

} // namespace

VirtualController::VirtualController() { createDevice(); }

VirtualController::~VirtualController() {
    if (fd_ >= 0) {
        ioctl(fd_, UI_DEV_DESTROY);
        close(fd_);
        fd_ = -1;
        Logger::instance().debug("Destroyed virtual controller uinput device");
    }
}

void VirtualController::createDevice() {
    fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd_ < 0) {
        throw std::runtime_error(
            "failed to open /dev/uinput: " + std::string(std::strerror(errno)) +
            " (is the uinput kernel module loaded, and do you have write permission — "
            "typically via the 'input' group or a udev rule?)");
    }

    if (ioctl(fd_, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(fd_, UI_SET_EVBIT, EV_ABS) < 0) {
        throw std::runtime_error("UI_SET_EVBIT failed: " + std::string(std::strerror(errno)));
    }

    static constexpr std::array<std::uint16_t, 11> kAllButtons = {
        kButtonA,    kButtonB,     kButtonX,     kButtonY,      kButtonLB,   kButtonRB,
        kButtonBack, kButtonStart, kButtonGuide, kButtonThumbL, kButtonThumbR,
    };
    for (const std::uint16_t code : kAllButtons) {
        if (ioctl(fd_, UI_SET_KEYBIT, code) < 0) {
            throw std::runtime_error("UI_SET_KEYBIT failed for button " + std::to_string(code) +
                                      ": " + std::string(std::strerror(errno)));
        }
    }

    // Sticks: signed 16-bit, small fuzz/flat to match a real Xbox pad's
    // deadzone/noise characteristics.
    setupAbsAxis(fd_, kLeftStickX, kStickMin, kStickMax, 16, 128);
    setupAbsAxis(fd_, kLeftStickY, kStickMin, kStickMax, 16, 128);
    setupAbsAxis(fd_, kRightStickX, kStickMin, kStickMax, 16, 128);
    setupAbsAxis(fd_, kRightStickY, kStickMin, kStickMax, 16, 128);
    // Triggers: unsigned 8-bit, no deadzone.
    setupAbsAxis(fd_, kLeftTrigger, kTriggerMin, kTriggerMax, 0, 0);
    setupAbsAxis(fd_, kRightTrigger, kTriggerMin, kTriggerMax, 0, 0);
    // D-pad: three-position hat.
    setupAbsAxis(fd_, kDpadX, -1, 1, 0, 0);
    setupAbsAxis(fd_, kDpadY, -1, 1, 0, 0);

    uinput_setup usetup{};
    std::memset(&usetup, 0, sizeof(usetup));
    // Report the real Microsoft Xbox 360 Controller vendor/product IDs so
    // the kernel's xpad quirks, SDL2's gamecontrollerdb, and Steam Input
    // all recognize this as a standard Xbox 360 pad without any extra
    // configuration.
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x045e;
    usetup.id.product = 0x028e;
    usetup.id.version = 0x0110;
    std::strncpy(usetup.name, "Virtual Xbox 360 Controller (Wreckfest Virtual Wheel)",
                 sizeof(usetup.name) - 1);

    if (ioctl(fd_, UI_DEV_SETUP, &usetup) < 0) {
        throw std::runtime_error("UI_DEV_SETUP failed: " + std::string(std::strerror(errno)));
    }
    if (ioctl(fd_, UI_DEV_CREATE) < 0) {
        throw std::runtime_error("UI_DEV_CREATE failed: " + std::string(std::strerror(errno)));
    }

    Logger::instance().info("Created virtual Xbox 360 controller via uinput");
}

void VirtualController::writeEvent(std::uint16_t type, std::uint16_t code, std::int32_t value) {
    input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (write(fd_, &ev, sizeof(ev)) < 0) {
        Logger::instance().warning("uinput write failed: " + std::string(std::strerror(errno)));
    }
}

void VirtualController::setButton(std::uint16_t code, bool pressed) {
    std::lock_guard<std::mutex> lock(mutex_);
    writeEvent(EV_KEY, code, pressed ? 1 : 0);
}

void VirtualController::setAxis(std::uint16_t code, std::int32_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    writeEvent(EV_ABS, code, value);
}

void VirtualController::syncReport() {
    std::lock_guard<std::mutex> lock(mutex_);
    writeEvent(EV_SYN, SYN_REPORT, 0);
}

void VirtualController::resetAllInputs() {
    // Held for the whole batch (rather than via the public setters, which
    // would each lock/unlock individually) so this reset always lands as
    // one atomic frame, even with the clutch worker thread writing
    // concurrently.
    std::lock_guard<std::mutex> lock(mutex_);
    for (const std::uint16_t code :
         {kButtonA, kButtonB, kButtonX, kButtonY, kButtonLB, kButtonRB, kButtonBack, kButtonStart,
          kButtonGuide, kButtonThumbL, kButtonThumbR}) {
        writeEvent(EV_KEY, code, 0);
    }
    writeEvent(EV_ABS, kLeftStickX, 0);
    writeEvent(EV_ABS, kLeftStickY, 0);
    writeEvent(EV_ABS, kRightStickX, 0);
    writeEvent(EV_ABS, kRightStickY, 0);
    writeEvent(EV_ABS, kLeftTrigger, kTriggerMin);
    writeEvent(EV_ABS, kRightTrigger, kTriggerMin);
    writeEvent(EV_ABS, kDpadX, 0);
    writeEvent(EV_ABS, kDpadY, 0);
    writeEvent(EV_SYN, SYN_REPORT, 0);
}

} // namespace vwheel
