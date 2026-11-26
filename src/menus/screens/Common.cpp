#include "menus/screens/Common.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace spindle::menus::screens {

bool blink(double t, double period) { return std::fmod(t, period) < period * 0.62; }

float px(const ScreenContext& c, float ref) { return ref * c.layout.scale; }

float capPx(const ScreenContext& c, float refCap) {
    float cap = px(c, refCap);
    if (c.touchUi && refCap < 24.0f) cap *= 1.25f; // phones: small text is read at arm's length
    return std::max(cap, std::min(kMinCapPx, px(c, refCap) * 3.0f));
}

void textAt(const ScreenContext& c, std::string_view s, float x, float y, float cap, Color col, Align a) {
    c.r.text(s, {x, y}, capPx(c, cap), col, a);
}

void scrim(const ScreenContext& c, float alpha) { c.r.fillRect({0, 0, c.layout.width, c.layout.height}, colors::kBlack.withAlpha(alpha)); }

void band(const ScreenContext& c, float yRef, float hRef, float alpha) {
    c.r.fillRect({0, areaY(c, yRef), c.layout.width, px(c, hRef)}, colors::kBlack.withAlpha(alpha));
}

Color stationColor(const ScreenContext& c, int s) {
    if (!c.arcade.settings().accessibility.accentColors) return colors::kWhite;
    constexpr Color kAccents[4] = {{0.35f, 0.85f, 1.0f, 1.0f}, {1.0f, 0.45f, 0.8f, 1.0f}, {0.5f, 1.0f, 0.45f, 1.0f}, {1.0f, 0.65f, 0.25f, 1.0f}};
    return kAccents[static_cast<std::size_t>(std::clamp(s, 0, 3))];
}

bool reduceFlashes(const ScreenContext& c) { return c.arcade.settings().accessibility.reduceFlashes; }

void drawIris(const ScreenContext& c, float t) {
    if (t >= 1.0f) return;
    const Vec2 o{c.layout.width * 0.5f, c.layout.height * 0.5f};
    const float full = std::hypot(c.layout.width, c.layout.height) * 0.5f + 4.0f;
    const float hole = full * easeOut(t);
    const float outer = full + 8.0f;
    c.r.ring(o, (hole + outer) * 0.5f, outer - hole, colors::kBlack);
    if (hole > 1.0f) c.r.ring(o, hole, std::max(1.5f, px(c, 3.0f)), colors::kWhite.withAlpha(1.0f - t));
}

float midX(const ScreenContext& c) { return c.layout.area.x + c.layout.area.w * 0.5f; }
float areaY(const ScreenContext& c, float ref) { return c.layout.area.y + px(c, ref); }

const char* startPrompt(const ScreenContext& c) {
    if (!c.arcade.credits().canJoin()) return "INSERT COIN";
    return c.touchUi ? "TAP TO START" : "PRESS START";
}

void creditLine(const ScreenContext& c) {
    const auto& credits = c.arcade.credits();
    const std::string s = credits.freePlay() ? std::string("FREE PLAY") : std::format("CREDIT {}", credits.credits());
    textAt(c, s, midX(c), areaY(c, 1036), 18, colors::kDim);
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

double ticksToSeconds(std::uint64_t from, std::uint64_t to) { return to > from ? static_cast<double>(to - from) * geom::kTickSeconds : 0.0; }

float easeOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

float easeInOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Color hsv(float h, float s, float v, float a) {
    h = h - std::floor(h);
    const float k = h * 6.0f;
    const int i = static_cast<int>(k) % 6;
    const float f = k - std::floor(k);
    const float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    switch (i) {
    case 0: return {v, t, p, a};
    case 1: return {q, v, p, a};
    case 2: return {p, v, t, a};
    case 3: return {p, q, v, a};
    case 4: return {t, p, v, a};
    default: return {v, p, q, a};
    }
}

void arc(render::Renderer2D& r, Vec2 centre, float radius, float from, float to, float thickness, Color col) {
    const float span = to - from;
    if (std::fabs(span) < 1e-4f || radius <= 0.0f) return;
    const int n = std::clamp(static_cast<int>(std::fabs(span) * radius / 6.0f), 6, 160);
    std::vector<Vec2> pts;
    pts.reserve(static_cast<std::size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        const float a = from + span * static_cast<float>(i) / static_cast<float>(n);
        pts.push_back({centre.x + radius * std::cos(a), centre.y + radius * std::sin(a)});
    }
    r.polyline(pts, thickness, col);
}

void rainbowRim(const ScreenContext& c, Vec2 centre, float radius, float thickness) {
    constexpr int kSegments = 48;
    const float spin = static_cast<float>(c.time) * 0.45f;
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = kTau * static_cast<float>(i) / kSegments, a1 = kTau * static_cast<float>(i + 1) / kSegments;
        arc(c.r, centre, radius, a0, a1 + 0.03f, thickness, hsv(static_cast<float>(i) / kSegments - spin, 0.8f, 1.0f));
    }
}

