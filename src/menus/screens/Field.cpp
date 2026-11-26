// The match: four columns, each with its circle, queue, fire meter and status line.

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "menus/screens/Common.hpp"

namespace spindle::menus::screens {
namespace {

constexpr Color kJunk{1.0f, 0.5f, 0.22f, 1.0f};
constexpr float kBlindShow = 0.5f, kBlindFade = 0.3f; // seconds a needle shows, then fades, in a blind round

std::string clockText(double seconds) {
    const int s = static_cast<int>(std::ceil(std::max(0.0, seconds)));
    return std::format("{}:{:02}", s / 60, s % 60);
}

// The line across the top: the mode, its clock or count, and the round intro while players get ready.
void drawModeBanner(const ScreenContext& c, const gameplay::Match& m, bool demo) {
    const auto& mode = m.mode();
    const float cx = midX(c);
    std::string line(gameplay::modeName(mode.id));
    switch (mode.id) {
    case gameplay::ModeId::Race: line += std::format("  first to {}", mode.target); break;
    case gameplay::ModeId::Crown: line += std::format("  round {} of {}", std::min(mode.target, m.roundsPlayed() + 1), mode.target); break;
    case gameplay::ModeId::Daily: line += std::format("  {}", c.arcade.highScores().today()); break;
    default: break;
    }
    if (demo) line = (c.arcade.showingReplay() ? "replay  " : "demo  ") + line;
    if (m.suddenDeath()) line = "sudden death";
    if (m.phase() == gameplay::MatchPhase::Ready) {
        textAt(c, std::format("round {}", m.round()), cx, areaY(c, 4), 30, colors::kWhite);
        if (m.mutator() != gameplay::Mutator::None) textAt(c, gameplay::mutatorName(m.mutator()), cx, areaY(c, 44), 18, kGold);
    } else if (mode.end == gameplay::MatchEnd::Clock) {
        const double left = m.clockLeft();
        textAt(c, clockText(left), cx, areaY(c, 6), 30, left <= 10.0 ? kMissRed : colors::kWhite);
    } else {
        textAt(c, line, cx, areaY(c, 22), m.suddenDeath() ? 20.0f : 15.0f, m.suddenDeath() ? colors::kWhite : colors::kDim);
    }
    if (m.phase() == gameplay::MatchPhase::Playing && m.mutator() != gameplay::Mutator::None)
        textAt(c, gameplay::mutatorName(m.mutator()), cx, areaY(c, 44), 14, kGold.withAlpha(0.7f));
}

// Under a station's header: race wins, crown points or its team.
void drawModeCounts(const ScreenContext& c, const gameplay::Match& m, int s) {
    const auto& mode = m.mode();
    const Rect& col = c.layout.columns[static_cast<std::size_t>(s)];
    const auto& p = *m.participant(s);
    if (mode.end == gameplay::MatchEnd::FirstTo) {
        for (int k = 0; k < mode.target; ++k) {
            const Vec2 pt{col.x + px(c, 48 + 22.0f * static_cast<float>(k)), col.y + px(c, 144)};
            if (k < p.raceWins) c.r.fillCircle(pt, px(c, 7), kGold);
            else c.r.ring(pt, px(c, 7), px(c, 1.6f), colors::kOut);
        }
    } else if (mode.end == gameplay::MatchEnd::Rounds) {
        textAt(c, std::format("crown points {}", p.crownPoints), col.x + px(c, 40), col.y + px(c, 136), 14, kGold, Align::Left);
    }
}

// 0..1 strength of the "on fire" look: quick flare-up, steady, then dying down over the last moments.
float fireStrength(const gameplay::Playfield& f, std::uint64_t now) {
    if (f.fireUntilTick() == 0 || now < f.fireStartTick()) return 0.0f;
    const double since = ticksToSeconds(f.fireStartTick(), now);
    const double left = f.onFire(now) ? ticksToSeconds(now, f.fireUntilTick()) : -ticksToSeconds(f.fireUntilTick(), now);
    if (left < -0.3) return 0.0f;
    const double in = std::min(1.0, since / 0.12);
    const double out = std::clamp((left + 0.3) / 0.5, 0.0, 1.0);
    return static_cast<float>(std::min(in, out));
}

// The meter: the flame image, grey when empty, filling in colour from the bottom as the combo grows.
// While on fire it is full and burns down with the time left.
void drawFireMeter(const ScreenContext& c, int s, const gameplay::Playfield& f, std::uint64_t now) {
    if (!f.config().fireEnabled) return;
    const Rect& col = c.layout.columns[static_cast<std::size_t>(s)];
    const float w = px(c, 46), h = px(c, 92);
    const Vec2 base{col.x + px(c, 64), col.y + px(c, 1012)};
    const bool burning = f.onFire(now);
    double fill = f.comboFill();
    if (burning) {
        const double total = ticksToSeconds(f.fireStartTick(), f.fireUntilTick());
        fill = total > 0.0 ? ticksToSeconds(now, f.fireUntilTick()) / total : 0.0;
    }
    const float flick = burning ? 1.0f + 0.06f * std::sin(static_cast<float>(c.time) * 31.0f) : 1.0f;
    if (c.flame && !lookOn(c, arcade::Look::BlueFlames)) {
        c.r.sprite(c.flame, {base.x, base.y - h * 0.5f}, w, h, 0.0f, Color{0.5f, 0.5f, 0.5f, 0.42f});
        if (fill > 0.0) {
            const float fh = h * static_cast<float>(fill);
            const Color tint = burning ? kFireHot : colors::kWhite;
            c.r.sprite(c.flame, {base.x, base.y - fh * 0.5f * flick}, w * flick, fh * flick, 0.0f, tint,
                       Rect{0.0f, 1.0f - static_cast<float>(fill), 1.0f, static_cast<float>(fill)});
        }
    } else {
        c.r.ring({base.x, base.y - h * 0.45f}, w * 0.42f, px(c, 1.5f), colors::kDim);
        if (fill > 0.0) c.r.fillCircle({base.x, base.y - h * 0.45f}, w * 0.42f * static_cast<float>(fill), fireColor(c));
    }
    if (burning) {
        c.r.text("fire", {base.x + px(c, 34), base.y - px(c, 56)}, capPx(c, 22), fireColor(c));
    } else if (f.combo() > 0) {
        c.r.text(std::format("x{}", f.combo()), {base.x + px(c, 34), base.y - px(c, 56)}, capPx(c, 22), colors::kWhite);
        // The chain: a bar that runs out if the next needle is too slow.
        const int left = f.chainTicksLeft(now);
        const int window = std::max(1, f.config().chainWindowTicks);
        const float bw = px(c, 48) * static_cast<float>(std::clamp(static_cast<double>(left) / window, 0.0, 1.0));
        c.r.fillRect({base.x + px(c, 34), base.y - px(c, 22), bw, px(c, 3)}, colors::kDim);
    }
}

// An X over a ball that touched (so a fail never relies on red alone).
void cross(const ScreenContext& c, Vec2 p, float r) {
    const float k = r * 0.62f, t = std::max(1.5f, r * 0.28f);
    c.r.line({p.x - k, p.y - k}, {p.x + k, p.y + k}, t, colors::kBlack);
    c.r.line({p.x - k, p.y + k}, {p.x + k, p.y - k}, t, colors::kBlack);
}

void drawField(const ScreenContext& c, int s, const gameplay::Match& m, const StationLook& look, Vec2 shake) {
    const gameplay::Playfield& f = m.field(s);
    const Vec2 centre{c.layout.circleCenter[static_cast<std::size_t>(s)].x + shake.x, c.layout.circleCenter[static_cast<std::size_t>(s)].y + shake.y};
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
    const std::uint64_t now = m.tickCount();
    const float core = px(c, static_cast<float>(geom::kCoreRadius));

    // On fire: the whole circle burns, flames standing out of the rim and around the core.
    if (const float heat = fireStrength(f, now); heat > 0.0f) {
        const float t = static_cast<float>(c.time);
        constexpr int kRim = 18, kCore = 9;
        for (int i = 0; i < kRim; ++i) {
            const float a = static_cast<float>(angle) + kTau * static_cast<float>(i) / kRim;
            const float flick = 0.75f + 0.25f * std::sin(t * 17.0f + static_cast<float>(i) * 2.3f);
            const float r = px(c, L + ball * 0.6f);
            flame(c, {centre.x + r * std::cos(a), centre.y + r * std::sin(a)}, px(c, 30) * heat, px(c, 58) * heat * flick,
                  a + std::numbers::pi_v<float> * 0.5f, colors::kWhite.withAlpha(heat));
        }
        for (int i = 0; i < kCore; ++i) {
            const float a = -static_cast<float>(angle) * 0.5f + kTau * static_cast<float>(i) / kCore;
            const float flick = 0.7f + 0.3f * std::sin(t * 21.0f + static_cast<float>(i) * 1.7f);
            flame(c, {centre.x + core * 0.8f * std::cos(a), centre.y + core * 0.8f * std::sin(a)}, px(c, 34) * heat,
                  px(c, 50) * heat * flick, a + std::numbers::pi_v<float> * 0.5f, colors::kWhite.withAlpha(heat));
        }
    }

    // Powerup rings still to be taken, turning with the circle.
    for (const auto& ring : f.pickups())
        if (!ring.claimed)
            drawPowerupIcon(c.r, ring.pickup.kind, onCircle(ring.pickup.angle), px(c, static_cast<float>(geom::kBallRadius) * 1.05f),
                            fg.withAlpha(0.9f));

    const auto& attached = f.attached();
    const bool blind = m.mutator() == gameplay::Mutator::Blind && m.phase() == gameplay::MatchPhase::Playing;
    for (std::size_t i = 0; i < attached.size(); ++i) {
        const Vec2 p = onCircle(attached[i].angle);
        const bool hit = f.status() == gameplay::FieldStatus::Failed && f.conflictAttached() && *f.conflictAttached() == i;
        Color col = hit ? Color{1.0f, 0.35f, 0.35f, 1.0f} : lookColor(look, static_cast<float>(attached[i].angle + angle), c.time);
        if (!hit && attached[i].junk) col = kJunk.withAlpha(col.a); // sabotage: sent by a rival
        if (blind && !hit && !attached[i].preplaced) {
            // Blind: a needle shows for a moment after it sticks, then fades away.
            const float age = static_cast<float>(ticksToSeconds(attached[i].tick, now));
            const float a = std::clamp(1.0f - (age - kBlindShow) / kBlindFade, 0.0f, 1.0f);
            if (a <= 0.0f) continue;
            col = col.withAlpha(col.a * a);
        }
        c.r.line(centre, p, needle, col);
        drawBall(c, p, ball, 0, col, false);
        if (hit) cross(c, p, px(c, ball));
    }
    // Burnt needles flare up and vanish where they were.
    for (const gameplay::BurnMark& b : f.burns()) {
        const double since = ticksToSeconds(b.tick, now);
        if (now < b.tick || since >= 0.45) continue;
        const float k = static_cast<float>(since / 0.45);
        const Vec2 p = onCircle(b.angle);
        const float a = static_cast<float>(b.angle + angle) + std::numbers::pi_v<float> * 0.5f;
        flame(c, p, px(c, 34) * (1.0f + k), px(c, 60) * (0.6f + k), a, colors::kWhite.withAlpha(1.0f - k));
        c.r.ring(p, px(c, ball * (1.0f + 2.2f * k)), px(c, 2.0f), fireColor(c).withAlpha(1.0f - k));
    }
    for (std::size_t i = 0; i < f.flying().size(); ++i) {
        const auto& fl = f.flying()[i];
        const Vec2 p{centre.x, centre.y + px(c, static_cast<float>(fl.distance))};
        const bool hit = f.status() == gameplay::FieldStatus::Failed && f.conflictFlying() && *f.conflictFlying() == i;
        if (fl.fire) flame(c, p, px(c, ball * 2.6f), px(c, ball * 5.5f), std::numbers::pi_v<float>, colors::kWhite);
        if (fl.ghost) c.r.ring(p, px(c, ball * 1.7f), px(c, 1.5f), fg.withAlpha(0.5f));
        if (lookOn(c, arcade::Look::Trails)) // a comet tail back towards the launcher
            for (int k = 1; k <= 5; ++k) {
                const float back = static_cast<float>(k) * ball * 1.15f;
                if (fl.distance + static_cast<double>(back) > geom::kLaunchDistance) break;
                c.r.fillCircle({p.x, p.y + px(c, back)}, px(c, ball * (1.0f - 0.14f * static_cast<float>(k))),
                               (fl.fire ? fireHotColor(c) : fg).withAlpha(0.42f - 0.07f * static_cast<float>(k)));
            }
        drawBall(c, p, ball, fl.number, hit ? Color{1.0f, 0.35f, 0.35f, 1.0f} : fl.fire ? fireHotColor(c) : fl.ghost ? fg.withAlpha(0.45f) : fg, !hit);
        if (hit) cross(c, p, px(c, ball));
    }
    // Waiting needles: the next one sits at the launcher, the rest queue below it.
    const int left = f.needlesLeft();
    const int shown = std::min(left, 10);
    // Launched now, it would be a fire needle (or a ghost, when a ghost ring is armed).
    const Color next = f.onFire(now) ? fireHotColor(c) : f.ghostArmed() ? fg.withAlpha(0.45f) : fg;
    for (int k = 0; k < shown; ++k) {
        const float y = static_cast<float>(geom::kLaunchDistance + geom::kQueueSpacing * k);
        const Color col = k == 0 ? next : fg.withAlpha(std::max(0.25f, 1.0f - 0.07f * static_cast<float>(k)));
        if (k == 0 && f.ghostArmed() && !f.onFire(now)) c.r.ring({centre.x, centre.y + px(c, y)}, px(c, ball * 1.7f), px(c, 1.5f), fg.withAlpha(0.5f));
        drawBall(c, {centre.x, centre.y + px(c, y)}, ball, left - k, col, true);
    }
    // Gold: a gold halo around the core that shrinks away with the time left.
    if (f.gold(now)) {
        const double goldLeft = ticksToSeconds(now, f.goldUntilTick());
        const float k = static_cast<float>(std::clamp(goldLeft / gameplay::powerup::kGoldSeconds, 0.0, 1.0));
        c.r.ring(centre, core + px(c, 14.0f + 10.0f * k), px(c, 2.5f), kGold.withAlpha(0.35f + 0.65f * k));
    }
    // Shield: a halo around the core while armed, a burst when it saves a needle.
    if (f.shieldArmed()) c.r.ring(centre, core + px(c, 9), px(c, 2.2f), fg);
    if (f.shieldBreakTick() > 0 && now >= f.shieldBreakTick()) {
        const double since = static_cast<double>(now - f.shieldBreakTick()) * geom::kTickSeconds;
        if (since < 0.45) {
            const float k = static_cast<float>(since / 0.45);
            c.r.ring(centre, core + px(c, 9.0f + 40.0f * k), px(c, 2.2f), fg.withAlpha(1.0f - k));
        }
    }
    // Core with the round number (rainbow for the crown holder).
    c.r.fillCircle(centre, core, fg);
    if (look.rainbow) rainbowRim(c, centre, core - px(c, 3), px(c, 7));
    const std::string round = std::to_string(m.round());
    const float cap = px(c, round.size() > 2 ? 22.0f : 28.0f);
    c.r.text(round, {centre.x, centre.y - cap * 0.5f}, cap, colors::kBlack, Align::Center, cap * 0.12f);
}

// Tries as little needles hanging from the top-right corner; the one just lost pops.
void drawTries(const ScreenContext& c, int s, const gameplay::Participant& p, int maxTries, std::uint64_t now) {
    const Rect& col = c.layout.columns[static_cast<std::size_t>(s)];
    const float r = px(c, 8);
    const double sinceLost = p.tryLostTick > 0 ? ticksToSeconds(p.tryLostTick, now) : 99.0;
    for (int i = 0; i < maxTries; ++i) {
        const Vec2 ballAt{col.x + col.w - px(c, 46) - px(c, 28) * static_cast<float>(i), col.y + px(c, 100)};
        const Vec2 top{ballAt.x, ballAt.y - px(c, 22)};
        if (i < p.tries) {
            c.r.line(top, ballAt, px(c, 2), colors::kWhite);
            c.r.fillCircle(ballAt, r, colors::kWhite);
        } else {
            c.r.ring(ballAt, r, px(c, 1.6f), colors::kOut);
            if (i == p.tries && sinceLost < 0.5) {
                const float k = static_cast<float>(sinceLost / 0.5);
                c.r.ring(ballAt, r * (1.0f + 2.5f * k), px(c, 2), kMissRed.withAlpha(1.0f - k));
            }
        }
    }
}

} // namespace

void drawMatch(const ScreenContext& c, const gameplay::Match& m, bool demo) {
    const auto& lineup = c.arcade.lineup();
    const std::uint64_t now = m.tickCount();
    const bool tally = m.phase() == gameplay::MatchPhase::Tally;
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
        const bool bot = lineup.seats[static_cast<std::size_t>(s)] == players::SeatKind::Bot;
        // A knocked-out player with a high score shoots their initials here while the others play on.
        if (!demo && out && c.arcade.entry(s) && !(tally && m.tallied(s))) {
            drawStationHeader(c, s, "player", colors::kWhite);
            drawInitialsColumn(c, s);
            continue;
        }
        // Fail: the column glows dark red and fades, and only this column gives a short shake (the
        // shared circle never stutters).
        Vec2 shake{0.0f, 0.0f};
        if (!tally && f.status() == gameplay::FieldStatus::Failed && f.failReason() != gameplay::FailReason::Clock && now >= f.failTick()) {
            const double since = static_cast<double>(now - f.failTick()) * geom::kTickSeconds;
            if (since < 0.6) {
                const float a = static_cast<float>(1.0 - since / 0.6);
                c.r.fillRect(col, colors::kFail.withAlpha(reduceFlashes(c) ? a * 0.3f : a));
            }
            if (since < 0.3 && !reduceFlashes(c)) {
                const float k = static_cast<float>(1.0 - since / 0.3);
                shake.x = std::sin(static_cast<float>(since) * 90.0f) * px(c, 9) * k;
            }
        }
        StationLook look;
        look.fg = stationColor(c, s);
        if (out) look.fg = colors::kOut;
        look.rainbow = !out && m.crownHolder() == s && !tally;
        const bots::BotController* brain = bot ? c.arcade.bot(s) : nullptr;
        std::string kind = bot ? (brain ? brain->brain().name() : std::string("cpu")) : "player";
        if (p->team >= 0) kind += std::format("  team {}", p->team + 1);
        drawStationHeader(c, s, kind, out ? colors::kOut : colors::kWhite);
        if (m.mode().knockouts || m.mode().teams) drawTries(c, s, *p, m.maxTries(), now);
        drawModeCounts(c, m, s);
        if (tally && m.tallied(s)) {
            drawTallyStation(c, m, s);
            continue;
        }
        if (out) {
            drawStationStatus(c, s, "out", colors::kOut);
            const Vec2 centre = c.layout.circleCenter[static_cast<std::size_t>(s)];
            c.r.fillCircle(centre, px(c, static_cast<float>(geom::kCoreRadius)), colors::kOut);
            textAt(c, std::to_string(p->score), centre.x, centre.y + px(c, 90), 26, colors::kOut);
            continue;
        }
        if (!m.stationInRound(s)) {
            drawStationStatus(c, s, "waiting", colors::kDim, 22);
            c.r.ring(c.layout.circleCenter[static_cast<std::size_t>(s)], px(c, static_cast<float>(geom::kCoreRadius)), px(c, 2), colors::kDim);
            continue;
        }
        drawField(c, s, m, look, shake);
        // Clear: a ring bursts out of the circle with a spray of sparks.
        if (f.status() == gameplay::FieldStatus::Cleared && now >= f.clearTick()) {
            const double since = static_cast<double>(now - f.clearTick()) * geom::kTickSeconds;
            const Vec2 centre = c.layout.circleCenter[static_cast<std::size_t>(s)];
            if (since < 0.7) {
                const float k = static_cast<float>(since / 0.7);
                c.r.ring(centre, px(c, 60.0f + 140.0f * k), px(c, reduceFlashes(c) ? 2.0f : 3.0f), colors::kWhite.withAlpha(1.0f - k));
                if (!reduceFlashes(c))
                    for (int i = 0; i < 12; ++i) {
                        const float a = kTau * static_cast<float>(i) / 12.0f + 0.3f;
                        const float d = px(c, 70.0f + 210.0f * easeOut(k));
                        c.r.fillCircle({centre.x + std::cos(a) * d, centre.y + std::sin(a) * d}, px(c, 5.0f * (1.0f - k) + 1.0f), kGold.withAlpha(1.0f - k));
                    }
            }
            drawStationStatus(c, s, "clear", colors::kWhite);
        } else if (f.status() == gameplay::FieldStatus::Failed) {
            drawStationStatus(c, s, f.failReason() == gameplay::FailReason::Timeout ? "time" : "miss", kMissRed);
        } else if (m.phase() == gameplay::MatchPhase::Ready) {
            if (m.junkFor(s) > 0) {
                drawStationStatus(c, s, std::format("+{} junk", m.junkFor(s)), kJunk, 24);
            } else {
                // A 3-2-1 over the ready pause.
                const double left = std::max(0.0, m.config().readyDelay - m.phaseTime());
                const int n = std::clamp(static_cast<int>(std::ceil(left / std::max(0.1, m.config().readyDelay) * 3.0)), 1, 3);
                drawStationStatus(c, s, std::to_string(n), colors::kWhite, 34);
            }
        } else if (f.onFire(now)) {
            drawStationStatus(c, s, "on fire!", fireColor(c), 28);
        } else if (f.lastClaim() && now >= f.lastClaimTick() &&
                   static_cast<double>(now - f.lastClaimTick()) * geom::kTickSeconds < 0.9) {
            drawStationStatus(c, s, powerupLabel(*f.lastClaim()), colors::kWhite, 26);
        }
        // A close call pops up next to the circle (no numbers: scores stay hidden till the round ends).
        if (p->closeCallTick > 0 && now >= p->closeCallTick && f.status() == gameplay::FieldStatus::Playing) {
            const double since = ticksToSeconds(p->closeCallTick, now);
            if (since < 0.7) {
                const float k = static_cast<float>(since / 0.7);
                textAt(c, "close call!", col.x + col.w * 0.5f, col.y + px(c, 228.0f - 26.0f * k), 18, kGold.withAlpha(1.0f - k));
            }
        }
        drawFireMeter(c, s, f, now);
        if (m.phase() == gameplay::MatchPhase::Playing && f.status() == gameplay::FieldStatus::Playing) {
            const double left = m.roundTimeLeft();
            if (left <= 10.0) textAt(c, std::format("{}", static_cast<int>(std::ceil(left))), col.x + col.w * 0.5f, col.y + px(c, 1000), 20,
                                     Color{1.0f, 0.5f, 0.5f, 1.0f});
        }
    }
    drawCrownLayer(c, m);
    if (m.phase() != gameplay::MatchPhase::Over && m.phase() != gameplay::MatchPhase::Tally) drawModeBanner(c, m, demo);
    if (const arcade::Replay* r = c.arcade.showingReplay(); demo && r) {
        band(c, 540, 130, 0.85f); // whose record this is, and the call to play
        textAt(c, std::format("record game  {}  {}", r->name, r->score), midX(c), areaY(c, 552), 24, kGold);
        if (blink(c.time)) textAt(c, startPrompt(c), midX(c), areaY(c, 604), 36, colors::kWhite);
    } else if (demo) {
        band(c, 570, 90, 0.85f); // the call to play stays readable over four busy circles
        if (blink(c.time)) textAt(c, startPrompt(c), midX(c), areaY(c, 594), 40, colors::kWhite);
    }
}

} // namespace spindle::menus::screens
