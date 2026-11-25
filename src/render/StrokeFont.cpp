#include "render/StrokeFont.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <numbers>

namespace spindle::render {
namespace {

using Path = std::vector<Vec2>;

struct Builder {
    std::vector<Path> strokes;

    Builder& line(float x0, float y0, float x1, float y1) {
        strokes.push_back({{x0, y0}, {x1, y1}});
        return *this;
    }
    Builder& poly(std::initializer_list<Vec2> pts) {
        strokes.emplace_back(pts);
        return *this;
    }
    // Arc of an ellipse from a0 to a1 degrees (0 = right, 90 = down), appended to a new stroke.
    Builder& arc(float cx, float cy, float rx, float ry, float a0, float a1) {
        strokes.push_back(arcPoints(cx, cy, rx, ry, a0, a1));
        return *this;
    }
    // Arc appended to the previous stroke so the two join into one polyline.
    Builder& arcTo(float cx, float cy, float rx, float ry, float a0, float a1) {
        Path pts = arcPoints(cx, cy, rx, ry, a0, a1);
        if (strokes.empty()) strokes.emplace_back();
        strokes.back().insert(strokes.back().end(), pts.begin(), pts.end());
        return *this;
    }
    Builder& to(float x, float y) {
        if (strokes.empty()) strokes.emplace_back();
        strokes.back().push_back({x, y});
        return *this;
    }
    Builder& dot(float x, float y) { return line(x, y - 0.05f, x, y + 0.05f); }

    static Path arcPoints(float cx, float cy, float rx, float ry, float a0, float a1) {
        const float span = std::fabs(a1 - a0);
        const int steps = std::max(4, static_cast<int>(std::ceil(span / 12.0f)));
        Path p;
        p.reserve(static_cast<std::size_t>(steps) + 1);
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const float a = (a0 + (a1 - a0) * t) * std::numbers::pi_v<float> / 180.0f;
            p.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a)});
        }
        return p;
    }
};

// Zero-length strokes act as invisible anchors: they widen the box (tabular digits) and are then dropped.
Glyph finish(Builder b) {
    Glyph g;
    g.defined = true;
    float minX = 1e9f, maxX = -1e9f;
    for (const Path& s : b.strokes)
        for (const Vec2& p : s) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
        }
    if (b.strokes.empty()) {
        minX = 0.0f;
        maxX = 0.0f;
    }
    std::erase_if(b.strokes, [](const Path& p) { return p.size() == 2 && p[0].x == p[1].x && p[0].y == p[1].y; });
    for (Path& s : b.strokes)
        for (Vec2& p : s) p.x -= minX;
    g.strokes = std::move(b.strokes);
    g.width = maxX - minX;
    return g;
}

// The 9: a closed bowl on top, a straight stem down the right side, a short hook to the bottom left.
Builder nine() {
    Builder b;
    b.arc(2, 1.9f, 2, 1.9f, 0, 360);
    b.poly({{4, 1.9f}, {4, 4.0f}}).arcTo(2, 4.0f, 2, 2.0f, 0, 145);
    return b;
}

// The 6 is the 9 turned half a revolution, so the pair always matches.
Builder six() {
    Builder b = nine();
    for (Path& s : b.strokes)
        for (Vec2& p : s) p = {4.0f - p.x, 6.0f - p.y};
    return b;
}

