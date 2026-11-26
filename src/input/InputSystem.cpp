#include "input/InputSystem.hpp"

#include <algorithm>
#include <format>

#include "engine/Log.hpp"

namespace spindle::input {
namespace {

std::vector<SDL_GamepadButton> parseButtons(const std::string& list, const std::string& fallback, const char* what, int& issues) {
    std::vector<SDL_GamepadButton> out;
    for (const std::string& name : buttonNames(list)) {
        const SDL_GamepadButton b = SDL_GetGamepadButtonFromString(name.c_str());
        if (b == SDL_GAMEPAD_BUTTON_INVALID) {
            log::warn("input: unknown gamepad button '{}' in {}", name, what);
            ++issues;
            continue;
        }
        out.push_back(b);
    }
    if (out.empty() && !buttonNames(list).empty() && !fallback.empty()) return parseButtons(fallback, "", what, issues);
    return out;
}

bool contains(const std::vector<SDL_GamepadButton>& v, SDL_GamepadButton b) { return std::find(v.begin(), v.end(), b) != v.end(); }

} // namespace

InputSystem::InputSystem() { settings_ = config::Settings::defaults(); }

int InputSystem::configure(const config::Settings& settings) {
    settings_ = settings;
    for (auto& k : keys_) k.clear();
    int issues = 0;
    const config::Settings defaults = config::Settings::defaults();
    const auto bind = [&](const std::string& list, const std::string& fallback, Action a, int station, const std::string& what) {
        int bound = 0;
        for (const std::string& name : keyNames(list)) {
            const SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
            if (sc == SDL_SCANCODE_UNKNOWN) {
                log::warn("input: unknown key '{}' for {}", name, what);
                ++issues;
                continue;
            }
            keys_[static_cast<std::size_t>(sc)].push_back({a, station});
            ++bound;
        }
        if (bound == 0 && fallback != list) {
            log::warn("input: {} has no usable key; using default '{}'", what, fallback);
            for (const std::string& name : keyNames(fallback)) {
                const SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
                if (sc != SDL_SCANCODE_UNKNOWN) keys_[static_cast<std::size_t>(sc)].push_back({a, station});
            }
        }
    };
    for (int s = 0; s < 4; ++s) {
        const auto i = static_cast<std::size_t>(s);
        bind(settings.stationKeys[i].fire, defaults.stationKeys[i].fire, Action::Fire, s, std::format("player {} fire", s + 1));
        bind(settings.stationKeys[i].start, defaults.stationKeys[i].start, Action::Start, s, std::format("player {} start", s + 1));
        bind(settings.stationKeys[i].left, defaults.stationKeys[i].left, Action::Left, s, std::format("player {} left", s + 1));
        bind(settings.stationKeys[i].right, defaults.stationKeys[i].right, Action::Right, s, std::format("player {} right", s + 1));
    }
    const auto& k = settings.systemKeys;
    const auto& dk = defaults.systemKeys;
    bind(k.coin, dk.coin, Action::Coin, -1, "coin");
    bind(k.serviceCoin, dk.serviceCoin, Action::ServiceCoin, -1, "service coin");
    bind(k.service, dk.service, Action::Service, -1, "service");
    bind(k.exit, dk.exit, Action::Exit, -1, "exit");
    bind(k.menuUp, dk.menuUp, Action::MenuUp, -1, "menu up");
    bind(k.menuDown, dk.menuDown, Action::MenuDown, -1, "menu down");
    bind(k.menuLeft, dk.menuLeft, Action::MenuLeft, -1, "menu left");
    bind(k.menuRight, dk.menuRight, Action::MenuRight, -1, "menu right");
    bind(k.menuSelect, dk.menuSelect, Action::MenuSelect, -1, "menu select");
    bind(k.menuBack, dk.menuBack, Action::MenuBack, -1, "menu back");

    fireButtons_ = parseButtons(settings.input.gamepadFire, defaults.input.gamepadFire, "gamepad_fire", issues);
    startButtons_ = parseButtons(settings.input.gamepadStart, defaults.input.gamepadStart, "gamepad_start", issues);
    coinButtons_ = parseButtons(settings.input.gamepadCoin, "", "gamepad_coin", issues);

    SDL_SetHint(SDL_HINT_WINDOWS_RAW_KEYBOARD, settings.input.perKeyboard ? "1" : "0");
    int count = 0;
    if (SDL_KeyboardID* ids = SDL_GetKeyboards(&count)) {
        for (int i = 0; i < count; ++i) registry_.addKeyboard(ids[i]);
        SDL_free(ids);
    }
    resolveStationKeyboards();

    // Fixed assignment: controllers already connected take their configured stations.
    if (settings.input.assignment == config::AssignmentMode::Fixed)
        for (const auto& d : registry_.devices())
            if (d->kind != DeviceKind::Keyboard) onControllerAdded(d.get());
    return issues;
}

void InputSystem::resolveStationKeyboards() {
    stationKeyboard_.fill(-1);
    if (!settings_.input.perKeyboard) return;
    for (int s = 0; s < 4; ++s) {
        const std::string& want = settings_.input.keyboardName[static_cast<std::size_t>(s)];
        if (want.empty()) continue;
        for (const auto& d : registry_.devices()) {
            if (d->kind == DeviceKind::Keyboard && d->name == want) {
                stationKeyboard_[static_cast<std::size_t>(s)] = static_cast<int>(d->id);
                break;
            }
        }
        if (stationKeyboard_[static_cast<std::size_t>(s)] < 0)
            log::warn("input: keyboard '{}' for station {} is not connected; any keyboard drives it", want, s + 1);
    }
}

bool InputSystem::stationHeld(int station, Action a) const {
    if (station < 0 || station >= 4) return false;
    return stationHeld_[static_cast<std::size_t>(station)][static_cast<std::size_t>(a)];
}

void InputSystem::push(std::uint64_t ts, Action a, int station, bool pressed) {
    queue_.push_back({ts, a, station, pressed});
    if (station >= 0)
        stationHeld_[static_cast<std::size_t>(station)][static_cast<std::size_t>(a)] = pressed;
    else
        systemHeld_[static_cast<std::size_t>(a)] = pressed;
}

void InputSystem::onKey(const SDL_KeyboardEvent& k) {
    if (k.scancode >= SDL_SCANCODE_COUNT) return;
    if (k.down && !k.repeat) lastEvent_ = std::format("key {} (keyboard {})", SDL_GetScancodeName(k.scancode), k.which);
    if (captureKey_ && k.down && !k.repeat) {
        captureKey_ = false;
        pendingKey_ = k.scancode;
        pendingKeyboard_ = k.which;
        return;
    }
    for (const KeyBinding& b : keys_[static_cast<std::size_t>(k.scancode)]) {
        if (b.station >= 0) {
            const int want = stationKeyboard_[static_cast<std::size_t>(b.station)];
            if (want >= 0 && static_cast<int>(k.which) != want) continue;
            if (k.repeat) continue;
        } else if (k.repeat) {
            const bool nav = b.action == Action::MenuUp || b.action == Action::MenuDown || b.action == Action::MenuLeft ||
                             b.action == Action::MenuRight;
            if (!nav) continue;
        }
        push(k.timestamp, b.action, b.station, k.down);
    }
}

void InputSystem::onControllerStart(InputDevice& d, std::uint64_t ts) {
    if (d.station >= 0) {
        push(ts, Action::Start, d.station, true);
        return;
    }
    if (settings_.input.assignment != config::AssignmentMode::Auto || !claimPolicy_) return;
    const int target = claimPolicy_();
    if (target < 0) return;
    const bool wasLost = std::any_of(registry_.lost().begin(), registry_.lost().end(), [&](const LostDevice& l) { return l.station == target; });
    registry_.assign(d, target);
    if (wasLost) pendingRestored_.push_back(target);
    push(ts, Action::Start, target, true);
}

void InputSystem::onGamepadButton(const SDL_GamepadButtonEvent& b) {
    InputDevice* d = registry_.find(DeviceKind::Gamepad, b.which);
    if (!d) return;
    const auto button = static_cast<SDL_GamepadButton>(b.button);
    if (b.button < SDL_GAMEPAD_BUTTON_COUNT) d->buttons[b.button] = b.down;
    if (b.down) lastEvent_ = std::format("{} button {} ({})", d->typeLabel, SDL_GetGamepadStringForButton(button), d->name);
    if (captureController_ && b.down) {
        captureController_ = false;
        pendingController_ = d->id;
        return;
    }
    // Menu navigation works from any controller.
    Action nav = Action::Count;
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: nav = Action::MenuUp; break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: nav = Action::MenuDown; break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: nav = Action::MenuLeft; break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: nav = Action::MenuRight; break;
    case SDL_GAMEPAD_BUTTON_SOUTH: nav = Action::MenuSelect; break;
    case SDL_GAMEPAD_BUTTON_EAST: nav = Action::MenuBack; break;
    default: break;
    }
    if (nav != Action::Count) push(b.timestamp, nav, -1, b.down);
    // The d-pad also picks letters and modes for the pad's own station.
    if (d->station >= 0 && button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) push(b.timestamp, Action::Left, d->station, b.down);
    if (d->station >= 0 && button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) push(b.timestamp, Action::Right, d->station, b.down);
    if (contains(coinButtons_, button)) push(b.timestamp, Action::Coin, -1, b.down);
    if (contains(startButtons_, button)) {
        if (b.down)
            onControllerStart(*d, b.timestamp);
        else if (d->station >= 0)
            push(b.timestamp, Action::Start, d->station, false);
    }
    if (contains(fireButtons_, button) && d->station >= 0) push(b.timestamp, Action::Fire, d->station, b.down);
}

