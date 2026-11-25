#pragma once

#include <string_view>
#include <vector>

#include "render/Geometry2D.hpp"

namespace spindle::render {

// spindle's built-in monoline stroke font: printable ASCII as polylines on a 6-unit cap height
// (y down: 0 = cap top, 2 = x-height, 6 = baseline, 8 = descender). Drawn with anti-aliased lines,
// so it is crisp at any size and needs no font file.
struct Glyph {
    std::vector<std::vector<Vec2>> strokes; // already shifted so the leftmost point is x = 0
    float width = 0.0f;
    bool defined = false;
};

namespace font {
inline constexpr float kCapUnits = 6.0f;
inline constexpr float kSpacingUnits = 1.45f;
inline constexpr float kSpaceUnits = 2.6f;
} // namespace font

const Glyph& glyph(char c);
bool hasGlyph(char c);

// Width in pixels of a single line drawn with the given cap height.
float measureText(std::string_view text, float capHeight);

// Appends polylines (pixel coordinates) for text whose cap top-left is at origin.
void layoutText(std::string_view text, float capHeight, Vec2 origin, std::vector<std::vector<Vec2>>& out);

} // namespace spindle::render
