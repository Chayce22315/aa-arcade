#include "input/TouchZones.hpp"

#include <algorithm>
#include <cmath>

namespace spindle::input {

bool inServiceCorner(float nx, float ny) { return nx >= 0.0f && ny >= 0.0f && nx < kServiceCornerWidth && ny < kServiceCornerHeight; }

bool inButtonCorner(float nx, float ny) { return nx <= 1.0f && ny >= 0.0f && nx > 1.0f - kButtonCornerWidth && ny < kButtonCornerHeight; }

int columnAt(const TouchContext& c, float nx) {
    for (int s = 0; s < 3; ++s)
        if (nx < c.columnEdges[static_cast<std::size_t>(s) + 1]) return s;
    return 3;
}

namespace {

TouchHit thirds(float t, int station) {
    if (t < 1.0f / 3.0f) return TouchHit{Action::Left, station};
    if (t > 2.0f / 3.0f) return TouchHit{Action::Right, station};
    return TouchHit{Action::Fire, station};
}

float withinColumn(const TouchContext& c, int s, float nx) {
    const float x0 = c.columnEdges[static_cast<std::size_t>(s)], x1 = c.columnEdges[static_cast<std::size_t>(s) + 1];
    return x1 > x0 ? (nx - x0) / (x1 - x0) : 0.5f;
}

} // namespace

std::optional<TouchHit> touchHit(const TouchContext& c, float nx, float ny) {
    if (!std::isfinite(nx) || !std::isfinite(ny) || nx < 0.0f || ny < 0.0f || nx > 1.0f || ny > 1.0f) return std::nullopt;
    if (inServiceCorner(nx, ny)) return std::nullopt;
    if (inButtonCorner(nx, ny)) {
        if (c.screen == TouchScreen::Lobby && c.gear) return TouchHit{Action::Options, -1};
        if (c.screen == TouchScreen::Playing && c.pause) return TouchHit{Action::Pause, -1};
    }
    switch (c.screen) {
    case TouchScreen::Ignore: return std::nullopt;
    case TouchScreen::Lobby: {
        const int s = columnAt(c, nx);
        if (c.voting[static_cast<std::size_t>(s)]) return thirds(withinColumn(c, s, nx), s);
        return TouchHit{Action::Start, s};
    }
    case TouchScreen::Playing: {
        const int humans = static_cast<int>(std::count(c.humans.begin(), c.humans.end(), true));
        const int entering = static_cast<int>(std::count(c.entering.begin(), c.entering.end(), true));
        if (entering == 1 && humans == 0) {
            const auto it = std::find(c.entering.begin(), c.entering.end(), true);
            return thirds(nx, static_cast<int>(it - c.entering.begin()));
        }
        if (entering > 0) {
            const int s = columnAt(c, nx);
            if (c.entering[static_cast<std::size_t>(s)]) return thirds(withinColumn(c, s, nx), s);
        }
        if (humans == 1) {
            // One player (usually a phone against bots): the whole screen is their fire button.
            const auto it = std::find(c.humans.begin(), c.humans.end(), true);
            return TouchHit{Action::Fire, static_cast<int>(it - c.humans.begin())};
        }
        const int s = columnAt(c, nx);
        if (!c.humans[static_cast<std::size_t>(s)]) return std::nullopt; // a bot's column
        return TouchHit{Action::Fire, s};
    }
    }
    return std::nullopt;
}

} // namespace spindle::input
