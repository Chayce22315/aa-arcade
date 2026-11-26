// The vote screen: players join in their columns and vote for one of the game mode cards; a tie spins
// the wheel; then the chosen mode's card.

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <string>

#include "menus/screens/Common.hpp"

namespace spindle::menus::screens {
namespace {

constexpr float kCardW = 560.0f, kCardH = 150.0f, kGap = 30.0f, kGridTop = 120.0f;

Rect cardRect(const ScreenContext& c, int index) {
    const int col = index % 3, row = index / 3;
    const float gridW = 3.0f * kCardW + 2.0f * kGap;
    const float x0 = midX(c) - px(c, gridW) * 0.5f;
    return {x0 + px(c, (kCardW + kGap) * static_cast<float>(col)), areaY(c, kGridTop + (kCardH + kGap) * static_cast<float>(row)), px(c, kCardW),
            px(c, kCardH)};
}

void drawCards(const ScreenContext& c, const arcade::VoteBox& v) {
    const auto& modes = v.modes();
    for (int i = 0; i < static_cast<int>(modes.size()); ++i) {
        const Rect r = cardRect(c, i);
        bool pointed = false;
        for (int s = 0; s < 4; ++s) pointed = pointed || (v.joined(s) && v.cursor(s) == i);
        const int votes = v.votesFor(i);
        frame(c, r, px(c, pointed ? 3.0f : 1.5f), pointed ? colors::kWhite : colors::kOut);
        const gameplay::ModeId m = modes[static_cast<std::size_t>(i)];
        drawModeIcon(c, m, {r.x + px(c, 72), r.y + r.h * 0.5f}, px(c, 42), pointed ? colors::kWhite : colors::kDim);
        c.r.text(gameplay::modeName(m), {r.x + px(c, 140), r.y + px(c, 26)}, px(c, 34), pointed ? colors::kWhite : colors::kDim);
        const std::string_view blurb = gameplay::modeBlurb(m);
        c.r.text(blurb, {r.x + px(c, 140), r.y + px(c, 86)}, fitCap(blurb, r.w - px(c, 160), px(c, 15)), colors::kDim);
        // Vote pips along the bottom edge.
        for (int k = 0; k < votes; ++k) c.r.fillCircle({r.x + r.w - px(c, 24 + 26.0f * static_cast<float>(k)), r.y + r.h - px(c, 22)}, px(c, 8), kGold);
        // Cursor tags: P1..P4 along the top edge; filled once that player voted here.
        for (int s = 0; s < 4; ++s) {
            if (!v.joined(s) || v.cursor(s) != i) continue;
            const Vec2 at{r.x + r.w - px(c, 34 + 52.0f * static_cast<float>(3 - s)), r.y + px(c, 8)};
            const bool voted = v.vote(s) == i;
            const Rect tag{at.x - px(c, 22), at.y, px(c, 44), px(c, 28)};
            if (voted) c.r.fillRect(tag, colors::kWhite);
            else frame(c, tag, px(c, 2), colors::kWhite);
            c.r.text(std::format("P{}", s + 1), {at.x, at.y + px(c, 6)}, px(c, 16), voted ? colors::kBlack : colors::kWhite, Align::Center);
        }
    }
}

void drawColumnPanels(const ScreenContext& c, const arcade::VoteBox& v) {
    for (int s = 0; s < gameplay::kMaxStations; ++s) {
        const auto i = static_cast<std::size_t>(s);
        const Rect& col = c.layout.columns[i];
        const float x = col.x + col.w * 0.5f;
        const float top = areaY(c, 700);
        c.r.text(std::format("P{}", s + 1), {col.x + px(c, 40), top}, px(c, 30), v.joined(s) ? colors::kWhite : colors::kOut);
        if (!v.joined(s)) {
            if (c.arcade.needCoinFlash(s) > 0.0)
                textAt(c, "INSERT COIN", x, top + px(c, 110), 24, kMissRed);
            else if (blink(c.time))
                textAt(c, c.touchUi ? "TAP TO JOIN" : "PRESS START", x, top + px(c, 110), 24, colors::kWhite);
            continue;
        }
        const gameplay::ModeId pointed = v.modes()[static_cast<std::size_t>(v.cursor(s))];
        const float y = top + px(c, 100);
        textAt(c, gameplay::modeName(pointed), x, y, 30, colors::kWhite);
        const float w = render::Renderer2D::textWidth(gameplay::modeName(pointed), px(c, 30)) * 0.5f + px(c, 30);
        if (v.modes().size() > 1) {
            textAt(c, "<", x - w, y, 30, colors::kDim);
            textAt(c, ">", x + w, y, 30, colors::kDim);
        }
        if (const auto voted = v.vote(s)) {
            textAt(c, std::format("voted {}", gameplay::modeName(v.modes()[static_cast<std::size_t>(*voted)])), x, y + px(c, 60), 18, kGold);
        } else if (v.modes().size() > 1) {
            textAt(c, c.touchUi ? "tap the sides to pick, the middle to vote" : "left / right pick   fire votes", x, y + px(c, 60), 14,
                   colors::kDim);
        } else if (blink(c.time, 1.4)) {
            textAt(c, c.touchUi ? "tap again = go" : "start = go", x, y + px(c, 60), 16, colors::kDim);
        }
    }
}

void drawClock(const ScreenContext& c, double left, double total) {
    const Vec2 o{midX(c), areaY(c, 60)};
    const float r = px(c, 32);
    const float frac = static_cast<float>(std::clamp(left / std::max(1.0, total), 0.0, 1.0));
    c.r.ring(o, r, px(c, 2), colors::kFaint);
    const float start = -std::numbers::pi_v<float> * 0.5f;
    arc(c.r, o, r, start, start + kTau * frac, px(c, 5), left <= 5.0 ? kMissRed : colors::kWhite);
    textAt(c, std::format("{}", static_cast<int>(std::ceil(left))), o.x, o.y - px(c, 13), 26, colors::kWhite);
    textAt(c, "vote for a game", o.x - px(c, 300), o.y - px(c, 12), 22, colors::kDim);
    textAt(c, "most votes wins", o.x + px(c, 300), o.y - px(c, 12), 22, colors::kDim);
}

void drawWheel(const ScreenContext& c) {
    const auto& a = c.arcade;
    const auto& wheel = a.wheel();
    const int n = static_cast<int>(wheel.size());
    if (n <= 0) return;
    const double t = a.votePhaseTime();
    const float turn = static_cast<float>(arcade::wheelAngle(t, n, a.wheelResult()));
    const Vec2 o{midX(c), areaY(c, 480)};
    const float R = px(c, 330), core = px(c, 70);
    const float w = kTau / static_cast<float>(n);
    const int under = arcade::wheelSliceAt(static_cast<double>(turn), n);
    textAt(c, "tie! spinning the wheel", midX(c), areaY(c, 50), 34, colors::kWhite);
    c.r.ring(o, R, px(c, 3), colors::kWhite);
    for (int k = 0; k < n; ++k) {
        // Slice borders are needles; the slice under the needle glows.
        const float a0 = turn + w * static_cast<float>(k);
        const Vec2 rim{o.x + R * std::cos(a0), o.y + R * std::sin(a0)};
        c.r.line(o, rim, px(c, 2.5f), colors::kWhite);
        c.r.fillCircle(rim, px(c, 12), colors::kWhite);
        const float mid = a0 + w * 0.5f;
        const bool lit = k == under;
        if (lit) arc(c.r, o, R - px(c, 14), a0 + 0.02f, a0 + w - 0.02f, px(c, 16), kGold);
        const gameplay::ModeId m = a.vote().modes()[static_cast<std::size_t>(wheel[static_cast<std::size_t>(k)])];
        const Vec2 label{o.x + R * 0.64f * std::cos(mid), o.y + R * 0.64f * std::sin(mid)};
        drawModeIcon(c, m, {label.x, label.y - px(c, 26)}, px(c, 24), lit ? kGold : colors::kWhite);
        c.r.text(gameplay::modeName(m), {label.x, label.y + px(c, 8)}, px(c, 24), lit ? kGold : colors::kWhite, Align::Center);
    }
    c.r.fillCircle(o, core, colors::kWhite);
    // The needle comes up from below in the last moment and sticks in the winner.
    const double fly = arcade::VoteBox::kSpinSeconds - 0.45;
    const float launcher = o.y + R + px(c, 150);
    if (t < fly) {
        c.r.fillCircle({o.x, launcher}, px(c, 14), colors::kWhite);
    } else {
        const float u = easeOut(static_cast<float>((t - fly) / 0.45));
        const float y = launcher + (o.y + R - launcher) * u;
        c.r.line({o.x, y}, {o.x, std::min(launcher, y + px(c, 60))}, px(c, 2.5f), colors::kWhite);
        c.r.fillCircle({o.x, y}, px(c, 14), colors::kWhite);
    }
}

void drawIntro(const ScreenContext& c) {
    const gameplay::ModeId m = c.arcade.chosenMode();
    const float k = easeOut(static_cast<float>(std::min(1.0, c.arcade.votePhaseTime() / 0.35)));
    const float cx = midX(c);
    drawModeIcon(c, m, {cx, areaY(c, 330)}, px(c, 110) * (0.6f + 0.4f * k), colors::kWhite.withAlpha(k));
    textAt(c, gameplay::modeName(m), cx, areaY(c, 500), 110, colors::kWhite.withAlpha(k));
    textAt(c, gameplay::modeBlurb(m), cx, areaY(c, 660), 28, colors::kDim.withAlpha(k));
    if (blink(c.time, 0.5)) textAt(c, "get ready", cx, areaY(c, 780), 26, colors::kWhite.withAlpha(k));
}

} // namespace

void drawVote(const ScreenContext& c) {
    const auto& a = c.arcade;
    switch (a.votePhase()) {
    case arcade::VotePhase::Voting:
        drawClock(c, a.voteTimeLeft(), static_cast<double>(a.settings().arcade.voteSeconds));
        drawCards(c, a.vote());
        drawColumnPanels(c, a.vote());
        break;
    case arcade::VotePhase::Spin: drawWheel(c); break;
    case arcade::VotePhase::Intro: drawIntro(c); break;
    }
}

} // namespace spindle::menus::screens
