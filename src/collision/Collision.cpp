#include "collision/Collision.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace spindle::collision {

namespace {
constexpr double kPi = std::numbers::pi;
constexpr double kTwoPi = 2.0 * std::numbers::pi;
} // namespace

double wrapAngle(double a) {
    if (!std::isfinite(a)) return 0.0;
    double w = std::fmod(a + kPi, kTwoPi);
    if (w < 0.0) w += kTwoPi;
    w -= kPi;
    return (w <= -kPi) ? kPi : w;
}

double wrapPositive(double a) {
    if (!std::isfinite(a)) return 0.0;
    double w = std::fmod(a, kTwoPi);
    if (w < 0.0) w += kTwoPi;
    return (w >= kTwoPi) ? 0.0 : w;
}

double angularDistance(double a, double b) { return std::fabs(wrapAngle(a - b)); }

double minSeparation(double needleLength, double ballRadius) {
    if (!(needleLength > 0.0) || !(ballRadius > 0.0) || ballRadius >= needleLength) return kPi;
    return 2.0 * std::asin(ballRadius / needleLength);
}

std::optional<std::size_t> findAttachConflict(double newAngle, std::span<const double> attached, double minSep) {
    for (std::size_t i = 0; i < attached.size(); ++i) {
        if (angularDistance(newAngle, attached[i]) < minSep) return i;
    }
    return std::nullopt;
}

std::optional<std::size_t> findFlightConflict(double flightDistance, double rotorAngle, std::span<const double> attached,
                                              double needleLength, double ballRadius) {
    const double contact = 2.0 * ballRadius;
    const double contactSq = contact * contact;
    // Flying ball centre relative to circle centre: (0, flightDistance).
    for (std::size_t i = 0; i < attached.size(); ++i) {
        const double a = attached[i] + rotorAngle;
        const double dx = needleLength * std::cos(a);
        const double dy = needleLength * std::sin(a) - flightDistance;
        if (dx * dx + dy * dy < contactSq) return i;
    }
    return std::nullopt;
}

int substepsFor(double travel, double rotation, double needleLength, double ballRadius) {
    const double step = ballRadius / 4.0;
    if (!(step > 0.0)) return 1;
    const double movement = std::max(std::fabs(travel), std::fabs(rotation) * needleLength);
    if (!std::isfinite(movement)) return 256;
    const double n = std::ceil(movement / step);
    return static_cast<int>(std::clamp(n, 1.0, 256.0));
}

} // namespace spindle::collision
