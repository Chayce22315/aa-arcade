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

void Playfield::step(std::uint64_t tick, double dt, double angleBefore, double angleDelta) {
    if (status_ != FieldStatus::Playing) return;
    if (!flying_.empty()) {
        const double travel = config_.needleSpeed * dt;
        const int n = collision::substepsFor(travel, angleDelta, config_.needleLength, config_.ballRadius);
        const double travelStep = travel / n;
        for (int s = 1; s <= n; ++s) {
            const double angle = angleBefore + angleDelta * (static_cast<double>(s) / n);
            // flying_ is ordered by launch time, so the front ball is always the highest one.
            for (std::size_t i = 0; i < flying_.size();) {
                const double next = flying_[i].distance - travelStep;
                if (next <= config_.needleLength) {
                    const double rel = collision::wrapAngle(geom::kAttachAngle - angle);
                    if (auto hit = collision::findAttachConflict(rel, angles_, config_.minSeparation)) {
                        flying_[i].distance = config_.needleLength;
                        failCollision(tick, *hit, i);
                        return;
                    }
                    attached_.push_back({rel, false});
                    angles_.push_back(rel);
                    ++placed_;
                    flying_.erase(flying_.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }
                if (auto hit = collision::findFlightConflict(next, angle, angles_, config_.needleLength, config_.ballRadius)) {
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

} // namespace spindle::gameplay
