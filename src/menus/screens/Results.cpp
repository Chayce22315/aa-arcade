// The end of a match: initials for whoever made a high score table, then the results; plus the
// paused / disconnected overlays drawn over a match.

#include <format>
#include <string>

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

void drawGameOver(const ScreenContext& c) {
    const auto& res = c.arcade.lastResult();
    if (const gameplay::Match* m = c.arcade.match()) drawMatch(c, *m, false);
    c.r.fillRect({0, 0, c.layout.width, c.layout.height}, colors::kBlack.withAlpha(0.86f));
    const float cx = midX(c);
    textAt(c, "GAME OVER", cx, areaY(c, 150), 70, colors::kWhite);
    if (!res) return;
    if (res->winner >= 0) {
        const auto i = static_cast<std::size_t>(res->winner);
        const bool bot = res->lineup.seats[i] == players::SeatKind::Bot;
        textAt(c, std::format("P{} wins", res->winner + 1), cx, areaY(c, 290), 56, colors::kWhite);
        textAt(c, bot ? "cpu" : "player", cx, areaY(c, 370), 18, colors::kDim);
        // Frame the winning column.
        const Rect& col = c.layout.columns[i];
        c.r.ring({col.x + col.w * 0.5f, c.layout.circleCenter[i].y}, px(c, 205), px(c, 2), colors::kWhite.withAlpha(blink(c.time, 0.8) ? 1.0f : 0.4f));
    } else {
        const int cleared = res->standings.empty() ? 0 : res->roundsCleared[static_cast<std::size_t>(res->standings.front())];
        textAt(c, std::format("rounds cleared {}", cleared), cx, areaY(c, 290), 48, colors::kWhite);
    }
    float y = 470;
    int rank = 1;
    for (int s : res->standings) {
        const auto i = static_cast<std::size_t>(s);
        const bool bot = res->lineup.seats[i] == players::SeatKind::Bot;
        const Color col = rank == 1 ? colors::kWhite : colors::kDim;
        const float yy = areaY(c, y);
        c.r.text(std::format("{}", rank), {cx - px(c, 420), yy}, px(c, 24), col, Align::Right);
        c.r.text(std::format("P{}  {}", s + 1, bot ? "cpu" : "player"), {cx - px(c, 380), yy}, px(c, 24), col, Align::Left);
        c.r.text(std::to_string(res->scores[i]), {cx + px(c, 160), yy}, px(c, 26), col, Align::Right);
        c.r.text(std::format("rounds cleared {}", res->roundsCleared[i]), {cx + px(c, 220), yy}, px(c, 18), colors::kDim, Align::Left);
        ++rank;
        y += 56;
    }
}

void drawSuspendedOverlay(const ScreenContext& c) {
    c.r.fillRect({0, 0, c.layout.width, c.layout.height}, colors::kBlack.withAlpha(0.72f));
    textAt(c, "paused", midX(c), areaY(c, 430), 60, colors::kWhite);
    if (blink(c.time)) textAt(c, c.touchUi ? "tap to continue" : "press start or fire to continue", midX(c), areaY(c, 540), 26, colors::kWhite);
}

void drawDisconnectOverlay(const ScreenContext& c) {
    c.r.fillRect({0, 0, c.layout.width, c.layout.height}, colors::kBlack.withAlpha(0.72f));
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
