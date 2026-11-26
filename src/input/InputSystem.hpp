#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "arcade/ArcadeMachine.hpp"
#include "config/Settings.hpp"
#include "input/DeviceRegistry.hpp"
#include "input/Keybinds.hpp"
#include "input/TouchZones.hpp"

namespace spindle::input {

inline constexpr int kActionCount = static_cast<int>(Action::Count);

// Everything input produced during one simulation tick.
struct TickInput {
    arcade::ArcadeInput arcade;
    std::bitset<kActionCount> systemPressed; // edges of non-station actions (and menu navigation)
    std::array<std::bitset<kActionCount>, 4> stationPressed{};
    std::vector<int> stationsLost;
    std::vector<int> stationsRestored;
    std::optional<SDL_Scancode> capturedKey;
    std::uint32_t capturedKeyboard = 0; // keyboard that sent capturedKey
    std::optional<std::uint32_t> capturedController; // joystick id of the controller whose button was captured
    std::vector<std::pair<float, float>> taps; // fingers that touched down (normalized window x, y), for touch menus
    bool serviceByTouch = false;               // the service press came from the hidden corner
};

// Turns SDL keyboard / gamepad / joystick events into per-station and system actions. Events are
// queued with their SDL timestamps and released to the simulation tick in which they happened.
class InputSystem {
public:
    InputSystem();

    // Rebuilds bindings from settings. Returns the number of unusable names (reported in the log).
    int configure(const config::Settings& settings);

    void handleEvent(const SDL_Event& e);

    // Collects queued input with timestamp <= untilNs (all queued input when useTimestamps is false).
    TickInput drain(std::uint64_t untilNs);
    void setUseTimestamps(bool use) { useTimestamps_ = use; }

    // Station chosen when an unassigned controller presses start (auto assignment); -1 = none.
    void setClaimPolicy(std::function<int()> policy) { claimPolicy_ = std::move(policy); }
    // What a tap means right now (lobby, playing, ignored); asked on every finger down.
    void setTouchContext(std::function<TouchContext()> context) { touchContext_ = std::move(context); }
    int fingersDown() const { return static_cast<int>(fingers_.size()); }

    void beginKeyCapture() { captureKey_ = true; }
    void beginControllerCapture() { captureController_ = true; }
    void cancelCapture() {
        captureKey_ = false;
        captureController_ = false;
    }
    bool capturing() const { return captureKey_ || captureController_; }

    bool systemHeld(Action a) const { return systemHeld_[static_cast<std::size_t>(a)]; }
    bool stationHeld(int station, Action a) const;
    const std::string& lastEventText() const { return lastEvent_; }

    DeviceRegistry& devices() { return registry_; }
    const DeviceRegistry& devices() const { return registry_; }
    void assignController(std::uint32_t joystickId, int station);
    void clearStation(int station);
    int stationKeyboard(int station) const { return stationKeyboard_[static_cast<std::size_t>(station)]; }

private:
    struct Queued {
        std::uint64_t timestamp = 0;
        Action action = Action::Fire;
        int station = -1; // -1 = system
        bool pressed = false;
    };
    struct KeyBinding {
        Action action;
        int station;
    };

    void push(std::uint64_t ts, Action a, int station, bool pressed);
    void onKey(const SDL_KeyboardEvent& k);
    void onGamepadButton(const SDL_GamepadButtonEvent& b);
    void onJoystickButton(const SDL_JoyButtonEvent& b);
    void onJoystickHat(const SDL_JoyHatEvent& h);
    void onStick(InputDevice& d, float x, std::uint64_t ts);
    void onFinger(const SDL_TouchFingerEvent& f, std::uint32_t type);
    void onControllerAdded(InputDevice* d);
    void onControllerStart(InputDevice& d, std::uint64_t ts);
    void resolveStationKeyboards();

    DeviceRegistry registry_;
    config::Settings settings_;
    std::array<std::vector<KeyBinding>, SDL_SCANCODE_COUNT> keys_{};
    std::vector<SDL_GamepadButton> fireButtons_, startButtons_, coinButtons_;
    std::array<int, 4> stationKeyboard_{-1, -1, -1, -1}; // keyboard id per station when per_keyboard = 1
    std::deque<Queued> queue_;
    std::vector<int> pendingLost_, pendingRestored_;
    std::optional<SDL_Scancode> pendingKey_;
    std::uint32_t pendingKeyboard_ = 0;
    std::optional<std::uint32_t> pendingController_;
    std::bitset<kActionCount> systemHeld_;
    std::array<std::bitset<kActionCount>, 4> stationHeld_{};
    std::array<std::bitset<kActionCount>, 4> tickHeld_{}; // station actions held as of the last drained tick
    std::function<int()> claimPolicy_;
    std::function<TouchContext()> touchContext_;
    struct Finger {
        SDL_FingerID id = 0;
        std::optional<TouchHit> hit; // the action it pressed, released when the finger lifts
    };
    std::vector<Finger> fingers_;
    std::optional<SDL_FingerID> cornerFinger_; // a finger holding the hidden service corner
    std::uint64_t cornerSinceNs_ = 0;
    bool cornerFired_ = false;
    std::vector<std::pair<float, float>> pendingTaps_;
    bool useTimestamps_ = true;
    bool captureKey_ = false;
    bool captureController_ = false;
    std::string lastEvent_;
};

} // namespace spindle::input
