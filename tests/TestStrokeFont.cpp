#include <cmath>

#include "Test.hpp"
#include "render/Layout.hpp"
#include "render/StrokeFont.hpp"

using namespace spindle::render;

TEST_CASE(font_covers_printable_ascii) {
    for (char c = '!'; c <= '~'; ++c) {
        if (!hasGlyph(c)) test::fail(__FILE__, __LINE__, std::string("missing glyph '") + c + "'");
        const Glyph& g = glyph(c);
        CHECK(!g.strokes.empty());
        for (const auto& s : g.strokes)
            for (const Vec2& p : s) CHECK(std::isfinite(p.x) && std::isfinite(p.y) && p.x >= -1e-4f);
    }
    CHECK(!hasGlyph('\x01'));
    CHECK(!glyph('\x7f').strokes.empty()); // falls back to '?'
}

TEST_CASE(font_measure_scales_and_digits_are_tabular) {
    const float a = measureText("spindle", 20.0f);
    const float b = measureText("spindle", 40.0f);
    CHECK_NEAR(b, 2.0f * a, 1e-3);
    CHECK_NEAR(measureText("1", 30.0f), measureText("8", 30.0f), 1e-4);
    CHECK_EQ(measureText("", 30.0f), 0.0f);
    std::vector<std::vector<Vec2>> lines;
    layoutText("ab", 12.0f, {100.0f, 50.0f}, lines);
    CHECK(!lines.empty());
    for (const auto& l : lines)
        for (const Vec2& p : l) CHECK(p.x >= 100.0f - 1e-3f && p.y >= 50.0f - 1e-3f);
}

TEST_CASE(layout_four_columns_and_scale) {
    ScreenLayout l = computeLayout(1920, 1080);
    CHECK_NEAR(l.scale, 1.0, 1e-6);
    CHECK_NEAR(l.columns[3].x, 1440.0, 1e-3);
    CHECK_NEAR(l.circleCenter[0].x, 240.0, 1e-3);
    ScreenLayout wide = computeLayout(3440, 1440);
    CHECK_NEAR(wide.scale, 1440.0 / 1080.0, 1e-5);
    CHECK(wide.area.x > 0.0f); // pillarboxed
    ScreenLayout tall = computeLayout(1280, 1024);
    CHECK_NEAR(tall.scale, 1280.0 / 1920.0, 1e-5);
    CHECK(tall.area.y > 0.0f); // letterboxed
    ScreenLayout zero = computeLayout(0, 0);
    CHECK(zero.scale > 0.0f);
}