void InputSystem::onJoystickButton(const SDL_JoyButtonEvent& b) {
    InputDevice* d = registry_.find(DeviceKind::Joystick, b.which);
    if (!d) return;
    if (b.button < d->rawButtons.size()) d->rawButtons[b.button] = b.down;
    if (b.down) lastEvent_ = std::format("joystick button {} ({})", b.button, d->name);
    if (captureController_ && b.down) {
        captureController_ = false;
        pendingController_ = d->id;
        return;
    }
    // Unmapped fallback: buttons 0-3 fire (and select/back in menus), any other button is start.
    if (b.button == 0) push(b.timestamp, Action::MenuSelect, -1, b.down);
    if (b.button == 1) push(b.timestamp, Action::MenuBack, -1, b.down);
    if (b.button < 4) {
        if (d->station >= 0) {
            push(b.timestamp, Action::Fire, d->station, b.down);
        } else if (b.down) {
            onControllerStart(*d, b.timestamp); // an unassigned stick claims a station with any button
        }
    } else if (b.down) {
        onControllerStart(*d, b.timestamp);
    } else if (d->station >= 0) {
        push(b.timestamp, Action::Start, d->station, false);
    }
}

void InputSystem::onJoystickHat(const SDL_JoyHatEvent& h) {
    InputDevice* d = registry_.find(DeviceKind::Joystick, h.which);
    if (!d) return;
    const std::uint8_t before = d->hat;
    d->hat = h.value;
    const auto edge = [&](std::uint8_t mask, Action a) {
        const bool was = (before & mask) != 0, now = (h.value & mask) != 0;
        if (was != now) push(h.timestamp, a, -1, now);
    };
    edge(SDL_HAT_UP, Action::MenuUp);
    edge(SDL_HAT_DOWN, Action::MenuDown);
    edge(SDL_HAT_LEFT, Action::MenuLeft);
    edge(SDL_HAT_RIGHT, Action::MenuRight);
    if (d->station >= 0) {
        const auto stationEdge = [&](std::uint8_t mask, Action a) {
            const bool was = (before & mask) != 0, now = (h.value & mask) != 0;
            if (was != now) push(h.timestamp, a, d->station, now);
        };
        stationEdge(SDL_HAT_LEFT, Action::Left);
        stationEdge(SDL_HAT_RIGHT, Action::Right);
    }
}

