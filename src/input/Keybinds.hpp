#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace spindle::input {

enum class Action {
    Fire,
    Start,
    Coin,
    ServiceCoin,
    Service,
    Exit,
    MenuUp,
    MenuDown,
    MenuLeft,
    MenuRight,
    MenuSelect,
    MenuBack,
    Count
};

const char* actionName(Action a);
bool isStationAction(Action a);

// Expands short aliases used in settings.ini (LCtrl, Esc, Enter, ...) to SDL scancode names
// ("Left Ctrl", "Escape", "Return", ...). Unknown names are returned trimmed and unchanged.
std::string canonicalKeyName(std::string_view name);

// Splits a comma list of key names and canonicalizes each entry.
std::vector<std::string> keyNames(std::string_view list);

// Splits a comma list of gamepad button names (lowercased); "none" yields an empty list.
std::vector<std::string> buttonNames(std::string_view list);

// Joins names back into a settings value ("Left Ctrl, Space").
std::string joinNames(const std::vector<std::string>& names);

} // namespace spindle::input