Glyph make(char c) {
    Builder b;
    switch (c) {
    // ---------------------------------------------------------------- uppercase
    case 'A': b.poly({{0, 6}, {2.1f, 0}, {4.2f, 6}}).line(0.75f, 4.0f, 3.45f, 4.0f); break;
    case 'B':
        b.line(0, 0, 0, 6);
        b.poly({{0, 0}, {2.3f, 0}}).arcTo(2.3f, 1.5f, 1.5f, 1.5f, -90, 90).to(0, 3);
        b.poly({{0, 3}, {2.5f, 3}}).arcTo(2.5f, 4.5f, 1.6f, 1.5f, -90, 90).to(0, 6);
        break;
    case 'C': b.arc(2.7f, 3, 2.7f, 3, 40, 320); break;
    case 'D': b.line(0, 0, 0, 6).poly({{0, 0}, {1.5f, 0}}).arcTo(1.5f, 3, 2.7f, 3, -90, 90).to(0, 6); break;
    case 'E': b.poly({{3.8f, 0}, {0, 0}, {0, 6}, {3.8f, 6}}).line(0, 3, 3.1f, 3); break;
    case 'F': b.poly({{3.8f, 0}, {0, 0}, {0, 6}}).line(0, 3, 3.1f, 3); break;
    case 'G': b.arc(2.7f, 3, 2.7f, 3, 320, 0).to(3.2f, 3); break;
    case 'H': b.line(0, 0, 0, 6).line(4, 0, 4, 6).line(0, 3, 4, 3); break;
    case 'I': b.line(0, 0, 0, 6); break;
    case 'J': b.poly({{3, 0}, {3, 4.4f}}).arcTo(1.5f, 4.4f, 1.5f, 1.6f, 0, 180); break;
    case 'K': b.line(0, 0, 0, 6).line(3.8f, 0, 0, 3.7f).line(1.35f, 2.55f, 4, 6); break;
    case 'L': b.poly({{0, 0}, {0, 6}, {3.5f, 6}}); break;
    case 'M': b.poly({{0, 6}, {0, 0}, {2.6f, 4.2f}, {5.2f, 0}, {5.2f, 6}}); break;
    case 'N': b.poly({{0, 6}, {0, 0}, {4, 6}, {4, 0}}); break;
    case 'O': b.arc(3, 3, 3, 3, 0, 360); break;
    case 'P': b.poly({{0, 6}, {0, 0}, {2.3f, 0}}).arcTo(2.3f, 1.6f, 1.6f, 1.6f, -90, 90).to(0, 3.2f); break;
    case 'Q': b.arc(3, 3, 3, 3, 0, 360).line(3.8f, 4.4f, 6.0f, 6.6f); break;
    case 'R':
        b.poly({{0, 6}, {0, 0}, {2.3f, 0}}).arcTo(2.3f, 1.6f, 1.6f, 1.6f, -90, 90).to(0, 3.2f);
        b.line(2.0f, 3.2f, 4.0f, 6);
        break;
    case 'S': b.arc(2, 1.5f, 1.9f, 1.5f, 330, 90).arcTo(2, 4.5f, 2, 1.5f, 270, 510); break;
    case 'T': b.line(0, 0, 4.2f, 0).line(2.1f, 0, 2.1f, 6); break;
    case 'U': b.poly({{0, 0}, {0, 4}}).arcTo(2, 4, 2, 2, 180, 0).to(4, 0); break;
    case 'V': b.poly({{0, 0}, {2.1f, 6}, {4.2f, 0}}); break;
    case 'W': b.poly({{0, 0}, {1.3f, 6}, {2.6f, 1.6f}, {3.9f, 6}, {5.2f, 0}}); break;
    case 'X': b.line(0, 0, 4, 6).line(4, 0, 0, 6); break;
    case 'Y': b.poly({{0, 0}, {2.1f, 3}, {4.2f, 0}}).line(2.1f, 3, 2.1f, 6); break;
    case 'Z': b.poly({{0, 0}, {4, 0}, {0, 6}, {4, 6}}); break;
    // ---------------------------------------------------------------- lowercase
    case 'a': b.arc(2, 4, 2, 2, 0, 360).line(4, 2, 4, 6); break;
    case 'b': b.line(0, 0, 0, 6).arc(2, 4, 2, 2, 0, 360); break;
    case 'c': b.arc(2, 4, 2, 2, 40, 320); break;
    case 'd': b.line(4, 0, 4, 6).arc(2, 4, 2, 2, 0, 360); break;
    case 'e': b.poly({{0.05f, 4}, {4, 4}}).arcTo(2, 4, 2, 2, 0, -320); break;
    case 'f': b.poly({{1.2f, 6}, {1.2f, 1.4f}}).arcTo(2.4f, 1.4f, 1.2f, 1.2f, 180, 300).line(0, 2.4f, 2.6f, 2.4f); break;
    case 'g': b.arc(2, 4, 2, 2, 0, 360).poly({{4, 2}, {4, 6.4f}}).arcTo(2, 6.4f, 2, 1.6f, 0, 150); break;
    case 'h': b.line(0, 0, 0, 6).arc(2, 4, 2, 2, 180, 360).to(4, 6); break;
    case 'i': b.line(0, 2, 0, 6).dot(0, 0.55f); break;
    case 'j': b.poly({{1.4f, 2}, {1.4f, 6.6f}}).arcTo(0.2f, 6.6f, 1.2f, 1.4f, 0, 150).dot(1.4f, 0.55f); break;
    case 'k': b.line(0, 0, 0, 6).line(3.2f, 2, 0, 4.4f).line(1.2f, 3.6f, 3.4f, 6); break;
    case 'l': b.line(0, 0, 0, 6); break;
    case 'm':
        b.line(0, 2, 0, 6).arc(1.5f, 3.6f, 1.5f, 1.6f, 180, 360).to(3, 6);
        b.arc(4.5f, 3.6f, 1.5f, 1.6f, 180, 360).to(6, 6);
        break;
    case 'n': b.line(0, 2, 0, 6).arc(2, 4, 2, 2, 180, 360).to(4, 6); break;
    case 'o': b.arc(2, 4, 2, 2, 0, 360); break;
    case 'p': b.line(0, 2, 0, 8).arc(2, 4, 2, 2, 0, 360); break;
    case 'q': b.line(4, 2, 4, 8).arc(2, 4, 2, 2, 0, 360); break;
    case 'r': b.line(0, 2, 0, 6).arc(2.3f, 4.1f, 2.3f, 2.1f, 180, 285); break;
    case 's': b.arc(1.75f, 3, 1.75f, 1, 330, 90).arcTo(1.75f, 5, 1.75f, 1, 270, 510); break;
    case 't': b.poly({{1.2f, 0.6f}, {1.2f, 5}}).arcTo(2.4f, 5, 1.2f, 1, 180, 60).line(0, 2, 2.8f, 2); break;
    case 'u': b.poly({{0, 2}, {0, 4}}).arcTo(2, 4, 2, 2, 180, 0).line(4, 2, 4, 6); break;
    case 'v': b.poly({{0, 2}, {2, 6}, {4, 2}}); break;
    case 'w': b.poly({{0, 2}, {1.3f, 6}, {2.6f, 2.6f}, {3.9f, 6}, {5.2f, 2}}); break;
    case 'x': b.line(0, 2, 4, 6).line(4, 2, 0, 6); break;
    case 'y': b.line(0, 2, 2.1f, 6.2f).line(4, 2, 1.1f, 8); break;
    case 'z': b.poly({{0, 2}, {4, 2}, {0, 6}, {4, 6}}); break;
    // ---------------------------------------------------------------- digits (tabular, 4 wide)
    case '0': b.arc(2, 3, 2, 3, 0, 360); break;
    case '1': b.poly({{1.0f, 1.2f}, {2.4f, 0}, {2.4f, 6}}); break;
    case '2': b.arc(2, 1.9f, 1.9f, 1.9f, 195, 400).to(0, 6).to(4, 6); break;
    case '3': b.arc(2, 1.5f, 1.8f, 1.5f, 200, 450).arcTo(2, 4.5f, 2, 1.5f, 270, 520); break;
    case '4': b.poly({{3, 6}, {3, 0}, {0, 4.2f}, {4, 4.2f}}); break;
    case '5': b.poly({{3.8f, 0}, {0.5f, 0}, {0.3f, 2.7f}}).arcTo(2, 4.1f, 2, 1.9f, 230, 520); break;
    case '6': b = six(); break;
    case '7': b.poly({{0, 0}, {4, 0}, {1.4f, 6}}); break;
    case '8': b.arc(2, 1.45f, 1.7f, 1.45f, 0, 360).arc(2, 4.4f, 2, 1.6f, 0, 360); break;
    case '9': b = nine(); break;
    // ---------------------------------------------------------------- punctuation
    case ' ': break;
    case '.': b.dot(0, 5.9f); break;
    case ',': b.line(0.3f, 5.6f, -0.2f, 6.8f); break;
    case ':': b.dot(0, 2.4f).dot(0, 5.9f); break;
    case ';': b.dot(0.3f, 2.4f).line(0.3f, 5.6f, -0.2f, 6.8f); break;
    case '!': b.line(0, 0, 0, 4.2f).dot(0, 5.9f); break;
    case '?': b.arc(2, 1.6f, 1.8f, 1.6f, 195, 430).to(2, 3.6f).to(2, 4.2f).dot(2, 5.9f); break;
    case '-': b.line(0, 3.4f, 2.4f, 3.4f); break;
    case '+': b.line(0, 3.4f, 3, 3.4f).line(1.5f, 1.9f, 1.5f, 4.9f); break;
    case '/': b.line(0, 6.5f, 3, -0.5f); break;
    case '\\': b.line(0, -0.5f, 3, 6.5f); break;
    case '\'': b.line(0, 0, 0, 1.6f); break;
    case '"': b.line(0, 0, 0, 1.6f).line(1, 0, 1, 1.6f); break;
    case '(': b.arc(2.6f, 3, 2.2f, 3.8f, 125, 235); break;
    case ')': b.arc(-1.2f, 3, 2.2f, 3.8f, 55, -55); break;
    case '[': b.poly({{1.4f, -0.3f}, {0, -0.3f}, {0, 6.3f}, {1.4f, 6.3f}}); break;
    case ']': b.poly({{0, -0.3f}, {1.4f, -0.3f}, {1.4f, 6.3f}, {0, 6.3f}}); break;
    case '=': b.line(0, 2.6f, 3, 2.6f).line(0, 4.2f, 3, 4.2f); break;
    case '_': b.line(0, 6.6f, 4, 6.6f); break;
    case '%': b.line(0, 6, 4, 0).arc(0.9f, 1, 0.9f, 0.9f, 0, 360).arc(3.1f, 5, 0.9f, 0.9f, 0, 360); break;
    case '*': b.line(1.5f, 1.2f, 1.5f, 4.2f).line(0.2f, 1.95f, 2.8f, 3.45f).line(0.2f, 3.45f, 2.8f, 1.95f); break;
    case '#': b.line(1.4f, 0.6f, 0.8f, 6).line(3.2f, 0.6f, 2.6f, 6).line(0, 2.2f, 4, 2.2f).line(0, 4.2f, 4, 4.2f); break;
    case '<': b.poly({{3, 1.4f}, {0, 3.4f}, {3, 5.4f}}); break;
    case '>': b.poly({{0, 1.4f}, {3, 3.4f}, {0, 5.4f}}); break;
    case '|': b.line(0, -0.5f, 0, 6.5f); break;
    case '&':
        // Small top loop, a long diagonal to the bottom right, and an open lower bowl with a short tail.
        b.arc(1.7f, 1.35f, 1.05f, 1.2f, 0, 360);
        b.line(1.0f, 2.3f, 4.3f, 6.0f);
        b.arc(1.9f, 4.55f, 1.85f, 1.45f, 245, 25).to(4.4f, 3.6f);
        break;
    case '@': b.arc(3, 3.4f, 3, 3, 30, 340).arc(3, 3.4f, 1.2f, 1.2f, 0, 360); break;
    case '$': b.arc(2, 1.8f, 1.9f, 1.2f, 330, 90).arcTo(2, 4.2f, 2, 1.2f, 270, 510).line(2, -0.4f, 2, 6.4f); break;
    case '^': b.poly({{0, 2}, {1.5f, 0}, {3, 2}}); break;
    case '~': b.poly({{0, 3.6f}, {1, 3.0f}, {2, 3.6f}, {3, 3.0f}}); break;
    case '`': b.line(0, 0, 0.8f, 1.2f); break;
    case '{': b.poly({{1.6f, -0.3f}, {0.8f, 0.2f}, {0.8f, 2.6f}, {0, 3}, {0.8f, 3.4f}, {0.8f, 5.8f}, {1.6f, 6.3f}}); break;
    case '}': b.poly({{0, -0.3f}, {0.8f, 0.2f}, {0.8f, 2.6f}, {1.6f, 3}, {0.8f, 3.4f}, {0.8f, 5.8f}, {0, 6.3f}}); break;
    default: {
        Glyph box = finish(Builder{}.poly({{0, 0}, {3, 0}, {3, 6}, {0, 6}, {0, 0}}));
        box.defined = false;
        return box;
    }
    }
    if (c >= '0' && c <= '9') b.line(0, 6, 0, 6).line(4, 6, 4, 6); // tabular digits: always 4 units wide
    return finish(std::move(b));
}

