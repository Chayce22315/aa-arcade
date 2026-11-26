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

// The blinking call to play: coins on a cabinet, a tap on a phone.
const char* startPrompt(const ScreenContext& c) {
    if (!c.arcade.credits().canJoin()) return "INSERT COIN";
    return c.touchUi ? "TAP TO START" : "PRESS START";
}

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
    case gameplay::PowerupKind::Heat: {
        // a small flame: round base rising to a point
        r.fillCircle({c.x, c.y + radius * 0.16f}, radius * 0.27f, col);
        const Vec2 flame[] = {{c.x - radius * 0.25f, c.y + radius * 0.1f}, {c.x + radius * 0.05f, c.y - radius * 0.56f},
                              {c.x + radius * 0.25f, c.y + radius * 0.1f}};
        r.polyline(flame, t, col);
        break;
    }
    case gameplay::PowerupKind::Purge:
        r.line({c.x - radius * 0.38f, c.y - radius * 0.38f}, {c.x + radius * 0.38f, c.y + radius * 0.38f}, t, col);
        r.line({c.x - radius * 0.38f, c.y + radius * 0.38f}, {c.x + radius * 0.38f, c.y - radius * 0.38f}, t, col);
        break;
    case gameplay::PowerupKind::Swift:
        for (float dy : {-0.12f, 0.26f}) {
            const Vec2 chevron[] = {{c.x - radius * 0.4f, c.y + radius * (dy + 0.18f)}, {c.x, c.y + radius * (dy - 0.22f)},
                                    {c.x + radius * 0.4f, c.y + radius * (dy + 0.18f)}};
            r.polyline(chevron, t, col);
        }
        break;
    }
}

const char* powerupBlurb(gameplay::PowerupKind kind) {
    switch (kind) {
    case gameplay::PowerupKind::Shield: return "one touch bounces off";
    case gameplay::PowerupKind::Skip: return "one needle fewer";
    case gameplay::PowerupKind::Slim: return "thinner needles";
    case gameplay::PowerupKind::Mend: return "one try back";
    case gameplay::PowerupKind::Heat: return "on fire at once";
    case gameplay::PowerupKind::Purge: return "burns 2 old needles";
    case gameplay::PowerupKind::Swift: return "faster needles";
    }
    return "";
}

const char* powerupLabel(gameplay::PowerupKind kind) {
    switch (kind) {
    case gameplay::PowerupKind::Shield: return "shield";
    case gameplay::PowerupKind::Skip: return "skip";
    case gameplay::PowerupKind::Slim: return "slim";
    case gameplay::PowerupKind::Mend: return "mend";
    case gameplay::PowerupKind::Heat: return "heat";
    case gameplay::PowerupKind::Purge: return "purge";
    case gameplay::PowerupKind::Swift: return "swift";
    }
    return "";
}

// ------------------------------------------------------------------ fire

constexpr Color kFire{1.0f, 0.56f, 0.14f, 1.0f};
constexpr Color kFireHot{1.0f, 0.86f, 0.5f, 1.0f};

double ticksToSeconds(std::uint64_t from, std::uint64_t to) { return to > from ? static_cast<double>(to - from) * geom::kTickSeconds : 0.0; }