float fitCap(std::string_view s, float width, float maxCap) {
    const float w = render::Renderer2D::textWidth(s, maxCap);
    if (w <= width || w <= 0.0f) return maxCap;
    return maxCap * width / w;
}

Color lookColor(const StationLook& look, float angle, double time) {
    if (!look.rainbow) return look.fg;
    return hsv(angle / kTau + static_cast<float>(time) * 0.35f, 0.55f, 1.0f, look.fg.a);
}

void drawBall(const ScreenContext& c, Vec2 p, float radius, int number, Color fg, bool showNumber) {
    const float r = px(c, radius);
    c.r.fillCircle(p, r, fg);
    if (showNumber && number > 0) {
        const std::string n = std::to_string(number);
        const float cap = px(c, (n.size() > 1 ? 9.5f : 11.0f) * radius / static_cast<float>(geom::kBallRadius));
        c.r.text(n, {p.x, p.y - cap * 0.5f}, cap, colors::kBlack, Align::Center, std::max(1.0f, cap * 0.16f));
    }
}

void drawLabelBall(const ScreenContext& c, Vec2 p, float radius, std::string_view label, Color fg) {
    const float r = px(c, radius);
    c.r.fillCircle(p, r, fg);
    const float cap = fitCap(label, r * 1.3f, r * 0.95f);
    c.r.text(label, {p.x, p.y - cap * 0.5f}, cap, colors::kBlack, Align::Center, std::max(1.0f, cap * 0.15f));
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

void drawStationHeader(const ScreenContext& c, int s, std::string_view kind, Color col) {
    const Rect& r = c.layout.columns[static_cast<std::size_t>(s)];
    const bool white = col.r >= 0.999f && col.g >= 0.999f && col.b >= 0.999f;
    const Color accent = white ? stationColor(c, s).withAlpha(col.a) : col;
    c.r.text(std::format("P{}", s + 1), {r.x + px(c, 40), r.y + px(c, 72)}, px(c, 30), accent);
    c.r.text(kind, {r.x + px(c, 40), r.y + px(c, 112)}, capPx(c, 16), colors::kDim);
}

void drawStationStatus(const ScreenContext& c, int s, std::string_view text, Color col, float cap) {
    const Rect& r = c.layout.columns[static_cast<std::size_t>(s)];
    textAt(c, text, r.x + r.w * 0.5f, r.y + px(c, 168), cap, col);
}

void drawCrown(const ScreenContext& c, Vec2 base, float size, Color col) {
    const float w = size, h = size * 0.62f, t = std::max(1.5f, size * 0.07f);
    const float x0 = base.x - w * 0.5f, x1 = base.x + w * 0.5f;
    // Band, then three points with a jewel on each tip.
    c.r.line({x0, base.y}, {x1, base.y}, t * 1.4f, col);
    const Vec2 zig[] = {{x0, base.y}, {x0, base.y - h * 0.7f}, {x0 + w * 0.25f, base.y - h * 0.32f}, {base.x, base.y - h},
                        {x1 - w * 0.25f, base.y - h * 0.32f}, {x1, base.y - h * 0.7f}, {x1, base.y}};
    c.r.polyline(zig, t, col);
    for (Vec2 tip : {Vec2{x0, base.y - h * 0.7f}, Vec2{base.x, base.y - h}, Vec2{x1, base.y - h * 0.7f}})
        c.r.fillCircle({tip.x, tip.y - t * 0.6f}, t * 1.25f, col);
}

void frame(const ScreenContext& c, const Rect& r, float t, Color col) {
    c.r.fillRect({r.x, r.y, r.w, t}, col);
    c.r.fillRect({r.x, r.y + r.h - t, r.w, t}, col);
    c.r.fillRect({r.x, r.y, t, r.h}, col);
    c.r.fillRect({r.x + r.w - t, r.y, t, r.h}, col);
}

void drawModeIcon(const ScreenContext& c, gameplay::ModeId mode, Vec2 o, float rad, Color col) {
    const float t = std::max(1.5f, rad * 0.09f);
    const float ball = std::max(2.0f, rad * 0.13f);
    switch (mode) {
    case gameplay::ModeId::Battle: // two needles crossing
        for (float s : {-1.0f, 1.0f}) {
            const Vec2 a{o.x - rad * 0.7f, o.y + s * rad * 0.7f}, b{o.x + rad * 0.7f, o.y - s * rad * 0.7f};
            c.r.line(a, b, t, col);
            c.r.fillCircle(a, ball, col);
            c.r.fillCircle(b, ball, col);
        }
        break;
    case gameplay::ModeId::Marathon: // an endless loop
        c.r.ring({o.x - rad * 0.42f, o.y}, rad * 0.4f, t, col);
        c.r.ring({o.x + rad * 0.42f, o.y}, rad * 0.4f, t, col);
        break;
    case gameplay::ModeId::Blitz: { // a lightning bolt
        const Vec2 bolt[] = {{o.x + rad * 0.25f, o.y - rad * 0.85f}, {o.x - rad * 0.35f, o.y + rad * 0.08f}, {o.x + rad * 0.12f, o.y + rad * 0.08f},
                             {o.x - rad * 0.25f, o.y + rad * 0.85f}};
        c.r.polyline(bolt, t * 1.3f, col);
        break;
    }
    case gameplay::ModeId::Race: // two chevrons
        for (float dx : {-0.38f, 0.22f}) {
            const Vec2 ch[] = {{o.x + rad * dx, o.y - rad * 0.6f}, {o.x + rad * (dx + 0.4f), o.y}, {o.x + rad * dx, o.y + rad * 0.6f}};
            c.r.polyline(ch, t * 1.3f, col);
        }
        break;
    case gameplay::ModeId::Crown: drawCrown(c, {o.x, o.y + rad * 0.4f}, rad * 1.5f, col); break;
    case gameplay::ModeId::Sabotage: { // a lit bomb: junk on its way
        const Vec2 b{o.x - rad * 0.12f, o.y + rad * 0.15f};
        c.r.fillCircle(b, rad * 0.5f, col);
        const Vec2 fuse[] = {{b.x + rad * 0.3f, b.y - rad * 0.4f}, {o.x + rad * 0.45f, o.y - rad * 0.55f}, {o.x + rad * 0.6f, o.y - rad * 0.8f}};
        c.r.polyline(fuse, t, col);
        for (int i = 0; i < 4; ++i) {
            const float a = kTau * static_cast<float>(i) / 4.0f + 0.4f;
            const Vec2 spark{o.x + rad * 0.62f, o.y - rad * 0.84f};
            c.r.line(spark, {spark.x + std::cos(a) * rad * 0.18f, spark.y + std::sin(a) * rad * 0.18f}, t * 0.8f, col);
        }
        break;
    }
    case gameplay::ModeId::Teams: // two pairs
        for (float side : {-1.0f, 1.0f}) {
            c.r.fillCircle({o.x + side * rad * 0.55f, o.y - rad * 0.25f}, ball * 1.5f, col);
            c.r.fillCircle({o.x + side * rad * 0.55f, o.y + rad * 0.3f}, ball * 1.5f, col);
            c.r.line({o.x + side * rad * 0.55f, o.y - rad * 0.25f}, {o.x + side * rad * 0.55f, o.y + rad * 0.3f}, t, col);
        }
        c.r.line({o.x - rad * 0.15f, o.y}, {o.x + rad * 0.15f, o.y}, t, col);
        break;
    case gameplay::ModeId::Chaos: { // a die
        frame(c, {o.x - rad * 0.6f, o.y - rad * 0.6f, rad * 1.2f, rad * 1.2f}, t, col);
        for (float k : {-0.3f, 0.0f, 0.3f}) c.r.fillCircle({o.x + rad * k, o.y + rad * k}, ball, col);
        break;
    }
    case gameplay::ModeId::Daily: // a sun
        c.r.ring(o, rad * 0.36f, t, col);
        for (int i = 0; i < 8; ++i) {
            const float a = kTau * static_cast<float>(i) / 8.0f + 0.2f;
            c.r.line({o.x + std::cos(a) * rad * 0.55f, o.y + std::sin(a) * rad * 0.55f}, {o.x + std::cos(a) * rad * 0.82f, o.y + std::sin(a) * rad * 0.82f}, t,
                     col);
        }
        break;
    }
}

Vec2 crownSpot(const ScreenContext& c, int s) {
    const Rect& r = c.layout.columns[static_cast<std::size_t>(s)];
    return {r.x + px(c, 122), r.y + px(c, 98)};
}

} // namespace spindle::menus::screens
