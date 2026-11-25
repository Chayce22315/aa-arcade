#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gameplay/Rotor.hpp"

namespace spindle::gameplay {

// Everything that defines a round. Every alive station receives the identical LevelDef.
struct LevelDef {
    int round = 1;
    std::vector<double> prepins; // relative angles (radians) of needles already in the circle
    int launchCount = 0;         // needles each player must place
    RotationProgram rotation;    // before Gameplay.rotation_speed_scale is applied
};

struct LevelLimits {
    double minSeparation = 0.0;  // radians, from collision::minSeparation
    int maxLaunch = 40;
    int maxPrepins = 24;
    int maxSegments = 32;
    double maxSpeed = 12.0;       // rad/s, before the operator scale
    double minActiveSpeed = 0.2;  // rad/s: some segment must actually turn the circle
    double maxRampTime = 10.0;    // s
    double maxHoldTime = 30.0;    // s
    double minSegmentTime = 0.05; // s, ramp + hold
    double packingMargin = 1.15;  // (prepins + launch) * minSeparation * margin must fit in 2 pi
    double prepinMargin = 1.25;   // pre-placed needles keep this multiple of minSeparation apart
};

LevelLimits defaultLimits();

// nullopt when the level is playable within the limits, otherwise a description of the first problem.
std::optional<std::string> validateLevel(const LevelDef& level, const LevelLimits& limits);

// Interface the match uses to obtain rounds; LevelScript implements it with Lua.
class LevelProvider {
public:
    virtual ~LevelProvider() = default;
    virtual LevelDef level(int round, std::uint32_t seed) = 0;
};

} // namespace spindle::gameplay
