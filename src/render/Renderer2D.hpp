#pragma once

#include <map>
#include <span>
#include <string_view>
#include <vector>

#include <SDL3/SDL.h>

#include "render/Geometry2D.hpp"

namespace spindle::render {

enum class Align { Left, Center, Right };

// Immediate-mode 2D drawing batched into one SDL_RenderGeometry call per frame. Every edge gets a
// one-pixel alpha feather, which gives clean anti-aliased circles and lines on any SDL backend
// without shaders. Coordinates are output pixels, y down.
class Renderer2D {
public:
    explicit Renderer2D(SDL_Renderer* renderer) : renderer_(renderer) {}

    void begin(Color clearColor);
    void end(); // flushes the batch

    void fillRect(const Rect& r, Color c);
    void fillCircle(Vec2 center, float radius, Color c);
    void ring(Vec2 center, float radius, float thickness, Color c);
    void line(Vec2 a, Vec2 b, float thickness, Color c);
    void polyline(std::span<const Vec2> points, float thickness, Color c);

    // Draws part of a texture as a quad of size w x h centred on center, rotated by angle radians
    // (clockwise on screen) and tinted by c. uv selects the texture region in 0..1 coordinates.
    // Textured quads share the batch with the shapes; changing texture flushes it, keeping draw order.
    void sprite(SDL_Texture* texture, Vec2 center, float w, float h, float angle, Color c, Rect uv = {0.0f, 0.0f, 1.0f, 1.0f});

    // Draws text with the built-in stroke font. pos is the cap-top anchor (left, centre or right).
    void text(std::string_view s, Vec2 pos, float capHeight, Color c, Align align = Align::Left, float thickness = 0.0f);
    static float textWidth(std::string_view s, float capHeight);

    std::size_t lastVertexCount() const { return lastVertices_; }
    std::size_t lastDrawCalls() const { return lastCalls_; }

private:
    void prepare(SDL_Texture* texture);
    int vertex(float x, float y, Color c, float u = 0.0f, float v = 0.0f);
    void tri(int a, int b, int c);
    const std::vector<Vec2>& unitCircle(int segments);
    void flush();

    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* batchTexture_ = nullptr;
    std::vector<SDL_Vertex> vertices_;
    std::vector<int> indices_;
    std::map<int, std::vector<Vec2>> circles_;
    std::vector<std::vector<Vec2>> textScratch_;
    std::size_t lastVertices_ = 0;
    std::size_t lastCalls_ = 0;
    std::size_t calls_ = 0;
    std::size_t frameVertices_ = 0;
};

} // namespace spindle::render
