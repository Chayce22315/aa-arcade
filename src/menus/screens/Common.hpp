#pragma once

// Shared pieces of the cabinet screens (internal to src/menus/screens). Everything is drawn with
// lines, circles and the stroke font; sizes are reference pixels of a 1920 x 1080 screen.

#include <cstdint>
#include <numbers>
#include <string>
#include <string_view>

#include "gameplay/Geometry.hpp"
#include "gameplay/Match.hpp"
#include "menus/Screens.hpp"

namespace spindle::menus::screens {

using render::Align;
using render::Color;
using render::Rect;
using render::Vec2;
namespace colors = render::colors;
namespace geom = gameplay::geom;

inline constexpr float kTau = 2.0f * std::numbers::pi_v<float>;
inline constexpr Color kFire{1.0f, 0.56f, 0.14f, 1.0f};
inline constexpr Color kFireHot{1.0f, 0.86f, 0.5f, 1.0f};
inline constexpr Color kMissRed{1.0f, 0.45f, 0.45f, 1.0f};
inline constexpr Color kGold{1.0f, 0.82f, 0.3f, 1.0f};

// ------------------------------------------------------------------ basics
bool blink(double t, double period = 1.0);
float px(const ScreenContext& c, float ref);
void textAt(const ScreenContext& c, std::string_view s, float x, float y, float cap, Color col, Align a = Align::Center);
float midX(const ScreenContext& c);           // centre of the station area, for full-width messages
float areaY(const ScreenContext& c, float ref);
const char* startPrompt(const ScreenContext& c); // coins on a cabinet, a tap on a phone
void creditLine(const ScreenContext& c);
void dividers(const ScreenContext& c);
void exitHint(const ScreenContext& c);
double ticksToSeconds(std::uint64_t from, std::uint64_t to);
float easeOut(float t);   // 0..1 -> 0..1, fast start
float easeInOut(float t); // 0..1 -> 0..1, smooth both ends
Color hsv(float hueTurns, float saturation, float value, float alpha = 1.0f);
// A circular arc from angle `from` to `to` (radians, screen convention: 0 = right, clockwise).
void arc(render::Renderer2D& r, Vec2 centre, float radius, float from, float to, float thickness, Color col);
// A turning rainbow band around a circle (the crown holder's core).
void rainbowRim(const ScreenContext& c, Vec2 centre, float radius, float thickness);
// The largest cap height at which `s` fits in `width` (never above maxCap).
float fitCap(std::string_view s, float width, float maxCap);

// ------------------------------------------------------------------ station pieces
struct StationLook {
    Color fg = colors::kWhite;
    bool rainbow = false; // the crown holder's circle
};
// The colour of a needle or ball at `angle` on the circle (rainbow for the crown holder).
Color lookColor(const StationLook& look, float angle, double time);

void drawBall(const ScreenContext& c, Vec2 p, float radius, int number, Color fg, bool showNumber);
void drawLabelBall(const ScreenContext& c, Vec2 p, float radius, std::string_view label, Color fg); // a ball carrying a digit or letter
void drawPowerupIcon(render::Renderer2D& r, gameplay::PowerupKind kind, Vec2 c, float radius, Color col);
const char* powerupBlurb(gameplay::PowerupKind kind);
const char* powerupLabel(gameplay::PowerupKind kind);
void flame(const ScreenContext& c, Vec2 base, float w, float h, float angle, Color tint);
void decorativeCircle(const ScreenContext& c, Vec2 centre, float scale, double t);
void drawStationHeader(const ScreenContext& c, int s, std::string_view kind, Color col);
void drawStationStatus(const ScreenContext& c, int s, std::string_view text, Color col, float cap = 30);
// A stroke-drawn crown; `base` is the middle of its band, `size` its width in screen pixels.
void drawCrown(const ScreenContext& c, Vec2 base, float size, Color col);
Vec2 crownSpot(const ScreenContext& c, int s); // where a station's crown sits, next to its header
// A game mode's mark, drawn with lines inside a circle of `radius` (screen pixels).
void drawModeIcon(const ScreenContext& c, gameplay::ModeId mode, Vec2 centre, float radius, Color col);
// A rectangle outline.
void frame(const ScreenContext& c, const Rect& r, float thickness, Color col);

// ------------------------------------------------------------------ screens (one file each)
void drawMatch(const ScreenContext& c, const gameplay::Match& m, bool demo);       // Field.cpp
void drawTallyStation(const ScreenContext& c, const gameplay::Match& m, int s);    // Tally.cpp
void drawCrownLayer(const ScreenContext& c, const gameplay::Match& m);             // Tally.cpp
void drawInitialsColumn(const ScreenContext& c, int s);                            // Initials.cpp
void drawTitle(const ScreenContext& c);                                             // Attract.cpp
void drawHowTo(const ScreenContext& c);
void drawHighScores(const ScreenContext& c);
void drawInsertCoin(const ScreenContext& c);
void drawVote(const ScreenContext& c);                                              // Lobby.cpp
void drawInitialsState(const ScreenContext& c);                                     // Results.cpp
void drawGameOver(const ScreenContext& c);
void drawSuspendedOverlay(const ScreenContext& c);
void drawDisconnectOverlay(const ScreenContext& c);

} // namespace spindle::menus::screens
