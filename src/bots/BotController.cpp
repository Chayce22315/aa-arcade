#include "bots/BotController.hpp"

#include <algorithm>
#include <cmath>

#include "gameplay/Geometry.hpp"

namespace spindle::bots {

BotController::BotController(std::unique_ptr<BotBrain> brain) : brain_(std::move(brain)) {
    delayTicks_ = std::clamp(static_cast<int>(std::lround(brain_->profile().reaction * gameplay::geom::kTickRate)), 0,
                             gameplay::geom::kTickRate / 2);
    history_.resize(static_cast<std::size_t>(delayTicks_) + 1);
}

void BotController::beginRound(bool blind) {
    blind_ = blind;
    head_ = 0;
    filled_ = 0;
    brain_->resetMemory();
}

int BotController::think(const gameplay::RotorState& rotor, const gameplay::Playfield& field, double roundTime, double timeLeft,
                         std::uint64_t tick) {
    if (field.status() != gameplay::FieldStatus::Playing) return 0;
    constexpr double dtTick = gameplay::geom::kTickSeconds;
    Snapshot& now = history_[head_];
    now.angle = rotor.angle;
    now.speed = rotor.speed;
    now.accel = rotor.accel;
    now.attached.assign(field.attachedAngles().begin(), field.attachedAngles().end()); // reuses the slot's capacity
    now.revision = field.revision();
    now.needlesLeft = field.needlesLeft();
    now.inFlight = field.inFlight();
    now.onFire = field.onFire(tick);
    now.fireLeft = now.onFire ? static_cast<double>(field.fireUntilTick() - tick) * dtTick : 0.0;
    now.combo = field.combo();
    now.chainLeft = static_cast<double>(field.chainTicksLeft(tick)) * dtTick;
    head_ = (head_ + 1) % history_.size();
    filled_ = std::min(filled_ + 1, history_.size());

    // Oldest stored entry is delayTicks_ old once the buffer is full; before that the bot sees the
    // start of the round (it cannot see more recent than its reaction time allows).
    const std::size_t oldest = (filled_ < history_.size()) ? 0 : head_;
    const Snapshot& seen = history_[oldest];
    const double delay = static_cast<double>(filled_ - 1) * gameplay::geom::kTickSeconds;

    BotView v;
    v.angle = seen.angle;
    v.speed = seen.speed;
    v.accel = seen.accel;
    v.attached = seen.attached;
    v.revision = seen.revision;
    v.needlesLeft = seen.needlesLeft;
    v.inFlight = seen.inFlight;
    const auto& cfg = field.config();
    v.flightTime = (cfg.launchDistance - cfg.needleLength) / field.needleSpeed(); // swift makes it shorter
    v.fireEnabled = cfg.fireEnabled;
    v.blind = blind_;
    v.onFire = seen.onFire;
    v.fireLeft = seen.fireLeft;
    v.combo = seen.combo;
    v.comboTarget = cfg.fireCombo;
    v.chainLeft = seen.chainLeft;
    v.closeCall = gameplay::kCloseCallFactor * field.minSeparation();
    v.minSeparation = field.minSeparation();
    v.attachAngle = gameplay::geom::kAttachAngle;
    v.fireReady = field.canFireAt(tick);
    pickupScratch_.clear();
    for (const auto& p : field.pickups())
        if (!p.claimed) pickupScratch_.push_back(p.pickup.angle);
    v.pickups = pickupScratch_;
    v.claimAngle = field.claimAngle();
    v.roundTime = roundTime;
    v.timeLeft = timeLeft;
    v.delay = delay;
    v.dt = dtTick;
    return brain_->decide(v) ? 1 : 0;
}

} // namespace spindle::bots