void InputSystem::onStick(InputDevice& d, float x, std::uint64_t ts) {
    // Digital left / right from an analogue axis: press past 0.5, release inside 0.3.
    constexpr float kPress = 0.5f, kRelease = 0.3f;
    int dir = d.stickDir;
    if (dir == 0 && x <= -kPress) dir = -1;
    else if (dir == 0 && x >= kPress) dir = 1;
    else if (dir != 0 && x > -kRelease && x < kRelease) dir = 0;
    else if (dir == -1 && x >= kPress) dir = 1;
    else if (dir == 1 && x <= -kPress) dir = -1;
    if (dir == d.stickDir) return;
    if (d.station >= 0) {
        if (d.stickDir == -1) push(ts, Action::Left, d.station, false);
        if (d.stickDir == 1) push(ts, Action::Right, d.station, false);
        if (dir == -1) push(ts, Action::Left, d.station, true);
        if (dir == 1) push(ts, Action::Right, d.station, true);
    }
    d.stickDir = dir;
}

void InputSystem::onControllerAdded(InputDevice* d) {
    if (!d) return;
    if (auto station = registry_.takeLostMatch(*d)) {
        registry_.assign(*d, *station);
        pendingRestored_.push_back(*station);
        return;
    }
    if (settings_.input.assignment == config::AssignmentMode::Fixed && d->station < 0) {
        for (int s = 0; s < 4; ++s) {
            if (settings_.input.gamepadGuid[static_cast<std::size_t>(s)] == d->guid && !registry_.stationHasController(s)) {
                registry_.assign(*d, s);
                break;
            }
        }
    }
}

