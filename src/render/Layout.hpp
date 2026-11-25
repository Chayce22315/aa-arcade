#pragma once

#include <array>

#include "render/Geometry2D.hpp"

namespace spindle::render {

// Screen layout for four fixed station columns. Each column is a scaled copy of the 480 x 1080
// reference column; field units (gameplay/Geometry.hpp) are reference pixels. Wider-than-16:9
// screens are pillarboxed so the stations stay together; taller ones are letterboxed.
struct ScreenLayout {
    float width = 0.0f;
    float height = 0.0f;
    float scale = 1.0f;   // pixels per field unit
    float uiScale = 1.0f; // text scale relative to a 1080-pixel-tall screen
    Rect area;            // union of the four columns
    std::array<Rect, 4> columns{};
    std::array<Vec2, 4> circleCenter{};

    static constexpr float kRefColumnWidth = 480.0f;
    static constexpr float kRefColumnHeight = 1080.0f;
    static constexpr float kCircleY = 400.0f; // circle centre from the column top, reference px

    Vec2 fieldToScreen(int station, Vec2 field) const {
        const Vec2 c = circleCenter[static_cast<std::size_t>(station)];
        return {c.x + field.x * scale, c.y + field.y * scale};
    }
    // Reference-column coordinates (0..480, 0..1080) to screen pixels.
    Vec2 columnPoint(int station, float rx, float ry) const {
        const Rect& r = columns[static_cast<std::size_t>(station)];
        return {r.x + rx * scale, r.y + ry * scale};
    }
};

ScreenLayout computeLayout(int pixelWidth, int pixelHeight);

} // namespace spindle::render
