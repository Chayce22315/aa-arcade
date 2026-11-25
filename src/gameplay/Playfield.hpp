#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "gameplay/LevelDef.hpp"
#include "gameplay/Powerups.hpp"

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

    // Fire meter (docs/RULES.md). Off unless the match turns it on.
    bool fireEnabled = false;
    int fireCombo = 8;          // combo points that set the station on fire
    int fireTicks = 360;        // how long the station burns (1.5 s)
    int chainWindowTicks = 240; // a placement this soon after the previous one keeps the chain (1 s)
};

// Builds the field configuration from the fixed geometry and the operator speed scale.
FieldConfig makeFieldConfig(double needleSpeedScale);

// A needle landing closer than this multiple of the minimum gap to its nearest neighbour is a close
// call and counts double on the fire meter.
inline constexpr double kCloseCallFactor = 1.4;

struct AttachedNeedle {
    double angle = 0.0; // relative to the rotor
    bool preplaced = false;
};

struct FlyingNeedle {
    double distance = 0.0; // below the circle centre
    int number = 0;        // the countdown number it carried in the queue
    bool fire = false;     // launched while the station was on fire: burns through other needles
};

struct PickupState {
    Pickup pickup;
    bool claimed = false;
};

// A needle burnt away (by a fire needle or a purge), kept briefly for the burst effect.
struct BurnMark {
    double angle = 0.0; // relative to the rotor
    std::uint64_t tick = 0;
};

// One station's circle: its own needle placements, needles in flight, collision checks, powerups and
// fire meter. It never owns rotation; each tick it is handed the shared rotor's motion.
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
    std::uint64_t clearTick() const { return clearTick_; }
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

    // Powerups. Ball radius, minimum separation and needle speed are the effective values.
    const std::vector<PickupState>& pickups() const { return pickups_; }
    double claimAngle() const { return claimAngle_; }
    double ballRadius() const { return ballRadius_; }
    double minSeparation() const { return minSeparation_; }
    double needleSpeed() const { return needleSpeed_; }
    bool shieldArmed() const { return shield_; }
    bool slim() const { return slim_; }
    bool swift() const { return swift_; }
    std::optional<PowerupKind> lastClaim() const { return lastClaim_; }
    std::uint64_t lastClaimTick() const { return lastClaimTick_; }
    std::uint64_t shieldBreakTick() const { return shieldBreakTick_; }
    int claimedThisRound() const { return claimed_; }

    // Fire meter.
    int combo() const { return combo_; }
    double comboFill() const;                      // 0..1 of the way to catching fire
    bool onFire(std::uint64_t tick) const { return fireUntil_ > tick; }
    std::uint64_t fireStartTick() const { return fireStart_; }
    std::uint64_t fireUntilTick() const { return fireUntil_; }
    // Ticks left before the chain breaks (0 when there is no chain).
    int chainTicksLeft(std::uint64_t tick) const;
    // The chain clock starts when the round starts; the match calls this then.
    void startChain(std::uint64_t tick) { lastPlaceTick_ = tick; }
    // A combo banked by clearing the previous round.
    void setCombo(int combo);
    const std::vector<BurnMark>& burns() const { return burns_; }
    // Changes whenever the set of attached needles changes (a placement or a burn).
    std::uint32_t revision() const { return revision_; }

    // Claims, shield breaks, ignitions and burns since the last call (the match turns them into events).
    std::vector<PowerupKind> takeClaims();
    int takeShieldBreaks();
    int takeIgnitions();
    int takeBurns();

private:
    void failCollision(std::uint64_t tick, std::size_t attachedIndex, std::size_t flyingIndex);
    // A touch by a normal needle: returns true if a shield absorbed it (needle removed and re-queued).
    bool absorb(std::uint64_t tick, std::size_t flyingIndex);
    void claimAt(double relAngle, std::uint64_t tick);
    void burn(std::size_t attachedIndex, std::uint64_t tick);
    void ignite(std::uint64_t tick);
    void scorePlacement(double nearestGap, std::uint64_t tick);

    FieldConfig config_{};
    FieldStatus status_ = FieldStatus::Idle;
    FailReason failReason_ = FailReason::None;
    std::uint64_t failTick_ = 0;
    std::uint64_t clearTick_ = 0;
    std::vector<AttachedNeedle> attached_;
    std::vector<double> angles_; // mirror of attached_[i].angle for the collision functions
    std::vector<FlyingNeedle> flying_;
    int queue_ = 0;
    int launchCount_ = 0;
    int placed_ = 0;
    std::optional<std::uint64_t> lastFireTick_;
    std::optional<std::size_t> conflictAttached_;
    std::optional<std::size_t> conflictFlying_;

    std::vector<PickupState> pickups_;
    double claimAngle_ = 0.0;
    double ballRadius_ = 0.0;
    double minSeparation_ = 0.0;
    double needleSpeed_ = 0.0;
    bool shield_ = false;
    bool slim_ = false;
    bool swift_ = false;
    int claimed_ = 0;
    std::optional<PowerupKind> lastClaim_;
    std::uint64_t lastClaimTick_ = 0;
    std::uint64_t shieldBreakTick_ = 0;
    std::vector<PowerupKind> newClaims_;
    int newShieldBreaks_ = 0;

    int combo_ = 0;
    std::optional<std::uint64_t> lastPlaceTick_;
    std::uint64_t fireStart_ = 0;
    std::uint64_t fireUntil_ = 0;
    std::vector<BurnMark> burns_;
    int newIgnitions_ = 0;
    int newBurns_ = 0;
    std::uint32_t revision_ = 0;
};

} // namespace spindle::gameplay