void InputSystem::assignController(std::uint32_t joystickId, int station) {
    InputDevice* d = registry_.find(DeviceKind::Gamepad, joystickId);
    if (!d) d = registry_.find(DeviceKind::Joystick, joystickId);
    if (!d) return;
    const bool wasLost = std::any_of(registry_.lost().begin(), registry_.lost().end(), [&](const LostDevice& l) { return l.station == station; });
    registry_.assign(*d, station);
    if (wasLost) pendingRestored_.push_back(station);
}

void InputSystem::clearStation(int station) {
    registry_.unassignStation(station);
    registry_.forgetLost(station);
}

void InputSystem::handleEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: onKey(e.key); break;
    case SDL_EVENT_KEYBOARD_ADDED:
        registry_.addKeyboard(e.kdevice.which);
        resolveStationKeyboards();
        break;
    case SDL_EVENT_KEYBOARD_REMOVED:
        registry_.removeKeyboard(e.kdevice.which);
        resolveStationKeyboards();
        break;
    case SDL_EVENT_GAMEPAD_ADDED: onControllerAdded(registry_.addGamepad(e.gdevice.which)); break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (auto s = registry_.removeController(e.gdevice.which)) pendingLost_.push_back(*s);
        break;
    case SDL_EVENT_JOYSTICK_ADDED:
        if (!SDL_IsGamepad(e.jdevice.which)) onControllerAdded(registry_.addJoystick(e.jdevice.which));
        break;
    case SDL_EVENT_JOYSTICK_REMOVED:
        if (registry_.find(DeviceKind::Joystick, e.jdevice.which))
            if (auto s = registry_.removeController(e.jdevice.which)) pendingLost_.push_back(*s);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: onGamepadButton(e.gbutton); break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (InputDevice* d = registry_.find(DeviceKind::Gamepad, e.gaxis.which); d && e.gaxis.axis < SDL_GAMEPAD_AXIS_COUNT) {
            d->axes[e.gaxis.axis] = static_cast<float>(e.gaxis.value) / 32767.0f;
            if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) onStick(*d, d->axes[e.gaxis.axis], e.gaxis.timestamp);
        }
        break;
    case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
    case SDL_EVENT_JOYSTICK_BUTTON_UP: onJoystickButton(e.jbutton); break;
    case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        if (InputDevice* d = registry_.find(DeviceKind::Joystick, e.jaxis.which); d && e.jaxis.axis < d->rawAxes.size()) {
            d->rawAxes[e.jaxis.axis] = static_cast<float>(e.jaxis.value) / 32767.0f;
            if (e.jaxis.axis == 0) onStick(*d, d->rawAxes[0], e.jaxis.timestamp);
        }
        break;
    case SDL_EVENT_JOYSTICK_HAT_MOTION: onJoystickHat(e.jhat); break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_CANCELED: onFinger(e.tfinger, e.type); break;
    default: break;
    }
}

