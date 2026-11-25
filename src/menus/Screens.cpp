#include "menus/Screens.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "engine/Branding.hpp"
#include "gameplay/Geometry.hpp"

namespace spindle::menus {
namespace {

using render::Align;
using render::Color;
using render::Rect;
using render::Vec2;
namespace colors = render::colors;
namespace geom = gameplay::geom;

constexpr float kTau = 2.0f * std::numbers::pi_v<float>;

bool blink(double t, double period = 1.0) { return std::fmod(t, period) < period * 0.62; }

float px(const ScreenContext& c, float ref) { return ref * c.layout.scale; }

void textAt(const ScreenContext& c, std::string_view s, float x, float y, float cap, Color col, Align a = Align::Center) {
    c.r.text(s, {x, y}, px(c, cap), col, a);
}

// Centre of the whole station area, for full-width messages.
float midX(const ScreenContext& c) { return c.layout.area.x + c.layout.area.w * 0.5f; }
float areaY(const ScreenContext& c, float ref) { return c.layout.area.y + px(c, ref); }

void creditLine(const ScreenContext& c) {
    const auto& credits = c.arcade.credits();
    const std::string s = credits.freePlay() ? std::string("FREE PLAY") : std::format("CREDIT {}", credits.credits());
    textAt(c, s, midX(c), areaY(c, 1040), 16, colors::kDim);
}

void dividers(const ScreenContext& c) {
    for (int i = 1; i < 4; ++i) {
        const Rect& col = c.layout.columns[static_cast<std::size_t>(i)];
        c.r.line({col.x, col.y + px(c, 30)}, {col.x, col.y + col.h - px(c, 30)}, 1.0f, colors::kFaint);
    }
}

void exitHint(const ScreenContext& c) {
    if (c.exitHold <= 0.05 || c.exitHoldLimit <= 0.0) return;
    const float f = static_cast<float>(std::clamp(c.exitHold / c.exitHoldLimit, 0.0, 1.0));
    const float w = px(c, 360);
    const float x = midX(c) - w * 0.5f, y = areaY(c, 20);
    textAt(c, "hold to exit", midX(c), y, 14, colors::kWhite);
    c.r.fillRect({x, y + px(c, 26), w, px(c, 3)}, colors::kFaint);
    c.r.fillRect({x, y + px(c, 26), w * f, px(c, 3)}, colors::kWhite);
}

// ------------------------------------------------------------------ station rendering

struct StationLook {
    Color fg = colors::kWhite;
    bool dim = false;
};

void drawBall(const ScreenContext& c, Vec2 p, float radius, int number, Color fg, bool showNumber) {
    const float r = px(c, radius);
    c.r.fillCircle(p, r, fg);
    if (showNumber && number > 0) {
        const std::string n = std::to_string(number);
        const float cap = px(c, (n.size() > 1 ? 9.5f : 11.0f) * radius / static_cast<float>(geom::kBallRadius));
        c.r.text(n, {p.x, p.y - cap * 0.5f}, cap, colors::kBlack, Align::Center, std::max(1.0f, cap * 0.16f));
    }
}

// A powerup ring: an outline with the power's mark inside (all lines, no images).
void drawPowerupIcon(render::Renderer2D& r, gameplay::PowerupKind kind, Vec2 c, float radius, Color col) {
    const float t = std::max(1.2f, radius * 0.17f);
    r.ring(c, radius, t, col);
    switch (kind) {
    case gameplay::PowerupKind::Shield: r.ring(c, radius * 0.48f, t, col); break;
    case gameplay::PowerupKind::Skip: r.line({c.x - radius * 0.5f, c.y}, {c.x + radius * 0.5f, c.y}, t, col); break;
    case gameplay::PowerupKind::Slim: r.fillCircle(c, radius * 0.28f, col); break;
    case gameplay::PowerupKind::Mend:
        r.line({c.x - radius * 0.5f, c.y}, {c.x + radius * 0.5f, c.y}, t, col);
        r.line({c.x, c.y - radius * 0.5f}, {c.x, c.y + radius * 0.5f}, t, col);
        break;
    }
}

const char* powerupLabel(gameplay::PowerupKind kind) {
    switch (kind) {
    case gameplay::PowerupKind::Shield: return "shield";
    case gameplay::PowerupKind::Skip: return "skip";
    case gameplay::PowerupKind::Slim: return "slim";
    case gameplay::PowerupKind::Mend: return "mend";
    }
    return "";
}

void drawField(const ScreenContext& c, int s, const gameplay::Match& m, const StationLook& look) {
    const gameplay::Playfield& f = m.field(s);
    const Vec2 centre = c.layout.circleCenter[static_cast<std::size_t>(s)];
    const auto& rotor = m.rotor().state();
    double angle = rotor.angle;
    if (m.phase() == gameplay::MatchPhase::Playing && !m.paused() && f.status() == gameplay::FieldStatus::Playing)
        angle += rotor.speed * std::min(c.subTick, geom::kTickSeconds);
    const Color fg = look.fg;
    const float needle = px(c, 2.2f);
    const float L = static_cast<float>(f.config().needleLength > 0 ? f.config().needleLength : geom::kNeedleLength);

    const float ball = static_cast<float>(f.ballRadius() > 0.0 ? f.ballRadius() : geom::kBallRadius);
    const auto onCircle = [&](double rel) {
        const double a = rel + angle;
        return Vec2{centre.x + px(c, L) * static_cast<float>(std::cos(a)), centre.y + px(c, L) * static_cast<float>(std::sin(a))};
    };

    // Powerup rings still to be taken, turning with the circle.
    for (const auto& ring : f.pickups())
        if (!ring.claimed)
            drawPowerupIcon(c.r, ring.pickup.kind, onCircle(ring.pickup.angle), px(c, static_cast<float>(geom::kBallRadius) * 1.05f),
                            fg.withAlpha(0.9f));

    const auto& attached = f.attached();
    for (std::size_t i = 0; i < attached.size(); ++i) {
        const Vec2 p = onCircle(attached[i].angle);
        const bool hit = f.status() == gameplay::FieldStatus::Failed && f.conflictAttached() && *f.conflictAttached() == i;
        const Color col = hit ? Color{1.0f, 0.35f, 0.35f, 1.0f} : fg;
        c.r.line(centre, p, needle, col);
        drawBall(c, p, ball, 0, col, false);
    }
    for (std::size_t i = 0; i < f.flying().size(); ++i) {
        const auto& fl = f.flying()[i];
        const Vec2 p{centre.x, centre.y + px(c, static_cast<float>(fl.distance))};
        const bool hit = f.status() == gameplay::FieldStatus::Failed && f.conflictFlying() && *f.conflictFlying() == i;
        drawBall(c, p, ball, fl.number, hit ? Color{1.0f, 0.35f, 0.35f, 1.0f} : fg, true);
    }
    // Waiting needles: the next one sits at the launcher, the rest queue below it.
    const int left = f.needlesLeft();
    const int shown = std::min(left, 10);
    for (int k = 0; k < shown; ++k) {
        const float y = static_cast<float>(geom::kLaunchDistance + geom::kQueueSpacing * k);
        const Color col = k == 0 ? fg : fg.withAlpha(std::max(0.25f, 1.0f - 0.07f * static_cast<float>(k)));
        drawBall(c, {centre.x, centre.y + px(c, y)}, ball, left - k, col, true);
    }
    // Shield: a halo around the core while armed, a burst when it saves a needle.
    const float core = px(c, static_cast<float>(geom::kCoreRadius));
    if (f.shieldArmed()) c.r.ring(centre, core + px(c, 9), px(c, 2.2f), fg);
    const std::uint64_t now = m.tickCount();
    if (f.shieldBreakTick() > 0 && now >= f.shieldBreakTick()) {
        const double since = static_cast<double>(now - f.shieldBreakTick()) * geom::kTickSeconds;
        if (since < 0.45) {
            const float k = static_cast<float>(since / 0.45);
            c.r.ring(centre, core + px(c, 9.0f + 40.0f * k), px(c, 2.2f), fg.withAlpha(1.0f - k));
        }
    }
    // Core with the round number.
    c.r.fillCircle(centre, core, fg);
    const std::string round = std::to_string(m.round());
    const float cap = px(c, round.size() > 2 ? 22.0f : 28.0f);
    c.r.text(round, {centre.x, centre.y - cap * 0.5f}, cap, colors::kBlack, Align::Center, cap * 0.12f);
}

void drawTries(const ScreenContext& c, int s, const gameplay::Participant& p, int maxTries) {
    const Rect& col = c.layout.columns[static_cast<std::size_t>(s)];
    const float r = px(c, 6);
    for (int i = 0; i < maxTries; ++i) {
        const Vec2 pt{col.x + col.w - px(c, 46) - px(c, 22) * static_cast<float>(i), col.y + px(c, 86)};
        if (i < p.tries)
            c.r.fillCircle(pt, r, colors::kWhite);
        else
            c.r.ring(pt, r, px(c, 1.6f), colors::kOut);
    }
}

void drawStationHeader(const ScreenContext& c, int s, std::string_view kind, Color col) {
    const Rect& r = c.layout.columns[static_cast<std::size_t>(s)];
    c.r.text(std::format("P{}", s + 1), {r.x + px(c, 40), r.y + px(c, 72)}, px(c, 30), col);
    c.r.text(kind, {r.x + px(c, 40), r.y + px(c, 114)}, px(c, 13), colors::kDim);
}

void drawStationStatus(const ScreenContext& c, int s, std::string_view text, Color col, float cap = 30) {
    const Rect& r = c.layout.columns[static_cast<std::size_t>(s)];
    textAt(c, text, r.x + r.w * 0.5f, r.y + px(c, 168), cap, col);
}

void drawMatch(const ScreenContext& c, const gameplay::Match& m, bool demo) {
    const auto& lineup = c.arcade.lineup();
    const std::uint64_t now = m.tickCount();
    for (int s = 0; s < gameplay::kMaxStations; ++s) {
        const auto& p = m.participant(s);
        const Rect& col = c.layout.columns[static_cast<std::size_t>(s)];
        if (!p) {
            drawStationHeader(c, s, "", colors::kFaint);
            textAt(c, "-", col.x + col.w * 0.5f, col.y + px(c, 380), 30, colors::kFaint);
            continue;
        }
        const gameplay::Playfield& f = m.field(s);
        const bool out = p->eliminated && !m.stationInRound(s);
        // Fail flash: the column glows dark red and fades.
        if (f.status() == gameplay::FieldStatus::Failed && now >= f.failTick()) {
            const double since = static_cast<double>(now - f.failTick()) * geom::kTickSeconds;
            if (since < 0.6) {
                const float a = static_cast<float>(1.0 - since / 0.6);
                c.r.fillRect(col, colors::kFail.withAlpha(a));
            }
        }
        StationLook look;
        if (out) look.fg = colors::kOut;
        const bool bot = lineup.seats[static_cast<std::size_t>(s)] == players::SeatKind::Bot;
        drawStationHeader(c, s, bot ? "cpu" : "player", out ? colors::kOut : colors::kWhite);
        drawTries(c, s, *p, m.config().tries);
        if (out) {
            drawStationStatus(c, s, "out", colors::kOut);
            c.r.fillCircle(c.layout.circleCenter[static_cast<std::size_t>(s)], px(c, static_cast<float>(geom::kCoreRadius)), colors::kOut);
            continue;
        }
        if (!m.stationInRound(s)) {
            drawStationStatus(c, s, "waiting", colors::kDim, 22);
            c.r.ring(c.layout.circleCenter[static_cast<std::size_t>(s)], px(c, static_cast<float>(geom::kCoreRadius)), px(c, 2), colors::kDim);
            continue;
        }
        drawField(c, s, m, look);
        // Clear pulse: a ring expanding out of the circle.
        if (f.status() == gameplay::FieldStatus::Cleared && now >= f.clearTick()) {
            const double since = static_cast<double>(now - f.clearTick()) * geom::kTickSeconds;
            if (since < 0.7) {
                const float k = static_cast<float>(since / 0.7);
                c.r.ring(c.layout.circleCenter[static_cast<std::size_t>(s)], px(c, 60.0f + 140.0f * k), px(c, 3), colors::kWhite.withAlpha(1.0f - k));
            }
            drawStationStatus(c, s, "clear", colors::kWhite);
        } else if (f.status() == gameplay::FieldStatus::Failed) {
            drawStationStatus(c, s, f.failReason() == gameplay::FailReason::Timeout ? "time" : "miss", Color{1.0f, 0.45f, 0.45f, 1.0f});
        } else if (m.phase() == gameplay::MatchPhase::Ready) {
            drawStationStatus(c, s, "ready", colors::kDim, 24);
        } else if (f.lastClaim() && now >= f.lastClaimTick() &&
                   static_cast<double>(now - f.lastClaimTick()) * geom::kTickSeconds < 0.9) {
            drawStationStatus(c, s, powerupLabel(*f.lastClaim()), colors::kWhite, 26);
        }
        if (m.phase() == gameplay::MatchPhase::Playing && f.status() == gameplay::FieldStatus::Playing) {
            const double left = m.roundTimeLeft();
            if (left <= 10.0) textAt(c, std::format("{}", static_cast<int>(std::ceil(left))), col.x + col.w * 0.5f, col.y + px(c, 1000), 20,
                                     Color{1.0f, 0.5f, 0.5f, 1.0f});
        }
    }
    if (m.suddenDeath() && m.phase() != gameplay::MatchPhase::Over) textAt(c, "sudden death", midX(c), areaY(c, 22), 20, colors::kWhite);
    if (demo) {
        textAt(c, "demo", midX(c), areaY(c, 22), 14, colors::kDim);
        if (blink(c.time))
            textAt(c, c.arcade.credits().canJoin() ? "PRESS START" : "INSERT COIN", midX(c), areaY(c, 600), 40, colors::kWhite);
    }
}

// ------------------------------------------------------------------ attract pages

void decorativeCircle(const ScreenContext& c, Vec2 centre, float scale, double t) {
    const float core = px(c, 46) * scale;
    const float L = px(c, 150) * scale;
    const int needles = 9;
    for (int i = 0; i < needles; ++i) {
        const float a = static_cast<float>(t * 1.1) + kTau * static_cast<float>(i) / needles + (i % 3 == 0 ? 0.25f : 0.0f);
        const Vec2 p{centre.x + L * std::cos(a), centre.y + L * std::sin(a)};
        c.r.line(centre, p, px(c, 2.2f) * scale, colors::kWhite);
        c.r.fillCircle(p, px(c, 11) * scale, colors::kWhite);
    }
    c.r.fillCircle(centre, core, colors::kWhite);
}

void drawTitle(const ScreenContext& c) {
    const float cx = midX(c);
    decorativeCircle(c, {cx, areaY(c, 640)}, 1.0f, c.time);
    const std::string title(kTitle);
    textAt(c, title, cx, areaY(c, 170), 120, colors::kWhite);
    textAt(c, kTagline, cx, areaY(c, 340), 22, colors::kDim);
    if (blink(c.time)) textAt(c, c.arcade.credits().canJoin() ? "PRESS START" : "INSERT COIN", cx, areaY(c, 900), 40, colors::kWhite);
}

void drawHowTo(const ScreenContext& c) {
    const float cx = midX(c);
    textAt(c, "how to play", cx, areaY(c, 120), 40, colors::kWhite);
    const char* lines[] = {
        "press fire to launch a needle into your circle",
        "needles must never touch each other",
        "every round all players get the same circle",
        "clear the round to keep your tries",
        "a touch or the timer costs one try",
        "out of tries means out of the match",
        "the last player standing wins",
    };
    float y = 210;
    for (const char* l : lines) {
        textAt(c, l, cx, areaY(c, y), 22, colors::kWhite);
        y += 54;
    }
    // Powerup legend (only the rings the operator enabled).
    const auto& gp = c.arcade.settings().gameplay;
    const auto kinds = gp.powerups ? gameplay::parsePowerupList(gp.powerupTypes) : std::vector<gameplay::PowerupKind>{};
    if (!kinds.empty()) {
        textAt(c, "land a needle on a ring to take its power", cx, areaY(c, 610), 20, colors::kWhite);
        const float spacing = 230.0f;
        float x = cx - px(c, spacing) * 0.5f * static_cast<float>(kinds.size() - 1);
        for (gameplay::PowerupKind k : kinds) {
            const char* what = k == gameplay::PowerupKind::Shield ? "one touch bounces off"
                               : k == gameplay::PowerupKind::Skip ? "one needle fewer"
                               : k == gameplay::PowerupKind::Slim ? "thinner needles"
                                                                  : "one try back";
            drawPowerupIcon(c.r, k, {x, areaY(c, 682)}, px(c, 15), colors::kWhite);
            textAt(c, powerupLabel(k), x, areaY(c, 712), 17, colors::kWhite);
            textAt(c, what, x, areaY(c, 740), 13, colors::kDim);
            x += px(c, spacing);
        }
    }
    // A small animated example: a needle flying into a turning circle.
    const Vec2 centre{cx, areaY(c, 860)};
    const double phase = std::fmod(c.time, 2.0);
    const float scale = 0.42f;
    decorativeCircle(c, centre, scale, c.time * 0.6);
    const float dist = static_cast<float>(300.0 - std::min(phase, 1.0) * 150.0) * scale;
    if (phase < 1.0) c.r.fillCircle({centre.x, centre.y + px(c, dist)}, px(c, 11) * scale, colors::kWhite);
    if (blink(c.time)) textAt(c, c.arcade.credits().canJoin() ? "PRESS START" : "INSERT COIN", cx, areaY(c, 990), 30, colors::kWhite);
}

void drawWinners(const ScreenContext& c) {
    const float cx = midX(c);
    textAt(c, "recent winners", cx, areaY(c, 160), 40, colors::kWhite);
    float y = 300;
    int rank = 1;
    for (const auto& w : c.arcade.bookkeeping().recentWinners) {
        textAt(c, std::format("{}   P{}   {}   round {}", rank++, w.station + 1, w.bot ? "cpu   " : "player", w.round), cx, areaY(c, y), 28,
               colors::kWhite);
        y += 80;
    }
    if (blink(c.time)) textAt(c, c.arcade.credits().canJoin() ? "PRESS START" : "INSERT COIN", cx, areaY(c, 900), 40, colors::kWhite);
}

void drawInsertCoin(const ScreenContext& c) {
    const float cx = midX(c);
    const auto& credits = c.arcade.credits();
    textAt(c, credits.freePlay() ? std::string("FREE PLAY") : std::format("CREDIT {}", credits.credits()), cx, areaY(c, 300), 80,
           colors::kWhite);
    if (credits.canJoin()) {
        if (blink(c.time)) textAt(c, "PRESS START", cx, areaY(c, 520), 44, colors::kWhite);
        if (!credits.freePlay())
            textAt(c, std::format("{} credit{} per player", credits.config().creditsPerGame, credits.config().creditsPerGame > 1 ? "s" : ""),
                   cx, areaY(c, 620), 20, colors::kDim);
    } else if (blink(c.time)) {
        textAt(c, "INSERT COIN", cx, areaY(c, 520), 44, colors::kWhite);
    }
}

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
            if (blink(c.time, 1.4)) textAt(c, "start = go", x, col.y + px(c, 760), 16, colors::kDim);
        } else {
            c.r.ring(centre, px(c, 46), px(c, 2), colors::kDim);
            const bool needCoin = c.arcade.needCoinFlash(s) > 0.0;
            if (needCoin)
                textAt(c, "INSERT COIN", x, col.y + px(c, 700), 26, Color{1.0f, 0.5f, 0.5f, 1.0f});
            else if (blink(c.time))
                textAt(c, "PRESS START", x, col.y + px(c, 700), 26, colors::kWhite);
        }
    }
    const int left = static_cast<int>(std::ceil(c.arcade.joinTimeLeft()));
    textAt(c, std::format("starting in {}", left), midX(c), areaY(c, 20), 22, colors::kWhite);
}

