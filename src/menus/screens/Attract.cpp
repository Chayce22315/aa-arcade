// Attract pages (title, how to play, high scores) and the insert-coin screen.

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <vector>

#include "engine/Branding.hpp"
#include "menus/screens/Common.hpp"

namespace spindle::menus::screens {

void drawTitle(const ScreenContext& c) {
    const float cx = midX(c);
    decorativeCircle(c, {cx, areaY(c, 640)}, 1.0f, c.time);
    // The letters arrive one by one, each pinned by a needle shot up from below.
    const std::string title(kTitle);
    const float cap = capPx(c, 120);
    const float width = render::Renderer2D::textWidth(title, cap);
    const float x0 = cx - width * 0.5f, top = areaY(c, 170);
    const double t = c.arcade.pageTime();
    constexpr double kFirst = 0.25, kStep = 0.14, kFlight = 0.22;
    for (std::size_t i = 0; i < title.size(); ++i) {
        const float lx = x0 + render::Renderer2D::textWidth(std::string_view(title).substr(0, i), cap);
        const float lw = render::Renderer2D::textWidth(std::string_view(title).substr(i, 1), cap);
        const float mid = lx + lw * 0.5f;
        const double hit = kFirst + kStep * static_cast<double>(i);
        if (t >= hit) {
            c.r.text(std::string_view(title).substr(i, 1), {lx, top}, cap, colors::kWhite, Align::Left);
            const float pop = static_cast<float>(std::clamp((t - hit) / 0.2, 0.0, 1.0));
            if (pop < 1.0f) c.r.ring({mid, top + cap * 0.5f}, cap * (0.3f + 0.4f * pop), px(c, 2), colors::kWhite.withAlpha(1.0f - pop));
        } else if (t >= hit - kFlight) {
            const float u = static_cast<float>((t - (hit - kFlight)) / kFlight);
            const float y = areaY(c, 900) + (top + cap - areaY(c, 900)) * u;
            c.r.fillCircle({mid, y}, px(c, 11), colors::kWhite);
        }
    }
    textAt(c, kTagline, cx, areaY(c, 372), 22, colors::kDim);
    textAt(c, std::format("v{}", kVersion), cx, areaY(c, 1010), 14, colors::kFaint);
    if (blink(c.time)) textAt(c, startPrompt(c), cx, areaY(c, 900), 40, colors::kWhite);
}

// Once after an update: what this version brought, one line each, popping in one after another.
void drawWhatsNew(const ScreenContext& c) {
    const float cx = midX(c);
    textAt(c, "what's new", cx, areaY(c, 110), 48, colors::kWhite);
    textAt(c, std::format("v{}", c.arcade.whatsNewVersion()), cx, areaY(c, 182), 20, kGold);
    const auto& lines = c.arcade.whatsNew();
    const double t = c.arcade.pageTime();
    float y = 270.0f;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const double at = 0.3 + 0.25 * static_cast<double>(i);
        if (t < at) break;
        const float k = static_cast<float>(std::clamp((t - at) / 0.25, 0.0, 1.0));
        const float cap = fitCap(lines[i], px(c, 1500), capPx(c, 22));
        c.r.fillCircle({cx - px(c, 790), areaY(c, y) + cap * 0.5f}, px(c, 6), kGold.withAlpha(k));
        c.r.text(lines[i], {cx - px(c, 760), areaY(c, y)}, cap, colors::kWhite.withAlpha(k), Align::Left);
        y += 66.0f;
    }
    if (blink(c.time)) textAt(c, startPrompt(c), cx, areaY(c, 960), 32, colors::kWhite);
}

