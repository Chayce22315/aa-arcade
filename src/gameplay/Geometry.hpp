#pragma once

#include <numbers>

// Playfield geometry in field units. Field units are the pixels of the 480 x 1080 reference column;
// render/Layout scales them to the real display. Screen y points down, angles are radians measured
// clockwise on screen from +x, so a needle attaches at the bottom of the circle: kAttachAngle = pi/2.
namespace spindle::gameplay::geom {

inline constexpr double kPi = std::numbers::pi;
inline constexpr double kTwoPi = 2.0 * std::numbers::pi;

inline constexpr double kCoreRadius = 46.0;
inline constexpr double kNeedleLength = 150.0;   // circle centre to attached ball centre (L)
inline constexpr double kBallRadius = 11.0;      // r
inline constexpr double kLaunchDistance = 300.0; // circle centre to the ball waiting at the launcher
inline constexpr double kQueueSpacing = 30.0;    // spacing of the waiting balls below the launcher
inline constexpr double kAttachAngle = kPi / 2.0;

// Uncalibrated: tune by feel. Scaled by Gameplay.needle_speed_scale.
inline constexpr double kBaseNeedleSpeed = 1500.0; // field units per second

inline constexpr int kTickRate = 240;
inline constexpr double kTickSeconds = 1.0 / kTickRate;

} // namespace spindle::gameplay::geom
