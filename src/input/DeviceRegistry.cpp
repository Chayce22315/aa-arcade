#include "input/DeviceRegistry.hpp"

#include <algorithm>

#include "engine/Log.hpp"

namespace spindle::input {

std::string guidString(SDL_GUID guid) {
    char buf[64] = {};
    SDL_GUIDToString(guid, buf, static_cast<int>(sizeof buf));
    return buf;
}

std::string gamepadTypeLabel(SDL_GamepadType type) {
    switch (type) {
    case SDL_GAMEPAD_TYPE_XBOX360:
    case SDL_GAMEPAD_TYPE_XBOXONE: return "xbox";
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return "playstation";
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return "switch";
    default: return "gamepad";
    }
}

InputDevice* DeviceRegistry::find(DeviceKind kind, std::uint32_t id) {
    for (auto& d : devices_)
        if (d->kind == kind && d->id == id) return d.get();
    return nullptr;
}

InputDevice* DeviceRegistry::addKeyboard(SDL_KeyboardID id) {
    if (InputDevice* existing = find(DeviceKind::Keyboard, id)) return existing;
    auto d = std::make_unique<InputDevice>();
    d->kind = DeviceKind::Keyboard;
    d->id = id;
    const char* name = SDL_GetKeyboardNameForID(id);
    d->name = (name && *name) ? name : "keyboard";
    d->typeLabel = "keyboard";
    log::info("input: keyboard connected: {} (id {})", d->name, id);
    devices_.push_back(std::move(d));
    return devices_.back().get();
}

void DeviceRegistry::removeKeyboard(SDL_KeyboardID id) {
    std::erase_if(devices_, [&](const auto& d) {
        if (d->kind == DeviceKind::Keyboard && d->id == id) {
            log::info("input: keyboard disconnected: {} (id {})", d->name, id);
            return true;
        }
        return false;
    });
}

InputDevice* DeviceRegistry::addGamepad(SDL_JoystickID id) {
    if (InputDevice* existing = find(DeviceKind::Gamepad, id)) return existing;
    sdl::GamepadPtr pad(SDL_OpenGamepad(id));
    if (!pad) {
        log::warn("input: cannot open gamepad {}: {}", id, SDL_GetError());
        return nullptr;
    }
    auto d = std::make_unique<InputDevice>();
    d->kind = DeviceKind::Gamepad;
    d->id = id;
    const char* name = SDL_GetGamepadName(pad.get());
    d->name = (name && *name) ? name : "gamepad";
    d->guid = guidString(SDL_GetGamepadGUIDForID(id));
    const char* serial = SDL_GetGamepadSerial(pad.get());
    d->serial = serial ? serial : "";
    d->typeLabel = gamepadTypeLabel(SDL_GetGamepadType(pad.get()));
    d->gamepad = std::move(pad);
    log::info("input: {} controller connected: {} (id {}, guid {})", d->typeLabel, d->name, id, d->guid);
    devices_.push_back(std::move(d));
    return devices_.back().get();
}

InputDevice* DeviceRegistry::addJoystick(SDL_JoystickID id) {
    if (InputDevice* existing = find(DeviceKind::Joystick, id)) return existing;
    sdl::JoystickPtr joy(SDL_OpenJoystick(id));
    if (!joy) {
        log::warn("input: cannot open joystick {}: {}", id, SDL_GetError());
        return nullptr;
    }
    auto d = std::make_unique<InputDevice>();
    d->kind = DeviceKind::Joystick;
    d->id = id;
    const char* name = SDL_GetJoystickName(joy.get());
    d->name = (name && *name) ? name : "joystick";
    d->guid = guidString(SDL_GetJoystickGUIDForID(id));
    const char* serial = SDL_GetJoystickSerial(joy.get());
    d->serial = serial ? serial : "";
    d->typeLabel = "joystick";
    d->rawButtons.assign(static_cast<std::size_t>(std::clamp(SDL_GetNumJoystickButtons(joy.get()), 0, 64)), false);
    d->rawAxes.assign(static_cast<std::size_t>(std::clamp(SDL_GetNumJoystickAxes(joy.get()), 0, 16)), 0.0f);
    d->joystick = std::move(joy);
    log::info("input: unmapped joystick connected: {} (id {}, guid {}, {} buttons); any button fires", d->name, id, d->guid,
              d->rawButtons.size());
    devices_.push_back(std::move(d));
    return devices_.back().get();
}

std::optional<int> DeviceRegistry::removeController(SDL_JoystickID id) {
    std::optional<int> station;
    std::erase_if(devices_, [&](const auto& d) {
        if (d->kind == DeviceKind::Keyboard || d->id != id) return false;
        log::info("input: controller disconnected: {} (id {})", d->name, id);
        if (d->station >= 0) {
            station = d->station;
            lost_.push_back({d->station, d->guid, d->serial, d->name});
        }
        return true;
    });
    return station;
}

void DeviceRegistry::assign(InputDevice& device, int station) {
    if (station >= 0) unassignStation(station);
    device.station = station;
    if (station >= 0) {
        forgetLost(station);
        log::info("input: {} assigned to station {}", device.name, station + 1);
    }
}

void DeviceRegistry::unassignStation(int station) {
    for (auto& d : devices_)
        if (d->kind != DeviceKind::Keyboard && d->station == station) d->station = -1;
}

InputDevice* DeviceRegistry::controllerForStation(int station) {
    for (auto& d : devices_)
        if (d->kind != DeviceKind::Keyboard && d->station == station) return d.get();
    return nullptr;
}

const InputDevice* DeviceRegistry::controllerForStation(int station) const {
    for (const auto& d : devices_)
        if (d->kind != DeviceKind::Keyboard && d->station == station) return d.get();
    return nullptr;
}

bool DeviceRegistry::stationHasController(int station) const {
    return std::any_of(devices_.begin(), devices_.end(),
                       [&](const auto& d) { return d->kind != DeviceKind::Keyboard && d->station == station; });
}

std::optional<int> DeviceRegistry::takeLostMatch(const InputDevice& device) {
    for (auto it = lost_.begin(); it != lost_.end(); ++it) {
        const bool serialOk = it->serial.empty() || device.serial.empty() || it->serial == device.serial;
        if (it->guid == device.guid && serialOk && !stationHasController(it->station)) {
            const int station = it->station;
            lost_.erase(it);
            return station;
        }
    }
    return std::nullopt;
}

void DeviceRegistry::forgetLost(int station) {
    std::erase_if(lost_, [&](const LostDevice& l) { return l.station == station; });
}

} // namespace spindle::input
