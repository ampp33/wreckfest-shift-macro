#include "config/Config.h"

#include <toml.hpp>

#include <stdexcept>

#include "config/KeyCodes.h"

namespace vcontroller {

namespace {

/// Resolves a KEY_* name from a TOML string value, returning 0 (unbound)
/// when the TOML key is absent. Throws if the key is present but names an
/// unrecognized key.
std::uint16_t optionalKey(const toml::table& table, std::string_view tomlKey) {
    const auto value = table[tomlKey].value<std::string>();
    if (!value) {
        return 0;
    }
    const auto code = KeyCodes::lookupKey(*value);
    if (!code) {
        throw std::runtime_error("unrecognized key name '" + *value + "' for binding " +
                                  std::string(tomlKey));
    }
    return *code;
}

std::uint16_t requireAxis(const toml::table& table, std::string_view tomlKey) {
    const auto value = table[tomlKey].value<std::string>();
    if (!value) {
        throw std::runtime_error("missing or non-string axis: " + std::string(tomlKey));
    }
    const auto code = KeyCodes::lookupAxis(*value);
    if (!code) {
        throw std::runtime_error("unrecognized axis name '" + *value + "' for " +
                                  std::string(tomlKey));
    }
    return *code;
}

} // namespace

Config Config::loadFromFile(const std::filesystem::path& path) {
    toml::table root;
    try {
        root = toml::parse_file(path.string());
    } catch (const toml::parse_error& error) {
        throw std::runtime_error("failed to parse config '" + path.string() +
                                  "': " + std::string(error.description()));
    }

    Config config;

    if (const auto* controller = root["controller"].as_table()) {
        const auto slot = (*controller)["slot"].value_or(0);
        if (slot < 0 || slot > 3) {
            throw std::runtime_error("[controller].slot must be 0-3 (got " +
                                      std::to_string(slot) + ")");
        }
        config.controller.slot = static_cast<std::uint32_t>(slot);
    }

    if (const auto* mode = root["mode"].as_table()) {
        config.mode.drivingHotkey = optionalKey(*mode, "driving_hotkey");
        config.mode.chatHotkey = optionalKey(*mode, "chat_hotkey");
        config.mode.keybindHotkey = optionalKey(*mode, "keybind_hotkey");
    }
    if (config.mode.drivingHotkey == 0) {
        throw std::runtime_error("[mode].driving_hotkey must be set to a valid KEY_* name");
    }
    if (config.mode.chatHotkey == 0) {
        throw std::runtime_error("[mode].chat_hotkey must be set to a valid KEY_* name");
    }

    if (const auto* bindings = root["bindings"].as_table()) {
        config.bindings.gear1 = optionalKey(*bindings, "gear1");
        config.bindings.gear2 = optionalKey(*bindings, "gear2");
        config.bindings.gear3 = optionalKey(*bindings, "gear3");
        config.bindings.gear4 = optionalKey(*bindings, "gear4");
        config.bindings.gear5 = optionalKey(*bindings, "gear5");
        config.bindings.gear6 = optionalKey(*bindings, "gear6");
        config.bindings.reverse = optionalKey(*bindings, "reverse");

        config.bindings.throttle = optionalKey(*bindings, "throttle");
        config.bindings.brake = optionalKey(*bindings, "brake");

        config.bindings.steerLeft = optionalKey(*bindings, "steer_left");
        config.bindings.steerRight = optionalKey(*bindings, "steer_right");

        config.bindings.handbrake = optionalKey(*bindings, "handbrake");

        config.bindings.clutch = optionalKey(*bindings, "clutch");

        config.bindings.reset = optionalKey(*bindings, "reset");
    }

    if (const auto* clutch = root["clutch"].as_table()) {
        config.clutch.enabled = (*clutch)["enabled"].value_or(true);
        if (config.clutch.enabled) {
            config.clutch.axis = requireAxis(*clutch, "axis");
        }
        config.clutch.pressValue = (*clutch)["press_value"].value_or(32767);
        config.clutch.releaseValue = (*clutch)["release_value"].value_or(0);
        config.clutch.pressDelay =
            std::chrono::milliseconds((*clutch)["press_delay_ms"].value_or(2));
        config.clutch.releaseDelay =
            std::chrono::milliseconds((*clutch)["release_delay_ms"].value_or(2));
    } else {
        config.clutch.enabled = false;
    }

    if (const auto* steering = root["steering"].as_table()) {
        config.steering.mode = (*steering)["mode"].value_or(std::string{"digital"});
        config.steering.maxValue = (*steering)["max_value"].value_or(32767);
        config.steering.rampSpeed = (*steering)["ramp_speed"].value_or(5000.0);
        config.steering.returnSpeed = (*steering)["return_speed"].value_or(5000.0);
        config.steering.instant = (*steering)["instant"].value_or(false);
        config.steering.useDpad = (*steering)["use_dpad"].value_or(false);
    }
    if (config.steering.mode != "digital") {
        throw std::runtime_error("unrecognized [steering].mode '" + config.steering.mode +
                                  "': only \"digital\" is currently supported");
    }

    if (const auto* logging = root["logging"].as_table()) {
        config.logLevel = Logger::parseLevel((*logging)["level"].value_or(std::string{"info"}));
    }

    return config;
}

} // namespace vcontroller
