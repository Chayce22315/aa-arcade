#pragma once

#include "arcade/ArcadeMachine.hpp"
#include "render/Layout.hpp"
#include "render/Renderer2D.hpp"

namespace spindle::menus {

struct ScreenContext {
    render::Renderer2D& r;
    const render::ScreenLayout& layout;
    const arcade::ArcadeMachine& arcade;
    double time = 0.0;      // seconds since boot, for blinking and idle animation
    double subTick = 0.0;   // seconds since the last simulation tick (rotation extrapolation)
    double exitHold = 0.0;  // seconds the exit key has been held
    double exitHoldLimit = 0.0;
};

// Draws the whole cabinet screen for the current arcade state (everything procedural).
void drawArcade(const ScreenContext& ctx);

} // namespace spindle::menus
