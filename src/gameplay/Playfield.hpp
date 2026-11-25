#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "gameplay/LevelDef.hpp"

namespace spindle::gameplay {

enum class FieldStatus { Idle, Playing, Cleared, Failed };
enum class FailReason { None, Collision, Timeout };

struct FieldConfig {
    double needleLength = 0.0;
    double ballRadius = 0.0;
    double launchDistance = 0.0;
    double needleSpeed = 0.0;      // field units per second
    double minSeparation = 0.0;    // radians
    int minFireIntervalTicks = 1;  // so balls in flight can never overlap each other
};

// Builds the field configuration from the fixed geometry and the operator speed scale.
FieldConfig makeFieldConfig(double needleSpeedScale);

struct AttachedNeedle {
    double angle = 0.0; // relative to the rotor
    bool preplaced = false;
};

struct FlyingNeedle {
    double distance = 0.0; // below the circle centre
    int number = 0;        // the countdown number it carried in the queue
};

// One station's circle: its own needle placements, needles in flight and collision checks.
// It never owns rotation; each tick it is handed the shared rotor's motion.
class Playfield {
public:
    void reset();
    void begin(const LevelDef& level, const FieldConfig& config);

    // Launches the next queued needle if the field is playing, a needle is left and the fire interval
    // has elapsed since the previous launch. Returns whether a needle was launched.
    bool fire(std::uint64_t tick);

    // Advances needles by one tick. angleBefore is the rotor angle at the start of the tick and
    // angleDelta the (signed, unwrapped) rotation during it.
    void step(std::uint64_t tick, double dt, double angleBefore, double angleDelta);

    void failTimeout(std::uint64_t tick);

    FieldStatus status() const { return status_; }
    FailReason failReason() const { return failReason_; }
    std::uint64_t failTick() const { return failTick_; }
    int placed() const { return placed_; }          // needles this player placed this round
    int needlesLeft() const { return queue_; }      // not yet launched
    int inFlight() const { return static_cast<int>(flying_.size()); }
    int launchCount() const { return launchCount_; }
    bool canFireAt(std::uint64_t tick) const;
    const std::vector<AttachedNeedle>& attached() const { return attached_; }
    const std::vector<double>& attachedAngles() const { return angles_; }
    const std::vector<FlyingNeedle>& flying() const { return flying_; }
    const FieldConfig& config() const { return config_; }
    // Indices of the two balls that touched when status() == Failed by collision.
    std::optional<std::size_t> conflictAttached() const { return conflictAttached_; }
    std::optional<std::size_t> conflictFlying() const { return conflictFlying_; }

private:
    void failCollision(std::uint64_t tick, std::size_t attachedIndex, std::size_t flyingIndex);

    FieldConfig config_{};
    FieldStatus status_ = FieldStatus::Idle;
    FailReason failReason_ = FailReason::None;
    std::uint64_t failTick_ = 0;
    std::vector<AttachedNeedle> attached_;
    std::vector<double> angles_; // mirror of attached_[i].angle for the collision functions
    std::vector<FlyingNeedle> flying_;
    int queue_ = 0;
    int launchCount_ = 0;
    int placed_ = 0;
    std::optional<std::uint64_t> lastFireTick_;
    std::optional<std::size_t> conflictAttached_;
    std::optional<std::size_t> conflictFlying_;
};

} // namespace spindle::gameplay
