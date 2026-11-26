// Joining: each station's column shows whether it is in, and the countdown to the start.

#include <cmath>
#include <format>

#include "menus/screens/Common.hpp"

namespace spindle::menus::screens {

void drawJoin(const ScreenContext& c) {
    const auto& joined = c.arcade.joined();
    for (int s = 0; s < gameplay::kMaxStations; ++s) {
        const auto i = static_cast<std::size_t>(s);
        const Rect& col = c.layout.columns[i];
        const float x = col.x + col.w * 0.5f;
        const Vec2 centre = c.layout.circleCenter[i];
        drawStationHeader(c, s, joined[i] ? "player" : "", joined[i] ? colors::kWhite : colors::kDim);
        if (joined[i]) {
            decorativeCircle(c, centre, 0.8f, c.time * 0.8 + s);
            textAt(c, "joined", x, col.y + px(c, 700), 30, colors::kWhite);
            if (blink(c.time, 1.4)) textAt(c, c.touchUi ? "tap again = go" : "start = go", x, col.y + px(c, 760), 16, colors::kDim);
        } else {
            c.r.ring(centre, px(c, 46), px(c, 2), colors::kDim);
            const bool needCoin = c.arcade.needCoinFlash(s) > 0.0;
            if (needCoin)
                textAt(c, "INSERT COIN", x, col.y + px(c, 700), 26, kMissRed);
            else if (blink(c.time))
                textAt(c, c.touchUi ? "TAP TO JOIN" : "PRESS START", x, col.y + px(c, 700), 26, colors::kWhite);
        }
    }
    const int left = static_cast<int>(std::ceil(c.arcade.joinTimeLeft()));
    textAt(c, std::format("starting in {}", left), midX(c), areaY(c, 20), 22, colors::kWhite);
}

} // namespace spindle::menus::screens
