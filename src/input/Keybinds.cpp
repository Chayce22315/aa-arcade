#include "input/Keybinds.hpp"

#include <array>
#include <utility>

#include "config/IniDocument.hpp"
#include "config/Settings.hpp"

namespace spindle::input {

const char* actionName(Action a) {
    switch (a) {
    case Action::Fire: return "fire";
    case Action::Start: return "start";
    case Action::Left: return "left";
    case Action::Right: return "right";
    case Action::Coin: return "coin";
    case Action::ServiceCoin: return "service coin";
    case Action::Service: return "service";
    case Action::Exit: return "exit";
    case Action::MenuUp: return "menu up";
    case Action::MenuDown: return "menu down";
    case Action::MenuLeft: return "menu left";
    case Action::MenuRight: return "menu right";
    case Action::MenuSelect: return "menu select";
    case Action::MenuBack: return "menu back";
    case Action::Options: return "options";
    case Action::Pause: return "pause";
    case Action::Count: break;
    }
    return "?";
}

bool isStationAction(Action a) { return a == Action::Fire || a == Action::Start || a == Action::Left || a == Action::Right; }

std::string canonicalKeyName(std::string_view name) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 18> kAliases{{
        {"lctrl", "Left Ctrl"},   {"rctrl", "Right Ctrl"},   {"lshift", "Left Shift"}, {"rshift", "Right Shift"},
        {"lalt", "Left Alt"},     {"ralt", "Right Alt"},     {"lgui", "Left GUI"},     {"rgui", "Right GUI"},
        {"esc", "Escape"},        {"enter", "Return"},       {"del", "Delete"},        {"ins", "Insert"},
        {"pgup", "PageUp"},       {"pgdn", "PageDown"},      {"bksp", "Backspace"},    {"spacebar", "Space"},
        {"leftctrl", "Left Ctrl"}, {"rightctrl", "Right Ctrl"},
    }};
    const std::string_view t = config::trim(name);
    const std::string lower = config::toLowerAscii(t);
    for (const auto& [alias, real] : kAliases)
        if (lower == alias) return std::string(real);
    return std::string(t);
}

std::vector<std::string> keyNames(std::string_view list) {
    std::vector<std::string> out;
    for (const std::string& s : config::splitList(list)) out.push_back(canonicalKeyName(s));
    return out;
}

std::vector<std::string> buttonNames(std::string_view list) {
    std::vector<std::string> out;
    for (const std::string& s : config::splitList(list)) {
        std::string lower = config::toLowerAscii(s);
        if (lower == "none") return {};
        out.push_back(std::move(lower));
    }
    return out;
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& n : names) {
        if (!out.empty()) out += ", ";
        out += n;
    }
    return out;
}

} // namespace spindle::input
