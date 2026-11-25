#include "gameplay/Playfield.hpp"

#include <algorithm>
#include <cmath>

#include "collision/Collision.hpp"
#include "gameplay/Geometry.hpp"

namespace spindle::gameplay {

FieldConfig makeFieldConfig(double needleSpeedScale) {
    FieldConfig c;
    c.needleLength = geom::kNeedleLength;
    c.ballRadius = geom::kBallRadius;
    c.launchDistance = geom::kLaunchDistance;
    c.needleSpeed = geom::kBaseNeedleSpeed * std::clamp(needleSpeedScale, 0.5, 2.0);
    c.minSeparation = collision::minSeparation(c.needleLength, c.ballRadius);
    const double seconds = (2.0 * c.ballRadius) / c.needleSpeed;
    c.minFireIntervalTicks = std::max(1, static_cast<int>(std::ceil(seconds * geom::kTickRate)));
    return c;
}

void Playfield::reset() { *this = Playfield{}; }

void Playfield::begin(const LevelDef& level, const FieldConfig& config) {
    reset();
    config_ = config;
    status_ = FieldStatus::Playing;
    launchCount_ = level.launchCount;
    queue_ = level.launchCount;
    attached_.reserve(level.prepins.size() + static_cast<std::size_t>(level.launchCount));
    angles_.reserve(attached_.capacity());
    for (double a : level.prepins) {
        attached_.push_back({collision::wrapAngle(a), true});
        angles_.push_back(collision::wrapAngle(a));
    }
    ballRadius_ = config.ballRadius;
    minSeparation_ = config.minSeparation;
    claimAngle_ = config.minSeparation * powerup::kClaimFactor;
    for (const Pickup& p : level.pickups) pickups_.push_back({{collision::wrapAngle(p.angle), p.kind}, false});
}

bool Playfield::canFireAt(std::uint64_t tick) const {
    if (status_ != FieldStatus::Playing || queue_ <= 0) return false;
    if (!lastFireTick_) return true;
    return tick >= *lastFireTick_ + static_cast<std::uint64_t>(config_.minFireIntervalTicks);
}

bool Playfield::fire(std::uint64_t tick) {
    if (!canFireAt(tick)) return false;
    flying_.push_back({config_.launchDistance, queue_});
    --queue_;
    lastFireTick_ = tick;
    return true;
}

void Playfield::failCollision(std::uint64_t tick, std::size_t attachedIndex, std::size_t flyingIndex) {
    status_ = FieldStatus::Failed;
    failReason_ = FailReason::Collision;
    failTick_ = tick;
    conflictAttached_ = attachedIndex;
    conflictFlying_ = flyingIndex;
}

void Playfield::failTimeout(std::uint64_t tick) {
    if (status_ != FieldStatus::Playing) return;
    status_ = FieldStatus::Failed;
    failReason_ = FailReason::Timeout;
    failTick_ = tick;
}

bool Playfield::absorb(std::uint64_t tick, std::size_t flyingIndex) {
    if (!shield_) return false;
    shield_ = false;
    shieldBreakTick_ = tick;
    ++newShieldBreaks_;
    flying_.erase(flying_.begin() + static_cast<std::ptrdiff_t>(flyingIndex));
    ++queue_; // the needle bounces back to the launcher and still has to be placed
    return true;
}

void Playfield::claimAt(double relAngle, std::uint64_t tick) {
    for (PickupState& ps : pickups_) {
        if (ps.claimed || collision::angularDistance(relAngle, ps.pickup.angle) > claimAngle_) continue;
        ps.claimed = true;
        ++claimed_;
        lastClaim_ = ps.pickup.kind;
        lastClaimTick_ = tick;
        newClaims_.push_back(ps.pickup.kind);
        switch (ps.pickup.kind) {
        case PowerupKind::Shield: shield_ = true; break;
        case PowerupKind::Skip: queue_ = std::max(0, queue_ - 1); break;
        case PowerupKind::Slim:
            if (!slim_) {
                slim_ = true;
                ballRadius_ = config_.ballRadius * powerup::kSlimFactor;
                minSeparation_ = collision::minSeparation(config_.needleLength, ballRadius_);
            }
            break;
        case PowerupKind::Mend: break; // tries belong to the match
        }
        return; // rings are spaced so one needle can take at most one
    }
}

void Playfield::step(std::uint64_t tick, double dt, double angleBefore, double angleDelta) {
    if (status_ != FieldStatus::Playing) return;
    if (!flying_.empty()) {
        const double travel = config_.needleSpeed * dt;
        const int n = collision::substepsFor(travel, angleDelta, config_.needleLength, ballRadius_);
        const double travelStep = travel / n;
        for (int s = 1; s <= n; ++s) {
            const double angle = angleBefore + angleDelta * (static_cast<double>(s) / n);
            // flying_ is ordered by launch time, so the front ball is always the highest one.
            for (std::size_t i = 0; i < flying_.size();) {
                const double next = flying_[i].distance - travelStep;
                if (next <= config_.needleLength) {
                    const double rel = collision::wrapAngle(geom::kAttachAngle - angle);
                    if (auto hit = collision::findAttachConflict(rel, angles_, minSeparation_)) {
                        if (absorb(tick, i)) continue;
                        flying_[i].distance = config_.needleLength;
                        failCollision(tick, *hit, i);
                        return;
                    }
                    attached_.push_back({rel, false});
                    angles_.push_back(rel);
                    ++placed_;
                    flying_.erase(flying_.begin() + static_cast<std::ptrdiff_t>(i));
                    claimAt(rel, tick);
                    continue;
                }
                if (auto hit = collision::findFlightConflict(next, angle, angles_, config_.needleLength, ballRadius_)) {
                    if (absorb(tick, i)) continue;
                    flying_[i].distance = next;
                    failCollision(tick, *hit, i);
                    return;
                }
                flying_[i].distance = next;
                ++i;
            }
        }
    }
    if (queue_ == 0 && flying_.empty()) {
        status_ = FieldStatus::Cleared;
        clearTick_ = tick;
    }
}

std::vector<PowerupKind> Playfield::takeClaims() {
    std::vector<PowerupKind> out;
    out.swap(newClaims_);
    return out;
}

int Playfield::takeShieldBreaks() {
    const int n = newShieldBreaks_;
    newShieldBreaks_ = 0;
    return n;
}

} // namespace spindle::gameplay