namespace {

// One how-to card: a small looping animation over a short line of text.
void howToCard(const ScreenContext& c, float x, std::string_view title, std::string_view line, int kind) {
    const Vec2 o{x, areaY(c, 380)};
    const float scale = 0.55f;
    const double loop = std::fmod(c.time, 2.4);
    textAt(c, title, x, areaY(c, 170), 30, colors::kWhite);
    switch (kind) {
    case 0: { // fire a needle: it flies up and sticks
        decorativeCircle(c, o, scale, c.time * 0.7);
        const float u = static_cast<float>(std::min(loop / 0.6, 1.0));
        const float d = px(c, (300.0f - 150.0f * u) * scale);
        if (loop < 0.6) c.r.fillCircle({o.x, o.y + d}, px(c, 11) * scale, colors::kWhite);
        break;
    }
    case 1: { // take a ring: a needle lands on it and it pops
        decorativeCircle(c, o, scale, 0.4);
        const Vec2 ring{o.x, o.y + px(c, 150) * scale};
        const float k = static_cast<float>(std::clamp((loop - 0.6) / 0.4, 0.0, 1.0));
        if (loop < 0.6) {
            drawPowerupIcon(c.r, gameplay::PowerupKind::Shield, ring, px(c, 15) * scale * 1.6f, colors::kWhite);
            c.r.fillCircle({o.x, o.y + px(c, (300.0f - 150.0f * static_cast<float>(loop / 0.6)) * scale)}, px(c, 11) * scale, colors::kWhite);
        } else if (k < 1.0f) {
            c.r.ring(ring, px(c, 15) * scale * (1.6f + 2.0f * k), px(c, 2), kGold.withAlpha(1.0f - k));
        }
        break;
    }
    default: { // catch fire: the flame fills, then the circle burns
        const float fill = static_cast<float>(std::min(loop / 1.4, 1.0));
        decorativeCircle(c, o, scale, c.time * 0.7);
        if (fill >= 1.0f)
            for (int i = 0; i < 12; ++i) {
                const float a = kTau * static_cast<float>(i) / 12.0f + static_cast<float>(c.time);
                const float r = px(c, 165) * scale;
                flame(c, {o.x + r * std::cos(a), o.y + r * std::sin(a)}, px(c, 30) * scale, px(c, 58) * scale, a + std::numbers::pi_v<float> * 0.5f,
                      colors::kWhite);
            }
        if (c.flame) {
            const Vec2 m{o.x + px(c, 150), o.y + px(c, 60)};
            c.r.sprite(c.flame, {m.x, m.y}, px(c, 30), px(c, 60), 0.0f, Color{0.5f, 0.5f, 0.5f, 0.42f});
            c.r.sprite(c.flame, {m.x, m.y + px(c, 30) - px(c, 30) * fill}, px(c, 30), px(c, 60) * fill, 0.0f, colors::kWhite, Rect{0.0f, 1.0f - fill, 1.0f, fill});
        }
        break;
    }
    }
    textAt(c, line, x, areaY(c, 560), 18, colors::kDim);
}

} // namespace

