// The end of a match: initials for whoever made a high score table, then the results; plus the
// paused / disconnected overlays drawn over a match.

#include <algorithm>
#include <array>
#include <format>
#include <string>

#include "arcade/DailyCode.hpp"
#include "menus/screens/Common.hpp"

namespace spindle::menus::screens {

void drawInitialsState(const ScreenContext& c) {
    const gameplay::Match* m = c.arcade.match();
    const auto& lineup = c.arcade.lineup();
    for (int s = 0; s < gameplay::kMaxStations; ++s) {
        const auto i = static_cast<std::size_t>(s);
        const auto* p = m ? &m->participant(s) : nullptr;
        if (!p || !*p) {
            drawStationHeader(c, s, "", colors::kFaint);
            continue;
        }
        const bool bot = lineup.seats[i] == players::SeatKind::Bot;
        drawStationHeader(c, s, bot ? "cpu" : "player", colors::kWhite);
        if (c.arcade.entry(s)) {
            drawInitialsColumn(c, s);
            continue;
        }
        // Everyone else: their final total, waiting quietly.
        const Vec2 centre = c.layout.circleCenter[i];
        const bool winner = m->winnerStation() == s;
        c.r.fillCircle(centre, px(c, 70), winner ? colors::kWhite : colors::kOut);
        const std::string score = std::to_string((*p)->score);
        const float cap = fitCap(score, px(c, 112), px(c, 36));
        c.r.text(score, {centre.x, centre.y - cap * 0.5f}, cap, colors::kBlack, Align::Center, cap * 0.13f);
        if (winner) drawStationStatus(c, s, "winner", colors::kWhite, 26);
    }
    if (m && m->crownHolder() >= 0) drawCrown(c, crownSpot(c, m->crownHolder()), px(c, 40), kGold);
    textAt(c, "new high score", midX(c), areaY(c, 22), 22, kGold);
}

namespace {

const char* placeName(int rank) {
    switch (rank) {
    case 1: return "1st";
    case 2: return "2nd";
    case 3: return "3rd";
    default: return "4th";
    }
}

// The match's awards: each goes to everyone sharing the best value (above zero).
std::array<std::string, gameplay::kMaxStations> awards(const gameplay::Match& m) {
    std::array<std::string, gameplay::kMaxStations> out{};
    struct Award {
        const char* name;
        int (*value)(const gameplay::Participant&);
    };
    const Award list[] = {
        {"crown king", [](const gameplay::Participant& p) { return p.crownsWon; }},
        {"best combo", [](const gameplay::Participant& p) { return p.bestCombo; }},
        {"close shaves", [](const gameplay::Participant& p) { return p.closeCalls; }},
        {"hot hands", [](const gameplay::Participant& p) { return p.ignitions; }},
    };
    for (const Award& a : list) {
        int best = 0;
        for (int s = 0; s < gameplay::kMaxStations; ++s)
            if (const auto& p = m.participant(s)) best = std::max(best, a.value(*p));
        if (best <= 0) continue;
        for (int s = 0; s < gameplay::kMaxStations; ++s) {
            const auto& p = m.participant(s);
            auto& slot = out[static_cast<std::size_t>(s)];
            if (p && slot.empty() && a.value(*p) == best) slot = std::format("{} {}", a.name, best);
        }
    }
    return out;
}

} // namespace

// Results: one card per player in their own column, with place, score and an award.
void drawGameOver(const ScreenContext& c) {
    const auto& res = c.arcade.lastResult();
    const gameplay::Match* m = c.arcade.match();
    scrim(c, 1.0f);
    const float cx = midX(c);
    for (int k = 1; k < 4; ++k) { // dividers below the title
        const Rect& col = c.layout.columns[static_cast<std::size_t>(k)];
        c.r.line({col.x, areaY(c, 170)}, {col.x, col.y + col.h - px(c, 30)}, 1.0f, colors::kFaint);
    }
    textAt(c, "game over", cx, areaY(c, 34), 46, colors::kWhite);
    if (!res || !m) return;
    std::string headline;
    if (res->winningTeam)
        headline = std::format("team {} wins  -  {}", *res->winningTeam + 1, gameplay::modeName(res->gameMode));
    else if (res->winner >= 0)
        headline = std::format("P{} wins  -  {}", res->winner + 1, gameplay::modeName(res->gameMode));
    else
        headline = std::format("{}  -  rounds cleared {}", gameplay::modeName(res->gameMode),
                               res->standings.empty() ? 0 : res->roundsCleared[static_cast<std::size_t>(res->standings.front())]);
    textAt(c, headline, cx, areaY(c, 100), 22, colors::kDim);
    const auto prizes = awards(*m);
    const auto end = gameplay::modeRules(res->gameMode).end;
    for (std::size_t rank = 0; rank < res->standings.size(); ++rank) {
        const int s = res->standings[rank];
        const auto i = static_cast<std::size_t>(s);
        const Rect& col = c.layout.columns[i];
        const float x = col.x + col.w * 0.5f;
        const bool bot = res->lineup.seats[i] == players::SeatKind::Bot;
        const bool first = rank == 0 && res->winner >= 0;
        const Color hi = first ? kGold : colors::kWhite;
        textAt(c, std::format("P{}", s + 1), x, col.y + px(c, 196), 30, stationColor(c, s));
        const bots::BotController* brain = bot ? c.arcade.bot(s) : nullptr;
        textAt(c, bot ? (brain ? brain->brain().name() : std::string("cpu")) : std::string("player"), x, col.y + px(c, 236), 16, colors::kDim);
        textAt(c, placeName(static_cast<int>(rank) + 1), x, col.y + px(c, 280), 64, hi);
        if (first) frame(c, {col.x + px(c, 24), col.y + px(c, 176), col.w - px(c, 48), px(c, 480)}, px(c, 2), kGold.withAlpha(blink(c.time, 0.8) ? 1.0f : 0.5f));
        textAt(c, std::to_string(res->scores[i]), x, col.y + px(c, 390), 46, colors::kWhite);
        textAt(c, "points", x, col.y + px(c, 448), 16, colors::kDim);
        const std::string extra = end == gameplay::MatchEnd::FirstTo ? std::format("rounds won {}", res->points[i])
                                  : end == gameplay::MatchEnd::Rounds ? std::format("crown points {}", res->points[i])
                                                                      : std::format("rounds cleared {}", res->roundsCleared[i]);
        textAt(c, extra, x, col.y + px(c, 510), 18, colors::kWhite);
        if (!prizes[i].empty()) textAt(c, prizes[i], x, col.y + px(c, 590), 20, kGold);
        // Daily: a code to share the score (a typo check, not proof).
        if (res->gameMode == gameplay::ModeId::Daily && !bot)
            if (const auto code = arcade::dailyCode(res->date, res->scores[i])) {
                textAt(c, "daily code", x, col.y + px(c, 650), 14, colors::kDim);
                const std::string shown = arcade::formatDailyCode(*code);
                c.r.text(shown, {x, col.y + px(c, 676)}, fitCap(shown, col.w - px(c, 30), capPx(c, 20)), colors::kWhite, Align::Center);
            }
    }
}

void drawSuspendedOverlay(const ScreenContext& c) {
    scrim(c, 0.86f);
    textAt(c, "paused", midX(c), areaY(c, 430), 60, colors::kWhite);
    if (blink(c.time)) textAt(c, c.touchUi ? "tap to continue" : "press start or fire to continue", midX(c), areaY(c, 540), 26, colors::kWhite);
}

void drawDisconnectOverlay(const ScreenContext& c) {
    scrim(c, 0.88f);
    float y = 380;
    for (int s = 0; s < gameplay::kMaxStations; ++s) {
        if (!c.arcade.stationWaitingForReconnect(s)) continue;
        textAt(c, std::format("P{} controller disconnected", s + 1), midX(c), areaY(c, y), 40, colors::kWhite);
        y += 80;
        textAt(c, std::format("reconnect it, press start on a free controller, or press P{} start on the keyboard", s + 1), midX(c),
               areaY(c, y), 18, colors::kDim);
        y += 70;
    }
    if (blink(c.time)) textAt(c, "paused", midX(c), areaY(c, 260), 26, colors::kWhite);
}

} // namespace spindle::menus::screens
