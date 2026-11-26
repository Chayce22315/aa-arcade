// The round end screen ("half-time"): every circle lights up, ammo balls carrying the digits of the
// player's total fire into the core and type it in, then total / round / bonus lines, then the crown.

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <string>

#include "menus/screens/Common.hpp"

namespace spindle::menus::screens {
namespace {

constexpr float kBigCore = 112.0f;     // reference radius of the lit-up core
constexpr double kLinePop = 0.22;      // seconds a breakdown line takes to slide in

float lerp(float a, float b, float t) { return a + (b - a) * t; }

} // namespace

void drawTallyStation(const ScreenContext& c, const gameplay::Match& m, int s) {
    const auto& p = *m.participant(s);
    const gameplay::Playfield& f = m.field(s);
    const auto& t = m.tallyTiming();
    const double e = m.phaseTime();
    const auto i = static_cast<std::size_t>(s);
    const Vec2 centre = c.layout.circleCenter[i];
    const float baseCore = px(c, static_cast<float>(geom::kCoreRadius));
    const float bigCore = px(c, kBigCore);

    // 1. Lights: the core swells and glows, the needles fade away.
    const float grow = t.lights > 0.0 ? easeOut(static_cast<float>(e / t.lights)) : 1.0f;
    const float coreR = lerp(baseCore, bigCore, grow);
    const bool crowned = m.crownHolder() == s && e >= t.crownStart() + t.crown * 0.6;
    const float fade = 1.0f - grow;
    if (fade > 0.01f) {
        const double angle = m.rotor().state().angle;
        const float L = px(c, static_cast<float>(geom::kNeedleLength));
        for (const auto& a : f.attached()) {
            const float ang = static_cast<float>(a.angle + angle);
            const Vec2 q{centre.x + L * std::cos(ang), centre.y + L * std::sin(ang)};
            c.r.line(centre, q, px(c, 2.2f), colors::kWhite.withAlpha(fade));
            c.r.fillCircle(q, px(c, static_cast<float>(geom::kBallRadius)), colors::kWhite.withAlpha(fade));
        }
    }
    for (int k = 0; k < 4; ++k) {
        const float alpha = (0.34f - 0.08f * static_cast<float>(k)) * grow;
        const Color glow = crowned ? hsv(static_cast<float>(k) * 0.2f - static_cast<float>(c.time) * 0.4f, 0.7f, 1.0f, alpha)
                                   : colors::kWhite.withAlpha(alpha);
        c.r.ring(centre, coreR + px(c, 10.0f + 13.0f * static_cast<float>(k)), px(c, 3.0f), glow);
    }
    c.r.fillCircle(centre, coreR, colors::kWhite);
    if (crowned) rainbowRim(c, centre, coreR - px(c, 5), px(c, 11));

    // 2. The ammo: one ball per digit of the new total, biggest digit first.
    const std::string total = std::to_string(p.score);
    const int n = static_cast<int>(total.size());
    const double slot = t.perDigit;
    int landed = n, launched = n;
    if (slot > 0.0) {
        landed = 0;
        launched = 0;
        for (int k = 0; k < n; ++k) {
            if (e >= t.digitLaunch(k)) ++launched;
            if (e >= t.digitLand(k)) ++landed;
        }
    }
    const float cap = fitCap(total, coreR * 1.55f, px(c, 70.0f));
    const float fullW = render::Renderer2D::textWidth(total, cap);
    const float x0 = centre.x - fullW * 0.5f;
    if (landed > 0)
        c.r.text(std::string_view(total).substr(0, static_cast<std::size_t>(landed)), {x0, centre.y - cap * 0.5f}, cap,
                 colors::kBlack, Align::Left, cap * 0.13f);
    // Empty slots wait as small dots.
    for (int k = landed; k < n; ++k) {
        const float sx = x0 + render::Renderer2D::textWidth(std::string_view(total).substr(0, static_cast<std::size_t>(k)), cap) +
                         render::Renderer2D::textWidth(std::string_view(total).substr(static_cast<std::size_t>(k), 1), cap) * 0.5f;
        c.r.fillCircle({sx, centre.y + cap * 0.42f}, px(c, 3.0f), colors::kBlack.withAlpha(0.25f));
    }
    // A pop ring each time a digit lands.
    if (slot > 0.0 && landed > 0) {
        const double since = e - t.digitLand(landed - 1);
        if (since >= 0.0 && since < 0.2) {
            const float k = static_cast<float>(since / 0.2);
            c.r.ring(centre, coreR + px(c, 4.0f + 30.0f * k), px(c, 3.0f), colors::kWhite.withAlpha(1.0f - k));
        }
    }
    const float launcherY = centre.y + px(c, static_cast<float>(geom::kLaunchDistance));
    const float ball = static_cast<float>(geom::kBallRadius) * 1.35f;
    for (int k = landed; k < launched; ++k) { // in flight
        const float u = static_cast<float>((e - t.digitLaunch(k)) / (slot * gameplay::scoring::TallyTiming::kFlight));
        const float y = lerp(launcherY, centre.y + coreR, std::clamp(u, 0.0f, 1.0f));
        drawLabelBall(c, {centre.x, y}, ball, std::string_view(total).substr(static_cast<std::size_t>(k), 1), colors::kWhite);
    }
    for (int k = launched; k < n; ++k) { // still queued below the launcher
        const float y = launcherY + px(c, static_cast<float>(geom::kQueueSpacing) * 1.3f * static_cast<float>(k - launched));
        drawLabelBall(c, {centre.x, y}, ball, std::string_view(total).substr(static_cast<std::size_t>(k), 1),
                      colors::kWhite.withAlpha(k == launched ? 1.0f : 0.75f));
    }

    // Status line: how the round ended.
    if (f.status() == gameplay::FieldStatus::Cleared)
        drawStationStatus(c, s, "clear", colors::kWhite);
    else
        drawStationStatus(c, s, f.failReason() == gameplay::FailReason::Timeout ? "time" : "miss", kMissRed);

    // 3. The breakdown: total, round points, bonus.
    if (e >= t.breakdownStart()) {
        const double step = t.breakdown / 3.0;
        const std::string lines[3] = {std::format("total  {}", p.score), std::format("round  +{}", p.roundPoints),
                                      std::format("bonus  +{}", p.roundBonus)};
        const Color cols[3] = {colors::kWhite, colors::kWhite, p.roundBonus > 0 ? kFireHot : colors::kDim};
        for (int k = 0; k < 3; ++k) {
            const double since = e - (t.breakdownStart() + step * k);
            if (since < 0.0) break;
            const float a = easeOut(static_cast<float>(std::min(1.0, since / kLinePop)));
            const float y = centre.y + px(c, 168.0f + 44.0f * static_cast<float>(k)) + px(c, 18.0f) * (1.0f - a);
            c.r.text(lines[k], {centre.x, y}, px(c, k == 0 ? 26.0f : 22.0f), cols[k].withAlpha(a), Align::Center);
        }
    }
}

void drawCrownLayer(const ScreenContext& c, const gameplay::Match& m) {
    const int holder = m.crownHolder();
    const float size = px(c, 40);
    if (m.phase() != gameplay::MatchPhase::Tally) {
        if (holder >= 0) drawCrown(c, crownSpot(c, holder), size, kGold);
        return;
    }
    const auto& t = m.tallyTiming();
    const double e = m.phaseTime();
    const int before = m.crownBefore();
    if (e < t.crownStart() || t.crown <= 0.0) {
        if (before >= 0) drawCrown(c, crownSpot(c, before), size, kGold);
        return;
    }
    const float k = static_cast<float>(std::clamp((e - t.crownStart()) / t.crown, 0.0, 1.0));
    if (holder == before) {
        if (holder >= 0) drawCrown(c, crownSpot(c, holder), size * (1.0f + 0.15f * std::sin(k * std::numbers::pi_v<float>)), kGold);
        return;
    }
    if (holder < 0) {
        // The holder was knocked out: the crown tumbles off and fades.
        const Vec2 from = crownSpot(c, before);
        drawCrown(c, {from.x, from.y + px(c, 160) * k * k}, size, kGold.withAlpha(1.0f - k));
        return;
    }
    const Vec2 to = crownSpot(c, holder);
    const Vec2 from = before >= 0 ? crownSpot(c, before) : Vec2{to.x, to.y - px(c, 200)};
    const float u = easeInOut(k);
    const Vec2 pos{lerp(from.x, to.x, u), lerp(from.y, to.y, u) - std::sin(u * std::numbers::pi_v<float>) * px(c, 110)};
    drawCrown(c, pos, size * (1.0f + 0.6f * std::sin(u * std::numbers::pi_v<float>)), kGold);
    if (k > 0.8f) {
        const float g = (k - 0.8f) / 0.2f;
        c.r.ring(to, px(c, 20.0f + 50.0f * g), px(c, 2.5f), kGold.withAlpha(1.0f - g));
        textAt(c, "crown!", c.layout.columns[static_cast<std::size_t>(holder)].x + c.layout.columns[static_cast<std::size_t>(holder)].w * 0.5f,
               c.layout.columns[static_cast<std::size_t>(holder)].y + px(c, 118), 20, kGold.withAlpha(g));
    }
}

} // namespace spindle::menus::screens
