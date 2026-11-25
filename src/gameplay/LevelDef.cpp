#include "gameplay/LevelDef.hpp"

#include <cmath>
#include <format>

#include "collision/Collision.hpp"
#include "gameplay/Geometry.hpp"

namespace spindle::gameplay {

LevelLimits defaultLimits() {
    LevelLimits l;
    l.minSeparation = collision::minSeparation(geom::kNeedleLength, geom::kBallRadius);
    return l;
}

std::optional<std::string> validateLevel(const LevelDef& level, const LevelLimits& limits) {
    if (level.round < 1) return std::format("round {} is not positive", level.round);
    if (level.launchCount < 1 || level.launchCount > limits.maxLaunch)
        return std::format("launch count {} outside 1..{}", level.launchCount, limits.maxLaunch);
    if (level.prepins.size() > static_cast<std::size_t>(limits.maxPrepins))
        return std::format("{} pre-placed needles exceed {}", level.prepins.size(), limits.maxPrepins);

    const double total = static_cast<double>(level.prepins.size()) + level.launchCount;
    if (total * limits.minSeparation * limits.packingMargin > geom::kTwoPi)
        return std::format("{} needles cannot fit around the circle", static_cast<int>(total));

    for (std::size_t i = 0; i < level.prepins.size(); ++i) {
        if (!std::isfinite(level.prepins[i])) return std::format("pre-placed needle {} is not finite", i);
        for (std::size_t j = 0; j < i; ++j) {
            if (collision::angularDistance(level.prepins[i], level.prepins[j]) < limits.minSeparation * limits.prepinMargin)
                return std::format("pre-placed needles {} and {} are too close", j, i);
        }
    }

    const RotationProgram& p = level.rotation;
    if (!std::isfinite(p.initialSpeed) || std::fabs(p.initialSpeed) > limits.maxSpeed)
        return std::format("initial speed {} outside +-{}", p.initialSpeed, limits.maxSpeed);
    if (p.segments.empty() || p.segments.size() > static_cast<std::size_t>(limits.maxSegments))
        return std::format("rotation needs 1..{} segments, got {}", limits.maxSegments, p.segments.size());
    double fastest = std::fabs(p.initialSpeed);
    for (std::size_t i = 0; i < p.segments.size(); ++i) {
        const RotationSegment& s = p.segments[i];
        if (!std::isfinite(s.targetSpeed) || std::fabs(s.targetSpeed) > limits.maxSpeed)
            return std::format("segment {} speed {} outside +-{}", i, s.targetSpeed, limits.maxSpeed);
        if (!std::isfinite(s.rampTime) || s.rampTime < 0.0 || s.rampTime > limits.maxRampTime)
            return std::format("segment {} ramp {} outside 0..{}", i, s.rampTime, limits.maxRampTime);
        if (!std::isfinite(s.holdTime) || s.holdTime < 0.0 || s.holdTime > limits.maxHoldTime)
            return std::format("segment {} hold {} outside 0..{}", i, s.holdTime, limits.maxHoldTime);
        if (s.rampTime + s.holdTime < limits.minSegmentTime)
            return std::format("segment {} lasts less than {} s", i, limits.minSegmentTime);
        fastest = std::max(fastest, std::fabs(s.targetSpeed));
    }
    if (fastest < limits.minActiveSpeed) return std::string("the circle never turns fast enough to place needles");

    if (level.pickups.size() > static_cast<std::size_t>(limits.maxPickups))
        return std::format("{} powerup rings exceed {}", level.pickups.size(), limits.maxPickups);
    for (std::size_t i = 0; i < level.pickups.size(); ++i) {
        const Pickup& pk = level.pickups[i];
        if (!std::isfinite(pk.angle)) return std::format("powerup ring {} is not finite", i);
        if (!contains(limits.powerups, pk.kind)) return std::format("powerup '{}' is not enabled", powerupName(pk.kind));
        for (double pin : level.prepins)
            if (collision::angularDistance(pk.angle, pin) < limits.minSeparation * limits.pickupPrepinMargin)
                return std::format("powerup ring {} sits too close to a pre-placed needle", i);
        for (std::size_t j = 0; j < i; ++j)
            if (collision::angularDistance(pk.angle, level.pickups[j].angle) < limits.minSeparation * limits.pickupSpacing)
                return std::format("powerup rings {} and {} are too close", j, i);
    }
    return std::nullopt;
}

} // namespace spindle::gameplay