void drawHowTo(const ScreenContext& c) {
    const float cx = midX(c);
    textAt(c, "how to play", cx, areaY(c, 70), 44, colors::kWhite);
    const auto& gp = c.arcade.settings().gameplay;
    howToCard(c, cx - px(c, 600), "fire a needle", c.touchUi ? "tap your column - needles must never touch" : "press fire - needles must never touch", 0);
    howToCard(c, cx, "take a ring", "land on a ring to take its power", 1);
    if (gp.fireMeter) howToCard(c, cx + px(c, 600), "catch fire", "quick needles fill the flame - full = on fire", 2);
    textAt(c, "every round, everyone gets the same circle", cx, areaY(c, 650), 22, colors::kWhite);
    textAt(c, "vote for a game when you join - battle: a touch or the timer costs a try, the last one standing wins", cx, areaY(c, 700), 17,
           colors::kDim);
    // Powerup legend (only the rings the operator enabled; heat only with the fire meter).
    auto kinds = gp.powerups ? gameplay::parsePowerupList(gp.powerupTypes) : std::vector<gameplay::PowerupKind>{};
    if (!gp.fireMeter) std::erase(kinds, gameplay::PowerupKind::Heat);
    if (!kinds.empty()) {
        const float spacing = std::min(230.0f, 1800.0f / static_cast<float>(kinds.size()));
        float x = cx - px(c, spacing) * 0.5f * static_cast<float>(kinds.size() - 1);
        for (gameplay::PowerupKind k : kinds) {
            drawPowerupIcon(c.r, k, {x, areaY(c, 790)}, px(c, 16), colors::kWhite);
            textAt(c, powerupLabel(k), x, areaY(c, 820), 18, colors::kWhite);
            const std::string_view blurb = powerupBlurb(k);
            c.r.text(blurb, {x, areaY(c, 850)}, fitCap(blurb, px(c, spacing) * 0.92f, capPx(c, 15)), colors::kDim, Align::Center);
            x += px(c, spacing);
        }
    }
    if (blink(c.time)) textAt(c, startPrompt(c), cx, areaY(c, 960), 32, colors::kWhite);
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

// The cabinet's lifetime numbers and its achievements (earned in gold, the rest dim).
void drawStats(const ScreenContext& c) {
    const float cx = midX(c);
    const auto& p = c.arcade.progress();
    const auto& st = p.stats();
    textAt(c, "this machine", cx, areaY(c, 90), 48, colors::kWhite);
    textAt(c, std::format("{} of {} achievements", p.earnedCount(), arcade::kAchievementCount), cx, areaY(c, 168), 20, colors::kDim);
    const std::pair<const char*, std::string> rows[] = {
        {"games played", std::to_string(st.matches)},     {"needles placed", std::to_string(st.needles)},
        {"rounds cleared", std::to_string(st.roundsCleared)}, {"close calls", std::to_string(st.closeCalls)},
        {"times on fire", std::to_string(st.fires)},      {"crowns taken", std::to_string(st.crowns)},
        {"best score", std::to_string(st.bestScore)},     {"best round", std::to_string(st.bestRound)},
    };
    float y = 250.0f;
    const float left = cx - px(c, 820), mid = cx - px(c, 260);
    for (const auto& [label, value] : rows) {
        c.r.text(label, {left, areaY(c, y)}, capPx(c, 22), colors::kDim, Align::Left);
        c.r.text(value, {mid, areaY(c, y)}, capPx(c, 24), colors::kWhite, Align::Right);
        y += 62.0f;
    }
    // Achievements: two columns of names on the right.
    for (std::size_t i = 0; i < arcade::kAchievementCount; ++i) {
        const auto a = static_cast<arcade::Achievement>(i);
        const bool got = p.earned(a);
        const float x = cx + px(c, i < 7 ? -120.0f : 400.0f);
        const float yy = areaY(c, 250.0f + 62.0f * static_cast<float>(i % 7));
        const Color col = got ? kGold : colors::kFaint;
        if (got)
            c.r.fillCircle({x, yy + capPx(c, 20) * 0.5f}, px(c, 7), col);
        else
            c.r.ring({x, yy + capPx(c, 20) * 0.5f}, px(c, 7), px(c, 1.5f), col);
        c.r.text(arcade::achievementInfo(a).name, {x + px(c, 24), yy}, capPx(c, 20), got ? colors::kWhite : colors::kFaint, Align::Left);
    }
    if (blink(c.time)) textAt(c, startPrompt(c), cx, areaY(c, 960), 34, colors::kWhite);
}

// An achievement a player just earned: a gold card low in their column, under the circle and its queue
// (the header, the status line and the crown stay readable).
void drawAchievementToasts(const ScreenContext& c) {
    for (int s = 0; s < gameplay::kMaxStations; ++s) {
        const auto& t = c.arcade.toast(s);
        if (!t) continue;
        const Rect& col = c.layout.columns[static_cast<std::size_t>(s)];
        const double shown = arcade::ArcadeMachine::kToastSeconds - t->timeLeft;
        const float in = static_cast<float>(std::clamp(shown / 0.25, 0.0, 1.0));
        const float out = static_cast<float>(std::clamp(t->timeLeft / 0.4, 0.0, 1.0));
        const float a = std::min(in, out);
        const auto info = arcade::achievementInfo(t->achievement);
        const std::string name(info.name);
        const std::string unlock = info.unlocks ? std::format("unlocked: {}", arcade::lookName(*info.unlocks)) : std::string();
        const float w = col.w - px(c, 40), pad = px(c, 10);
        const float labelCap = capPx(c, 14), nameCap = fitCap(name, w - px(c, 24), capPx(c, 24));
        const float unlockCap = unlock.empty() ? 0.0f : fitCap(unlock, w - px(c, 24), capPx(c, 15));
        const float h = pad + labelCap * 1.6f + nameCap * 1.4f + (unlock.empty() ? 0.0f : unlockCap * 1.6f) + pad;
        const float x = col.x + col.w * 0.5f, y = col.y + px(c, 780) + px(c, 24) * (1.0f - in);
        c.r.fillRect({x - w * 0.5f, y, w, h}, Color{0.0f, 0.0f, 0.0f, 0.88f * a});
        frame(c, {x - w * 0.5f, y, w, h}, px(c, 2), kGold.withAlpha(a));
        float ty = y + pad;
        c.r.text("achievement", {x, ty}, labelCap, kGold.withAlpha(a), Align::Center);
        ty += labelCap * 1.6f;
        c.r.text(name, {x, ty}, nameCap, colors::kWhite.withAlpha(a), Align::Center);
        ty += nameCap * 1.4f;
        if (!unlock.empty()) c.r.text(unlock, {x, ty}, unlockCap, kGold.withAlpha(a), Align::Center);
    }
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