void InputSystem::onFinger(const SDL_TouchFingerEvent& f, std::uint32_t type) {
    const auto it = std::find_if(fingers_.begin(), fingers_.end(), [&](const Finger& g) { return g.id == f.fingerID; });
    if (type == SDL_EVENT_FINGER_DOWN) {
        lastEvent_ = std::format("touch {:.2f}, {:.2f}", static_cast<double>(f.x), static_cast<double>(f.y));
        pendingTaps_.emplace_back(f.x, f.y);
        if (inServiceCorner(f.x, f.y) && !cornerFinger_) {
            cornerFinger_ = f.fingerID;
            cornerSinceNs_ = f.timestamp;
            cornerFired_ = false;
        }
        Finger finger{f.fingerID, std::nullopt};
        if (touchContext_) finger.hit = touchHit(touchContext_(), f.x, f.y);
        if (finger.hit) push(f.timestamp, finger.hit->action, finger.hit->station, true);
        if (it != fingers_.end())
            *it = finger;
        else
            fingers_.push_back(finger);
        return;
    }
    if (type == SDL_EVENT_FINGER_MOTION) {
        // Sliding out of the corner cancels the hidden hold.
        if (cornerFinger_ && *cornerFinger_ == f.fingerID && !inServiceCorner(f.x, f.y)) cornerFinger_.reset();
        return;
    }
    // Up or cancelled.
    if (cornerFinger_ && *cornerFinger_ == f.fingerID) cornerFinger_.reset();
    if (it == fingers_.end()) return;
    if (it->hit) push(f.timestamp, it->hit->action, it->hit->station, false);
    fingers_.erase(it);
}

TickInput InputSystem::drain(std::uint64_t untilNs) {
    TickInput t;
    // The hidden operator corner: one service press once a finger has held it long enough.
    if (cornerFinger_ && !cornerFired_) {
        const std::uint64_t now = useTimestamps_ && untilNs > 0 ? untilNs : SDL_GetTicksNS();
        if (now >= cornerSinceNs_ && now - cornerSinceNs_ >= static_cast<std::uint64_t>(kServiceHoldSeconds * 1e9)) {
            cornerFired_ = true;
            t.systemPressed[static_cast<std::size_t>(Action::Service)] = true;
            t.serviceByTouch = true;
            log::info("input: service menu opened from the hidden corner");
        }
    }
    t.taps.swap(pendingTaps_);
    while (!queue_.empty() && (!useTimestamps_ || queue_.front().timestamp <= untilNs)) {
        const Queued q = queue_.front();
        queue_.pop_front();
        if (q.station >= 0) tickHeld_[static_cast<std::size_t>(q.station)][static_cast<std::size_t>(q.action)] = q.pressed;
        if (!q.pressed) continue;
        if (q.station >= 0) {
            auto& st = t.arcade.stations[static_cast<std::size_t>(q.station)];
            if (q.action == Action::Fire) ++st.firePresses;
            if (q.action == Action::Start) st.startPressed = true;
            if (q.action == Action::Left) ++st.leftPresses;
            if (q.action == Action::Right) ++st.rightPresses;
            t.stationPressed[static_cast<std::size_t>(q.station)][static_cast<std::size_t>(q.action)] = true;
        } else {
            if (q.action == Action::Coin) ++t.arcade.coins;
            if (q.action == Action::ServiceCoin) ++t.arcade.serviceCredits;
            t.systemPressed[static_cast<std::size_t>(q.action)] = true;
        }
    }
    for (std::size_t s = 0; s < 4; ++s) {
        t.arcade.stations[s].leftHeld = tickHeld_[s][static_cast<std::size_t>(Action::Left)];
        t.arcade.stations[s].rightHeld = tickHeld_[s][static_cast<std::size_t>(Action::Right)];
    }
    t.stationsLost.swap(pendingLost_);
    t.stationsRestored.swap(pendingRestored_);
    t.capturedKey = pendingKey_;
    t.capturedKeyboard = pendingKeyboard_;
    t.capturedController = pendingController_;
    pendingKey_.reset();
    pendingController_.reset();
    return t;
}

} // namespace spindle::input
