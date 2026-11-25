#include "render/Layout.hpp"

#include <algorithm>

namespace spindle::render {

ScreenLayout computeLayout(int pixelWidth, int pixelHeight) {
    ScreenLayout l;
    l.width = static_cast<float>(std::max(1, pixelWidth));
    l.height = static_cast<float>(std::max(1, pixelHeight));
    l.scale = std::min(l.width / (4.0f * ScreenLayout::kRefColumnWidth), l.height / ScreenLayout::kRefColumnHeight);
    l.uiScale = l.scale;
    const float colW = ScreenLayout::kRefColumnWidth * l.scale;
    const float colH = ScreenLayout::kRefColumnHeight * l.scale;
    const float x0 = (l.width - 4.0f * colW) * 0.5f;
    const float y0 = (l.height - colH) * 0.5f;
    l.area = {x0, y0, 4.0f * colW, colH};
    for (std::size_t i = 0; i < 4; ++i) {
        l.columns[i] = {x0 + colW * static_cast<float>(i), y0, colW, colH};
        l.circleCenter[i] = {l.columns[i].x + colW * 0.5f, y0 + ScreenLayout::kCircleY * l.scale};
    }
    return l;
}

} // namespace spindle::render