void drawGameOver(const ScreenContext& c) {
    const auto& res = c.arcade.lastResult();
    if (const gameplay::Match* m = c.arcade.match()) drawMatch(c, *m, false);
    c.r.fillRect({0, 0, c.layout.width, c.layout.height}, colors::kBlack.withAlpha(0.78f));
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
        textAt(c, std::format("{}   P{}   {}   rounds cleared {}", rank, s + 1, bot ? "cpu   " : "player", res->roundsCleared[i]), cx,
               areaY(c, y), 24, rank == 1 ? colors::kWhite : colors::kDim);
        ++rank;
        y += 56;
    }
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

} // namespace

void drawArcade(const ScreenContext& c) {
    const auto& a = c.arcade;
    switch (a.state()) {
    case arcade::ArcadeState::Attract:
        switch (a.attractPage()) {
        case arcade::AttractPage::Title: drawTitle(c); break;
        case arcade::AttractPage::HowTo: drawHowTo(c); break;
        case arcade::AttractPage::Demo:
            dividers(c);
            if (const gameplay::Match* m = a.match()) drawMatch(c, *m, true);
            break;
        case arcade::AttractPage::Winners: drawWinners(c); break;
        }
        break;
    case arcade::ArcadeState::InsertCoin: drawInsertCoin(c); break;
    case arcade::ArcadeState::Join:
        dividers(c);
        drawJoin(c);
        break;
    case arcade::ArcadeState::Playing:
        dividers(c);
        if (const gameplay::Match* m = a.match()) drawMatch(c, *m, false);
        if (a.pausedForDisconnect()) drawDisconnectOverlay(c);
        break;
    case arcade::ArcadeState::GameOver: drawGameOver(c); break;
    }
    creditLine(c);
    exitHint(c);
}

} // namespace spindle::menus