const std::array<Glyph, 128>& table() {
    static const std::array<Glyph, 128> t = [] {
        std::array<Glyph, 128> out{};
        for (int c = 32; c < 127; ++c) out[static_cast<std::size_t>(c)] = make(static_cast<char>(c));
        return out;
    }();
    return t;
}

float advanceUnits(char c) {
    if (c == ' ') return font::kSpaceUnits;
    return glyph(c).width + font::kSpacingUnits;
}

} // namespace

const Glyph& glyph(char c) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 32 || u >= 127) return table()[static_cast<std::size_t>('?')];
    return table()[u];
}

bool hasGlyph(char c) {
    const auto u = static_cast<unsigned char>(c);
    return u >= 32 && u < 127 && table()[u].defined;
}

float measureText(std::string_view text, float capHeight) {
    const float scale = capHeight / font::kCapUnits;
    float w = 0.0f;
    for (char c : text) w += advanceUnits(c);
    if (!text.empty() && text.back() != ' ') w -= font::kSpacingUnits;
    return std::max(0.0f, w * scale);
}

void layoutText(std::string_view text, float capHeight, Vec2 origin, std::vector<std::vector<Vec2>>& out) {
    const float scale = capHeight / font::kCapUnits;
    float pen = 0.0f;
    for (char c : text) {
        const Glyph& g = glyph(c);
        for (const auto& stroke : g.strokes) {
            std::vector<Vec2> p;
            p.reserve(stroke.size());
            for (const Vec2& v : stroke) p.push_back({origin.x + (pen + v.x) * scale, origin.y + v.y * scale});
            out.push_back(std::move(p));
        }
        pen += advanceUnits(c);
    }
}

} // namespace spindle::render
