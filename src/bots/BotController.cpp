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

void BotController::beginRound() {
    head_ = 0;
    filled_ = 0;
    brain_->resetMemory();
}

int BotController::think(const gameplay::RotorState& rotor, const gameplay::Playfield& field, double roundTime,
                         std::uint64_t tick) {
    if (field.status() != gameplay::FieldStatus::Playing) return 0;
    Snapshot now;
    now.angle = rotor.angle;
    now.speed = rotor.speed;
    now.accel = rotor.accel;
    now.attachedCount = static_cast<int>(field.attachedAngles().size());
    now.needlesLeft = field.needlesLeft();
    now.inFlight = field.inFlight();
    history_[head_] = now;
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
    // Attached needles only accumulate within a round, so the delayed set is a prefix.
    v.attached = std::span<const double>(field.attachedAngles().data(),
                                         static_cast<std::size_t>(std::min<int>(seen.attachedCount,
                                                                                static_cast<int>(field.attachedAngles().size()))));
    v.needlesLeft = seen.needlesLeft;
    v.inFlight = seen.inFlight;
    const auto& cfg = field.config();
    v.flightTime = (cfg.launchDistance - cfg.needleLength) / cfg.needleSpeed;
    v.minSeparation = cfg.minSeparation;
    v.attachAngle = gameplay::geom::kAttachAngle;
    v.fireReady = field.canFireAt(tick);
    v.roundTime = roundTime;
    v.delay = delay;
    v.dt = gameplay::geom::kTickSeconds;
    return brain_->decide(v) ? 1 : 0;
}

} // namespace spindle::bots
