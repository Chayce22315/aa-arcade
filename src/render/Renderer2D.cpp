#include "render/Renderer2D.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "render/StrokeFont.hpp"

namespace spindle::render {
namespace {
constexpr float kFeather = 1.0f;
constexpr std::size_t kFlushVertices = 60000;
} // namespace

void Renderer2D::begin(Color clearColor) {
    vertices_.clear();
    indices_.clear();
    calls_ = 0;
    frameVertices_ = 0;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColorFloat(renderer_, clearColor.r, clearColor.g, clearColor.b, 1.0f);
    SDL_RenderClear(renderer_);
}

void Renderer2D::end() {
    flush();
    lastVertices_ = frameVertices_;
    lastCalls_ = calls_;
}

void Renderer2D::flush() {
    if (indices_.empty()) return;
    SDL_RenderGeometry(renderer_, nullptr, vertices_.data(), static_cast<int>(vertices_.size()), indices_.data(),
                       static_cast<int>(indices_.size()));
    frameVertices_ += vertices_.size();
    ++calls_;
    vertices_.clear();
    indices_.clear();
}

int Renderer2D::vertex(float x, float y, Color c) {
    SDL_Vertex v;
    v.position = {x, y};
    v.color = {c.r, c.g, c.b, c.a};
    v.tex_coord = {0.0f, 0.0f};
    vertices_.push_back(v);
    return static_cast<int>(vertices_.size() - 1);
}

void Renderer2D::tri(int a, int b, int c) {
    indices_.push_back(a);
    indices_.push_back(b);
    indices_.push_back(c);
}

const std::vector<Vec2>& Renderer2D::unitCircle(int segments) {
    auto it = circles_.find(segments);
    if (it != circles_.end()) return it->second;
    std::vector<Vec2> pts(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const float a = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(segments);
        pts[static_cast<std::size_t>(i)] = {std::cos(a), std::sin(a)};
    }
    return circles_.emplace(segments, std::move(pts)).first->second;
}

void Renderer2D::fillRect(const Rect& r, Color c) {
    if (vertices_.size() > kFlushVertices) flush();
    const int a = vertex(r.x, r.y, c), b = vertex(r.x + r.w, r.y, c), d = vertex(r.x + r.w, r.y + r.h, c),
              e = vertex(r.x, r.y + r.h, c);
    tri(a, b, d);
    tri(a, d, e);
}

void Renderer2D::fillCircle(Vec2 center, float radius, Color c) {
    if (!(radius > 0.0f) || !std::isfinite(radius)) return;
    if (vertices_.size() > kFlushVertices) flush();
    const int segments = std::clamp(static_cast<int>(std::ceil(2.0f * std::numbers::pi_v<float> * radius / 2.5f)), 12, 256);
    const auto& unit = unitCircle(segments);
    const float inner = std::max(0.0f, radius - kFeather * 0.5f);
    const float outer = radius + kFeather * 0.5f;
    const Color edge = c.withAlpha(0.0f);
    const int centre = vertex(center.x, center.y, c);
    const int first = static_cast<int>(vertices_.size());
    for (const Vec2& u : unit) {
        vertex(center.x + u.x * inner, center.y + u.y * inner, c);
        vertex(center.x + u.x * outer, center.y + u.y * outer, edge);
    }
    for (int i = 0; i < segments; ++i) {
        const int j = (i + 1) % segments;
        const int in0 = first + 2 * i, out0 = in0 + 1, in1 = first + 2 * j, out1 = in1 + 1;
        tri(centre, in0, in1);
        tri(in0, out0, out1);
        tri(in0, out1, in1);
    }
}

void Renderer2D::ring(Vec2 center, float radius, float thickness, Color c) {
    if (!(radius > 0.0f) || !(thickness > 0.0f)) return;
    if (vertices_.size() > kFlushVertices) flush();
    const int segments = std::clamp(static_cast<int>(std::ceil(2.0f * std::numbers::pi_v<float> * radius / 2.5f)), 16, 256);
    const auto& unit = unitCircle(segments);
    const float alpha = std::min(1.0f, thickness);
    const Color solid = c.withAlpha(alpha);
    const Color edge = c.withAlpha(0.0f);
    const float half = std::max(0.0f, thickness * 0.5f - kFeather * 0.5f);
    const float r0 = radius - half - kFeather, r1 = radius - half, r2 = radius + half, r3 = radius + half + kFeather;
    const int first = static_cast<int>(vertices_.size());
    for (const Vec2& u : unit) {
        vertex(center.x + u.x * r0, center.y + u.y * r0, edge);
        vertex(center.x + u.x * r1, center.y + u.y * r1, solid);
        vertex(center.x + u.x * r2, center.y + u.y * r2, solid);
        vertex(center.x + u.x * r3, center.y + u.y * r3, edge);
    }
    for (int i = 0; i < segments; ++i) {
        const int j = (i + 1) % segments;
        for (int k = 0; k < 3; ++k) {
            const int a = first + 4 * i + k, b = first + 4 * i + k + 1, d = first + 4 * j + k, e = first + 4 * j + k + 1;
            tri(a, b, e);
            tri(a, e, d);
        }
    }
}

void Renderer2D::line(Vec2 a, Vec2 b, float thickness, Color c) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (!(thickness > 0.0f) || !std::isfinite(len)) return;
    if (vertices_.size() > kFlushVertices) flush();
    // Direction and normal; a zero-length line becomes a square dot.
    const float ux = len > 1e-6f ? dx / len : 1.0f, uy = len > 1e-6f ? dy / len : 0.0f;
    const float nx = -uy, ny = ux;
    const float alpha = std::min(1.0f, thickness);
    const float half = std::max(0.0f, thickness * 0.5f - kFeather * 0.5f);
    const Color solid = c.withAlpha(alpha);
    const Color edge = c.withAlpha(0.0f);
    // Inner rectangle extended by `half` along the line (square caps), outer one by a further feather.
    const float ix = ux * half, iy = uy * half, ox = ux * (half + kFeather), oy = uy * (half + kFeather);
    const float inx = nx * half, iny = ny * half, onx = nx * (half + kFeather), ony = ny * (half + kFeather);
    const int i0 = vertex(a.x - ix + inx, a.y - iy + iny, solid);
    const int i1 = vertex(b.x + ix + inx, b.y + iy + iny, solid);
    const int i2 = vertex(b.x + ix - inx, b.y + iy - iny, solid);
    const int i3 = vertex(a.x - ix - inx, a.y - iy - iny, solid);
    const int o0 = vertex(a.x - ox + onx, a.y - oy + ony, edge);
    const int o1 = vertex(b.x + ox + onx, b.y + oy + ony, edge);
    const int o2 = vertex(b.x + ox - onx, b.y + oy - ony, edge);
    const int o3 = vertex(a.x - ox - onx, a.y - oy - ony, edge);
    tri(i0, i1, i2);
    tri(i0, i2, i3);
    const int in[4] = {i0, i1, i2, i3};
    const int out[4] = {o0, o1, o2, o3};
    for (int k = 0; k < 4; ++k) {
        const int n = (k + 1) % 4;
        tri(in[k], out[k], out[n]);
        tri(in[k], out[n], in[n]);
    }
}

void Renderer2D::polyline(std::span<const Vec2> points, float thickness, Color c) {
    if (points.size() < 2) return;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) line(points[i], points[i + 1], thickness, c);
    // Round joins hide the square caps at corners of thick strokes.
    if (thickness > 2.5f)
        for (std::size_t i = 1; i + 1 < points.size(); ++i) fillCircle(points[i], thickness * 0.5f, c);
}

float Renderer2D::textWidth(std::string_view s, float capHeight) { return measureText(s, capHeight); }

void Renderer2D::text(std::string_view s, Vec2 pos, float capHeight, Color c, Align align, float thickness) {
    if (s.empty() || !(capHeight > 0.0f)) return;
    const float w = measureText(s, capHeight);
    Vec2 origin = pos;
    if (align == Align::Center) origin.x -= w * 0.5f;
    if (align == Align::Right) origin.x -= w;
    const float t = thickness > 0.0f ? thickness : std::max(1.25f, capHeight * 0.11f);
    textScratch_.clear();
    layoutText(s, capHeight, origin, textScratch_);
    for (const auto& stroke : textScratch_) polyline(stroke, t, c);
}

} // namespace spindle::render
