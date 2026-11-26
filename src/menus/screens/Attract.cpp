// Attract pages (title, how to play, high scores) and the insert-coin screen.

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

#include "engine/Branding.hpp"
#include "menus/screens/Common.hpp"

namespace spindle::menus::screens {

void drawTitle(const ScreenContext& c) {
    const float cx = midX(c);
    decorativeCircle(c, {cx, areaY(c, 640)}, 1.0f, c.time);
    const std::string title(kTitle);
    textAt(c, title, cx, areaY(c, 170), 120, colors::kWhite);
    textAt(c, kTagline, cx, areaY(c, 340), 22, colors::kDim);
    if (blink(c.time)) textAt(c, startPrompt(c), cx, areaY(c, 900), 40, colors::kWhite);
}

void drawHowTo(const ScreenContext& c) {
    const float cx = midX(c);
    textAt(c, "how to play", cx, areaY(c, 120), 40, colors::kWhite);
    const char* const lines[] = {
        c.touchUi ? "tap your side of the screen to launch a needle" : "press fire to launch a needle into your circle",
        "needles must never touch each other",
        "every round all players get the same circle",
        "clear the round to keep your tries",
        "a touch or the timer costs one try",
        "out of tries means out of the match",
        "the last player standing wins",
    };
    float y = 190;
    for (const char* l : lines) {
        textAt(c, l, cx, areaY(c, y), 22, colors::kWhite);
        y += 50;
    }
    // Powerup legend (only the rings the operator enabled; heat only with the fire meter).
    const auto& gp = c.arcade.settings().gameplay;
    auto kinds = gp.powerups ? gameplay::parsePowerupList(gp.powerupTypes) : std::vector<gameplay::PowerupKind>{};
    if (!gp.fireMeter) std::erase(kinds, gameplay::PowerupKind::Heat);
    if (!kinds.empty()) {
        textAt(c, "land a needle on a ring to take its power", cx, areaY(c, 566), 20, colors::kWhite);
        const float spacing = std::min(230.0f, 1800.0f / static_cast<float>(kinds.size()));
        float x = cx - px(c, spacing) * 0.5f * static_cast<float>(kinds.size() - 1);
        for (gameplay::PowerupKind k : kinds) {
            drawPowerupIcon(c.r, k, {x, areaY(c, 632)}, px(c, 15), colors::kWhite);
            textAt(c, powerupLabel(k), x, areaY(c, 660), 17, colors::kWhite);
            textAt(c, powerupBlurb(k), x, areaY(c, 686), 13, colors::kDim);
            x += px(c, spacing);
        }
    }
    // The fire meter: the flame fills with the combo; full means on fire.
    if (gp.fireMeter) {
        constexpr std::string_view kFireLine = "quick needles fill your flame   close calls count double   full = on fire, needles burn through";
        const float fx = cx - (px(c, 30) + render::Renderer2D::textWidth(kFireLine, px(c, 17))) * 0.5f + px(c, 14);
        const float fill = static_cast<float>(0.5 + 0.5 * std::sin(c.time * 1.6));
        if (c.flame) {
            c.r.sprite(c.flame, {fx, areaY(c, 742)}, px(c, 28), px(c, 56), 0.0f, Color{0.5f, 0.5f, 0.5f, 0.42f});
            c.r.sprite(c.flame, {fx, areaY(c, 770) - px(c, 56) * fill * 0.5f}, px(c, 28), px(c, 56) * fill, 0.0f, colors::kWhite,
                       Rect{0.0f, 1.0f - fill, 1.0f, fill});
        }
        c.r.text(kFireLine, {fx + px(c, 30), areaY(c, 734)}, px(c, 17), kFire);
    }
    // A small animated example: a needle flying into a turning circle.
    const Vec2 centre{cx, areaY(c, 860)};
    const double phase = std::fmod(c.time, 2.0);
    const float scale = 0.42f;
    decorativeCircle(c, centre, scale, c.time * 0.6);
    const float dist = static_cast<float>(300.0 - std::min(phase, 1.0) * 150.0) * scale;
    if (phase < 1.0) c.r.fillCircle({centre.x, centre.y + px(c, dist)}, px(c, 11) * scale, colors::kWhite);
    if (blink(c.time)) textAt(c, startPrompt(c), cx, areaY(c, 990), 30, colors::kWhite);
}

namespace {

// One table: rank, name, score, round reached. topRef is in reference pixels from the top of the area.
void drawTable(const ScreenContext& c, const arcade::HighScoreTable& t, float centreX, float topRef, std::string_view title,
               std::string_view empty) {
    textAt(c, title, centreX, areaY(c, topRef), 26, colors::kWhite);
    if (t.empty()) {
        textAt(c, empty, centreX, areaY(c, topRef + 90.0f), 18, colors::kDim);
        return;
    }
    float y = topRef + 70.0f;
    for (std::size_t i = 0; i < t.rows().size(); ++i) {
        const auto& row = t.rows()[i];
        const Color col = i == 0 ? kGold : colors::kWhite;
        const float yy = areaY(c, y);
        c.r.text(std::format("{}", i + 1), {centreX - px(c, 300), yy}, px(c, 22), colors::kDim, Align::Right);
        c.r.text(row.name, {centreX - px(c, 270), yy}, px(c, 24), col, Align::Left);
        c.r.text(std::to_string(row.score), {centreX + px(c, 170), yy}, px(c, 24), col, Align::Right);
        c.r.text(std::format("r{}", row.round), {centreX + px(c, 300), yy}, px(c, 16), colors::kDim, Align::Right);
        y += 58.0f;
    }
}

} // namespace

void drawHighScores(const ScreenContext& c) {
    const float cx = midX(c);
    const auto& hs = c.arcade.highScores();
    const std::string_view table = c.arcade.attractTable();
    textAt(c, "high scores", cx, areaY(c, 90), 48, colors::kWhite);
    textAt(c, table, cx, areaY(c, 168), 20, colors::kDim);
    const auto& t = hs.tables(table);
    drawTable(c, t.allTime, cx - px(c, 430), 240.0f, "all time", "no scores yet");
    drawTable(c, t.today, cx + px(c, 430), 240.0f, "today", "no scores yet today");
    if (blink(c.time)) textAt(c, startPrompt(c), cx, areaY(c, 960), 34, colors::kWhite);
}

void drawInsertCoin(const ScreenContext& c) {
    const float cx = midX(c);
    const auto& credits = c.arcade.credits();
    textAt(c, credits.freePlay() ? std::string("FREE PLAY") : std::format("CREDIT {}", credits.credits()), cx, areaY(c, 300), 80,
           colors::kWhite);
    if (credits.canJoin()) {
        if (blink(c.time)) textAt(c, startPrompt(c), cx, areaY(c, 520), 44, colors::kWhite);
        if (!credits.freePlay())
            textAt(c, std::format("{} credit{} per player", credits.config().creditsPerGame, credits.config().creditsPerGame > 1 ? "s" : ""),
                   cx, areaY(c, 620), 20, colors::kDim);
    } else if (blink(c.time)) {
        textAt(c, "INSERT COIN", cx, areaY(c, 520), 44, colors::kWhite);
    }
}

} // namespace spindle::menus::screens