// A flame standing on `base`, pointing along `angle` (radians, 0 = up the screen, clockwise).
// Uses data/art/flame.png; if that failed to load, a plain glow stands in.
void flame(const ScreenContext& c, Vec2 base, float w, float h, float angle, Color tint) {
    const Vec2 up{std::sin(angle), -std::cos(angle)};
    if (c.flame) {
        c.r.sprite(c.flame, {base.x + up.x * h * 0.5f, base.y + up.y * h * 0.5f}, w, h, angle, tint);
        return;
    }
    c.r.fillCircle({base.x + up.x * h * 0.3f, base.y + up.y * h * 0.3f}, w * 0.32f, kFire.withAlpha(tint.a * 0.85f));
    c.r.fillCircle({base.x + up.x * h * 0.55f, base.y + up.y * h * 0.55f}, w * 0.18f, kFireHot.withAlpha(tint.a * 0.85f));
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
    if (c.flame) {
        c.r.sprite(c.flame, {base.x, base.y - h * 0.5f}, w, h, 0.0f, Color{0.5f, 0.5f, 0.5f, 0.42f});
        if (fill > 0.0) {
            const float fh = h * static_cast<float>(fill);
            const Color tint = burning ? kFireHot : colors::kWhite;
            c.r.sprite(c.flame, {base.x, base.y - fh * 0.5f * flick}, w * flick, fh * flick, 0.0f, tint,
                       Rect{0.0f, 1.0f - static_cast<float>(fill), 1.0f, static_cast<float>(fill)});
        }
    } else {
        c.r.ring({base.x, base.y - h * 0.45f}, w * 0.42f, px(c, 1.5f), colors::kDim);
        if (fill > 0.0) c.r.fillCircle({base.x, base.y - h * 0.45f}, w * 0.42f * static_cast<float>(fill), kFire);
    }
    if (burning) {
        c.r.text("fire", {base.x + px(c, 34), base.y - px(c, 52)}, px(c, 18), kFire);
    } else if (f.combo() > 0) {
        c.r.text(std::format("x{}", f.combo()), {base.x + px(c, 34), base.y - px(c, 52)}, px(c, 18), colors::kWhite);
        // The chain: a bar that runs out if the next needle is too slow.
        const int left = f.chainTicksLeft(now);
        const int window = std::max(1, f.config().chainWindowTicks);
        const float bw = px(c, 48) * static_cast<float>(std::clamp(static_cast<double>(left) / window, 0.0, 1.0));
        c.r.fillRect({base.x + px(c, 34), base.y - px(c, 22), bw, px(c, 3)}, colors::kDim);
    }
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
    for (std::size_t i = 0; i < attached.size(); ++i) {
        const Vec2 p = onCircle(attached[i].angle);
        const bool hit = f.status() == gameplay::FieldStatus::Failed && f.conflictAttached() && *f.conflictAttached() == i;
        const Color col = hit ? Color{1.0f, 0.35f, 0.35f, 1.0f} : fg;
        c.r.line(centre, p, needle, col);
        drawBall(c, p, ball, 0, col, false);
    }
    // Burnt needles flare up and vanish where they were.
    for (const gameplay::BurnMark& b : f.burns()) {
        const double since = ticksToSeconds(b.tick, now);
        if (now < b.tick || since >= 0.45) continue;
        const float k = static_cast<float>(since / 0.45);
        const Vec2 p = onCircle(b.angle);
        const float a = static_cast<float>(b.angle + angle) + std::numbers::pi_v<float> * 0.5f;
        flame(c, p, px(c, 34) * (1.0f + k), px(c, 60) * (0.6f + k), a, colors::kWhite.withAlpha(1.0f - k));
        c.r.ring(p, px(c, ball * (1.0f + 2.2f * k)), px(c, 2.0f), kFire.withAlpha(1.0f - k));
    }
    for (std::size_t i = 0; i < f.flying().size(); ++i) {
        const auto& fl = f.flying()[i];
        const Vec2 p{centre.x, centre.y + px(c, static_cast<float>(fl.distance))};
        const bool hit = f.status() == gameplay::FieldStatus::Failed && f.conflictFlying() && *f.conflictFlying() == i;
        if (fl.fire) flame(c, p, px(c, ball * 2.6f), px(c, ball * 5.5f), std::numbers::pi_v<float>, colors::kWhite);
        drawBall(c, p, ball, fl.number, hit ? Color{1.0f, 0.35f, 0.35f, 1.0f} : fl.fire ? kFireHot : fg, true);
    }
    // Waiting needles: the next one sits at the launcher, the rest queue below it.
    const int left = f.needlesLeft();
    const int shown = std::min(left, 10);
    const Color next = f.onFire(now) ? kFireHot : fg; // launched now, it would be a fire needle
    for (int k = 0; k < shown; ++k) {
        const float y = static_cast<float>(geom::kLaunchDistance + geom::kQueueSpacing * k);
        const Color col = k == 0 ? next : fg.withAlpha(std::max(0.25f, 1.0f - 0.07f * static_cast<float>(k)));
        drawBall(c, {centre.x, centre.y + px(c, y)}, ball, left - k, col, true);
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
        } else if (f.onFire(now)) {
            drawStationStatus(c, s, "on fire!", kFire, 28);
        } else if (f.lastClaim() && now >= f.lastClaimTick() &&
                   static_cast<double>(now - f.lastClaimTick()) * geom::kTickSeconds < 0.9) {
            drawStationStatus(c, s, powerupLabel(*f.lastClaim()), colors::kWhite, 26);
        }
        drawFireMeter(c, s, f, now);
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
            textAt(c, startPrompt(c), midX(c), areaY(c, 600), 40, colors::kWhite);
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
    if (blink(c.time)) textAt(c, startPrompt(c), cx, areaY(c, 900), 40, colors::kWhite);
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
                textAt(c, "INSERT COIN", x, col.y + px(c, 700), 26, Color{1.0f, 0.5f, 0.5f, 1.0f});
            else if (blink(c.time))
                textAt(c, c.touchUi ? "TAP TO JOIN" : "PRESS START", x, col.y + px(c, 700), 26, colors::kWhite);
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
        if (a.pausedForDisconnect())
            drawDisconnectOverlay(c);
        else if (a.suspended())
            drawSuspendedOverlay(c);
        break;
    case arcade::ArcadeState::GameOver: drawGameOver(c); break;
    }
    creditLine(c);
    exitHint(c);
}

} // namespace spindle::menus
