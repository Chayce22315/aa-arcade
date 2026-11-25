#pragma once

namespace spindle::render {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    Vec2 center() const { return {x + w * 0.5f, y + h * 0.5f}; }
};

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    Color withAlpha(float alpha) const { return {r, g, b, a * alpha}; }
};

namespace colors {
inline constexpr Color kBlack{0.0f, 0.0f, 0.0f, 1.0f};
inline constexpr Color kWhite{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr Color kDim{0.55f, 0.55f, 0.55f, 1.0f};
inline constexpr Color kFaint{0.16f, 0.16f, 0.16f, 1.0f};
inline constexpr Color kOut{0.30f, 0.30f, 0.30f, 1.0f};
inline constexpr Color kFail{0.42f, 0.03f, 0.05f, 1.0f};
} // namespace colors

} // namespace spindle::render
