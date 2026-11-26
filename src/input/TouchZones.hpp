#pragma once

#include <array>
#include <optional>

#include "input/Keybinds.hpp"

// Touch controls for phones and tablets (pure; see docs/RULES.md "Touch"). Positions are normalized
// to the window: x and y in 0..1, origin top-left.
namespace spindle::input {

enum class TouchScreen {
    Lobby,   // attract, insert coin, vote, game over: a tap is that column's start (or vote controls)
    Playing, // a tap fires for the human playing that column (anywhere, when only one human plays)
    Ignore   // the service menu handles its own taps
};

struct TouchContext {
    TouchScreen screen = TouchScreen::Lobby;
    std::array<bool, 4> humans{};      // stations still playing the match, played by humans (Playing)
    std::array<bool, 4> entering{};    // stations shooting their initials (Playing): left / shoot / right thirds
    std::array<bool, 4> voting{};      // joined stations on the vote screen (Lobby): left / vote / right thirds
    std::array<float, 5> columnEdges{0.0f, 0.25f, 0.5f, 0.75f, 1.0f}; // normalized x of the column borders
};

struct TouchHit {
    Action action = Action::Fire;
    int station = 0;
};

// The hidden operator corner (top-left): holding a finger there for kServiceHoldSeconds opens the
// service menu. Taps there do nothing else, so a player cannot open it by accident.
inline constexpr float kServiceCornerWidth = 0.07f;
inline constexpr float kServiceCornerHeight = 0.12f;
inline constexpr double kServiceHoldSeconds = 3.0;
bool inServiceCorner(float nx, float ny);

// The station column under nx (left of the first column counts as the first, right of the last as the last).
int columnAt(const TouchContext& context, float nx);

// What a finger touching down at (nx, ny) does; nothing in the corner, on a bot's column, or while ignored.
// A column shooting initials splits into thirds: left = previous letter, middle = shoot, right = next
// letter. When the only human left is entering initials, the whole screen splits that way.
std::optional<TouchHit> touchHit(const TouchContext& context, float nx, float ny);

} // namespace spindle::input
