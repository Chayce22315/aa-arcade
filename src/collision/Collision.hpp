#pragma once

#include <cstddef>
#include <optional>
#include <span>

// Pure collision math for one station. Angles are radians, clockwise on screen (y down).
namespace spindle::collision {

// Wraps to (-pi, pi].
double wrapAngle(double a);
// Wraps to [0, 2pi).
double wrapPositive(double a);
// Smallest absolute angle between two directions, in [0, pi].
double angularDistance(double a, double b);

// Smallest angular gap two balls of radius r on a circle of radius L can have without touching:
// chord 2 L sin(d/2) = 2r  ->  d = 2 asin(r / L). Requires 0 < r < L.
double minSeparation(double needleLength, double ballRadius);

// Index of an attached needle whose ball would touch a new ball attached at relative angle
// newAngle (strictly closer than minSep), or nullopt.
std::optional<std::size_t> findAttachConflict(double newAngle, std::span<const double> attached, double minSep);

// A ball in flight sits on the vertical below the circle centre at distance flightDistance.
// Attached ball i is at world angle attached[i] + rotorAngle on radius needleLength.
// Returns the first attached ball overlapping the flying ball (centre distance < 2r).
std::optional<std::size_t> findFlightConflict(double flightDistance, double rotorAngle, std::span<const double> attached,
                                              double needleLength, double ballRadius);

// Number of sub-steps so that neither the flying ball nor any attached ball moves more than r/4
// between two checks. Clamped to [1, 256].
int substepsFor(double travel, double rotation, double needleLength, double ballRadius);

} // namespace spindle::collision
