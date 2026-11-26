#include "render/Layout.hpp"

#include <algorithm>
#include <cmath>

namespace spindle::render {

ScreenLayout computeLayout(int pixelWidth, int pixelHeight, Rect safe) {
    ScreenLayout l;
    l.width = static_cast<float>(std::max(1, pixelWidth));
    l.height = static_cast<float>(std::max(1, pixelHeight));
    // The usable rectangle: the safe area clipped to the screen, or the whole screen.
    Rect use{0.0f, 0.0f, l.width, l.height};
    if (safe.w > 0.0f && safe.h > 0.0f && std::isfinite(safe.x) && std::isfinite(safe.y) && std::isfinite(safe.w) && std::isfinite(safe.h)) {
        const float x0 = std::clamp(safe.x, 0.0f, l.width), y0 = std::clamp(safe.y, 0.0f, l.height);
        const float x1 = std::clamp(safe.x + safe.w, x0, l.width), y1 = std::clamp(safe.y + safe.h, y0, l.height);
        if (x1 - x0 >= 1.0f && y1 - y0 >= 1.0f) use = {x0, y0, x1 - x0, y1 - y0};
    }
    l.scale = std::min(use.w / (4.0f * ScreenLayout::kRefColumnWidth), use.h / ScreenLayout::kRefColumnHeight);
    l.uiScale = l.scale;
    const float colW = ScreenLayout::kRefColumnWidth * l.scale;
    const float colH = ScreenLayout::kRefColumnHeight * l.scale;
    const float x0 = use.x + (use.w - 4.0f * colW) * 0.5f;
    const float y0 = use.y + (use.h - colH) * 0.5f;
    l.area = {x0, y0, 4.0f * colW, colH};
    for (std::size_t i = 0; i < 4; ++i) {
        l.columns[i] = {x0 + colW * static_cast<float>(i), y0, colW, colH};
        l.circleCenter[i] = {l.columns[i].x + colW * 0.5f, y0 + ScreenLayout::kCircleY * l.scale};
    }
    return l;
}

} // namespace spindle::render
