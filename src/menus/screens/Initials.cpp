// A player shooting their initials into the high score table, in their own column: the next ammo
// ball shows the letter (left / right turn the wheel), fire shoots it into the circle.

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <string>

#include "menus/screens/Common.hpp"

namespace spindle::menus::screens {
namespace {

constexpr float kCore = 112.0f;
constexpr double kShotFlight = 0.16;

void chevron(const ScreenContext& c, Vec2 at, float size, bool left, Color col) {
    const float d = left ? 1.0f : -1.0f;
    const Vec2 pts[] = {{at.x + d * size * 0.5f, at.y - size}, {at.x - d * size * 0.5f, at.y}, {at.x + d * size * 0.5f, at.y + size}};
    c.r.polyline(pts, px(c, 4), col);
}

} // namespace

void drawInitialsColumn(const ScreenContext& c, int s) {
    const auto& slot = c.arcade.entry(s);
    if (!slot) return;
    const arcade::InitialsEntry& e = *slot;
    const auto i = static_cast<std::size_t>(s);
    const Rect& col = c.layout.columns[i];
    const Vec2 centre = c.layout.circleCenter[i];
    const float coreR = px(c, kCore);
    const float cx = col.x + col.w * 0.5f;

    // Where the score landed.
    const auto& hs = c.arcade.highScores();
    const auto all = hs.rankOf(c.arcade.scoreTable(), e.token(), false);
    const auto today = hs.rankOf(c.arcade.scoreTable(), e.token(), true);
    const std::string place = all ? std::format("#{} all time", *all + 1) : today ? std::format("#{} today", *today + 1) : std::string();
    if (e.done()) {
        drawStationStatus(c, s, "saved", kGold, 28);
    } else {
        drawStationStatus(c, s, "high score!", blink(c.time, 0.6) ? kGold : colors::kWhite, 28);
    }
    textAt(c, place, cx, col.y + px(c, 212), 18, colors::kDim);

    // The clock: a ring around the core that runs down.
    if (!e.done()) {
        const float frac = static_cast<float>(std::clamp(e.timeLeft() / e.seconds(), 0.0, 1.0));
        const float start = -std::numbers::pi_v<float> * 0.5f;
        c.r.ring(centre, coreR + px(c, 16), px(c, 2), colors::kFaint);
        arc(c.r, centre, coreR + px(c, 16), start, start + kTau * frac, px(c, 5), frac < 0.3f ? kMissRed : colors::kWhite);
        textAt(c, std::format("{}", static_cast<int>(std::ceil(e.timeLeft()))), cx, centre.y + coreR + px(c, 30), 22,
               frac < 0.3f ? kMissRed : colors::kDim);
    }

    // The core with the letters shot so far and the next slot.
    c.r.fillCircle(centre, coreR, colors::kWhite);
    const std::string shown = e.done() ? e.name() : e.typed();
    const float cap = fitCap("WWWWWW", coreR * 1.75f, px(c, 62));
    const float w = render::Renderer2D::textWidth(shown, cap);
    const float slotW = render::Renderer2D::textWidth("W", cap);
    const bool showCursor = !e.done() && shown.size() < arcade::kMaxNameLength;
    const float x0 = centre.x - (w + (showCursor ? slotW : 0.0f)) * 0.5f;
    if (!shown.empty()) c.r.text(shown, {x0, centre.y - cap * 0.5f}, cap, colors::kBlack, Align::Left, cap * 0.13f);
    if (showCursor && blink(c.time, 0.7))
        c.r.fillRect({x0 + w + slotW * 0.12f, centre.y + cap * 0.62f, slotW * 0.76f, px(c, 4)}, colors::kBlack);

    const float launcherY = centre.y + px(c, static_cast<float>(geom::kLaunchDistance));
    const float ball = static_cast<float>(geom::kBallRadius) * 2.0f;
    // The letter just shot, flying up into the circle.
    if (e.lastShot() >= 0 && e.sinceShot() < kShotFlight) {
        const float u = static_cast<float>(e.sinceShot() / kShotFlight);
        const float y = launcherY + (centre.y + coreR - launcherY) * u;
        drawLabelBall(c, {cx, y}, ball, arcade::InitialsEntry::symbolLabel(e.lastShot()), colors::kWhite);
    }
    if (e.done()) return;

    // The wheel: the next ammo ball at the launcher, its neighbours fading out to the sides.
    for (int d = -2; d <= 2; ++d) {
        if (d == 0) continue;
        const int sym = ((e.cursor() + d) % arcade::InitialsEntry::kSymbols + arcade::InitialsEntry::kSymbols) % arcade::InitialsEntry::kSymbols;
        const float x = cx + px(c, 54.0f) * static_cast<float>(d);
        const float a = std::abs(d) == 1 ? 0.45f : 0.2f;
        drawLabelBall(c, {x, launcherY + px(c, 6.0f) * static_cast<float>(std::abs(d))}, ball * 0.7f,
                      arcade::InitialsEntry::symbolLabel(sym), colors::kWhite.withAlpha(a));
    }
    if (e.sinceShot() >= kShotFlight * 0.5)
        drawLabelBall(c, {cx, launcherY}, ball, arcade::InitialsEntry::symbolLabel(e.cursor()), colors::kWhite);
    chevron(c, {cx - px(c, 170), launcherY}, px(c, 18), true, colors::kWhite);
    chevron(c, {cx + px(c, 170), launcherY}, px(c, 18), false, colors::kWhite);

    const char* hint = c.touchUi ? "tap the sides to pick   middle to shoot" : "left / right pick   fire shoots";
    textAt(c, hint, cx, col.y + px(c, 860), 14, colors::kDim);
    textAt(c, "shoot end when you are done", cx, col.y + px(c, 890), 13, colors::kDim);
}

} // namespace spindle::menus::screens
