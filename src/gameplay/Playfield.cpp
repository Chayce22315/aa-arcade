#include "gameplay/Playfield.hpp"

#include <algorithm>
#include <cmath>

#include "collision/Collision.hpp"
#include "gameplay/Geometry.hpp"

namespace spindle::gameplay {

namespace {
constexpr std::size_t kBurnHistory = 16;
}

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
    const std::size_t firstJunk = level.prepins.size() - std::min(level.prepins.size(), static_cast<std::size_t>(std::max(0, level.junk)));
    for (std::size_t i = 0; i < level.prepins.size(); ++i) {
        const double a = collision::wrapAngle(level.prepins[i]);
        attached_.push_back({a, true, i >= firstJunk, 0});
        angles_.push_back(a);
    }
    ballRadius_ = config.ballRadius;
    minSeparation_ = config.minSeparation;
    needleSpeed_ = config.needleSpeed;
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
    flying_.push_back({config_.launchDistance, queue_, onFire(tick)});
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
    combo_ = 0;
}

void Playfield::failTimeout(std::uint64_t tick) {
    if (status_ != FieldStatus::Playing) return;
    status_ = FieldStatus::Failed;
    failReason_ = FailReason::Timeout;
    failTick_ = tick;
    combo_ = 0;
}

void Playfield::stopForClock(std::uint64_t tick) {
    if (status_ != FieldStatus::Playing) return;
    status_ = FieldStatus::Failed;
    failReason_ = FailReason::Clock;
    failTick_ = tick;
    flying_.clear();
}

bool Playfield::absorb(std::uint64_t tick, std::size_t flyingIndex) {
    if (!shield_) return false;
    shield_ = false;
    shieldBreakTick_ = tick;
    ++newShieldBreaks_;
    combo_ = 0; // a touch breaks the chain even when the shield saves the round
    flying_.erase(flying_.begin() + static_cast<std::ptrdiff_t>(flyingIndex));
    ++queue_; // the needle bounces back to the launcher and still has to be placed
    return true;
}

void Playfield::burn(std::size_t attachedIndex, std::uint64_t tick) {
    burns_.push_back({angles_[attachedIndex], tick});
    if (burns_.size() > kBurnHistory) burns_.erase(burns_.begin());
    attached_.erase(attached_.begin() + static_cast<std::ptrdiff_t>(attachedIndex));
    angles_.erase(angles_.begin() + static_cast<std::ptrdiff_t>(attachedIndex));
    ++newBurns_;
    ++revision_;
}

void Playfield::ignite(std::uint64_t tick) {
    if (!config_.fireEnabled) return;
    fireStart_ = tick;
    fireUntil_ = tick + static_cast<std::uint64_t>(std::max(1, config_.fireTicks));
    combo_ = 0;
    ++newIgnitions_;
}

double Playfield::comboFill() const {
    if (config_.fireCombo <= 0) return 0.0;
    return std::clamp(static_cast<double>(combo_) / config_.fireCombo, 0.0, 1.0);
}

int Playfield::chainTicksLeft(std::uint64_t tick) const {
    if (combo_ <= 0 || !lastPlaceTick_) return 0;
    const auto end = *lastPlaceTick_ + static_cast<std::uint64_t>(config_.chainWindowTicks);
    return end > tick ? static_cast<int>(end - tick) : 0;
}

void Playfield::setCombo(int combo) { combo_ = config_.fireEnabled ? std::clamp(combo, 0, std::max(0, config_.fireCombo - 1)) : 0; }

void Playfield::scorePlacement(double nearestGap, std::uint64_t tick) {
    const bool closeCall = nearestGap < kCloseCallFactor * minSeparation_;
    newPlacements_.push_back({closeCall, comboFill(), onFire(tick), config_.fireEnabled});
    if (config_.fireEnabled && !onFire(tick)) {
        const int points = closeCall ? 2 : 1;
        const bool chained = lastPlaceTick_ && tick <= *lastPlaceTick_ + static_cast<std::uint64_t>(config_.chainWindowTicks);
        combo_ = chained ? combo_ + points : points;
        if (combo_ >= config_.fireCombo) ignite(tick);
    }
    lastPlaceTick_ = tick;
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
        case PowerupKind::Heat: ignite(tick); break;
        case PowerupKind::Purge:
            for (int n = 0; n < powerup::kPurgeCount; ++n) {
                std::optional<std::size_t> nearest;
                for (std::size_t i = 0; i < attached_.size(); ++i) {
                    if (!attached_[i].preplaced) continue;
                    if (!nearest || collision::angularDistance(relAngle, angles_[i]) < collision::angularDistance(relAngle, angles_[*nearest]))
                        nearest = i;
                }
                if (!nearest) break;
                burn(*nearest, tick);
            }
            break;
        case PowerupKind::Swift:
            swift_ = true;
            needleSpeed_ = config_.needleSpeed * powerup::kSwiftFactor;
            break;
        }
        return; // rings are spaced so one needle can take at most one
    }
}

void Playfield::step(std::uint64_t tick, double dt, double angleBefore, double angleDelta) {
    if (status_ != FieldStatus::Playing) return;
    if (combo_ > 0 && chainTicksLeft(tick) == 0) combo_ = 0; // too slow: the chain is broken
    if (!flying_.empty()) {
        const double travel = needleSpeed_ * dt;
        const int n = collision::substepsFor(travel, angleDelta, config_.needleLength, ballRadius_);
        const double travelStep = travel / n;
        for (int s = 1; s <= n; ++s) {
            const double angle = angleBefore + angleDelta * (static_cast<double>(s) / n);
            // flying_ is ordered by launch time, so the front ball is always the highest one.
            for (std::size_t i = 0; i < flying_.size();) {
                const double next = flying_[i].distance - travelStep;
                const bool fireNeedle = flying_[i].fire;
                if (next <= config_.needleLength) {
                    const double rel = collision::wrapAngle(geom::kAttachAngle - angle);
                    if (fireNeedle) {
                        while (auto hit = collision::findAttachConflict(rel, angles_, minSeparation_)) burn(*hit, tick);
                    } else if (auto hit = collision::findAttachConflict(rel, angles_, minSeparation_)) {
                        if (absorb(tick, i)) continue;
                        flying_[i].distance = config_.needleLength;
                        failCollision(tick, *hit, i);
                        return;
                    }
                    double nearest = geom::kPi;
                    for (double a : angles_) nearest = std::min(nearest, collision::angularDistance(rel, a));
                    attached_.push_back({rel, false, false, tick});
                    angles_.push_back(rel);
                    ++placed_;
                    ++revision_;
                    flying_.erase(flying_.begin() + static_cast<std::ptrdiff_t>(i));
                    scorePlacement(nearest, tick);
                    claimAt(rel, tick);
                    continue;
                }
                if (fireNeedle) {
                    while (auto hit = collision::findFlightConflict(next, angle, angles_, config_.needleLength, ballRadius_)) burn(*hit, tick);
                } else if (auto hit = collision::findFlightConflict(next, angle, angles_, config_.needleLength, ballRadius_)) {
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

std::vector<scoring::Placement> Playfield::takePlacements() {
    std::vector<scoring::Placement> out;
    out.swap(newPlacements_);
    return out;
}

int Playfield::takeIgnitions() {
    const int n = newIgnitions_;
    newIgnitions_ = 0;
    return n;
}

int Playfield::takeBurns() {
    const int n = newBurns_;
    newBurns_ = 0;
    return n;
}

} // namespace spindle::gameplay
