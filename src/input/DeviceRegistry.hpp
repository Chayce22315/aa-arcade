#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "engine/SdlHandles.hpp"

namespace spindle::input {

enum class DeviceKind { Keyboard, Gamepad, Joystick };

struct InputDevice {
    DeviceKind kind = DeviceKind::Keyboard;
    std::uint32_t id = 0;   // SDL_KeyboardID or SDL_JoystickID
    std::string name;
    std::string guid;       // controllers only
    std::string serial;     // controllers only, may be empty
    std::string typeLabel;  // "xbox", "playstation", "switch", "gamepad", "joystick", "keyboard"
    int station = -1;       // controller assigned to a station (0..3), or -1
    sdl::GamepadPtr gamepad;
    sdl::JoystickPtr joystick;
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> buttons{};
    std::array<float, SDL_GAMEPAD_AXIS_COUNT> axes{};
    std::vector<bool> rawButtons;
    std::vector<float> rawAxes;
    std::uint8_t hat = 0;
};

// A controller that disappeared while assigned to a station, remembered so the same controller can
// re-attach to that station automatically.
struct LostDevice {
    int station = -1;
    std::string guid;
    std::string serial;
    std::string name;
};

class DeviceRegistry {
public:
    InputDevice* find(DeviceKind kind, std::uint32_t id);
    const std::vector<std::unique_ptr<InputDevice>>& devices() const { return devices_; }

    InputDevice* addKeyboard(SDL_KeyboardID id);
    void removeKeyboard(SDL_KeyboardID id);
    InputDevice* addGamepad(SDL_JoystickID id);
    InputDevice* addJoystick(SDL_JoystickID id);
    // Removes a controller; returns the station it was assigned to (if any) and remembers it as lost.
    std::optional<int> removeController(SDL_JoystickID id);

    // Assigns a controller to a station; any other controller on that station is unassigned.
    void assign(InputDevice& device, int station);
    void unassignStation(int station);
    InputDevice* controllerForStation(int station);
    const InputDevice* controllerForStation(int station) const;
    bool stationHasController(int station) const;

    // Matches a newly added controller against lost ones (same GUID, and same serial when known).
    std::optional<int> takeLostMatch(const InputDevice& device);
    void forgetLost(int station);
    const std::vector<LostDevice>& lost() const { return lost_; }

private:
    std::vector<std::unique_ptr<InputDevice>> devices_;
    std::vector<LostDevice> lost_;
};

std::string guidString(SDL_GUID guid);
std::string gamepadTypeLabel(SDL_GamepadType type);

} // namespace spindle::input
