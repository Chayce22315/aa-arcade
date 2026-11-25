#include "gameplay/Match.hpp"

#include <algorithm>
#include <cmath>

#include "collision/Collision.hpp"
#include "engine/Log.hpp"
#include "gameplay/Geometry.hpp"

namespace spindle::gameplay {

Match::Match(const MatchConfig& config, const std::vector<ParticipantSpec>& participants, LevelProvider& levels)
    : config_(config), levels_(levels), fieldConfig_(makeFieldConfig(config.needleScale)) {
    config_.tries = std::clamp(config_.tries, 1, 9);
    config_.fireCombo = std::clamp(config_.fireCombo, 2, 99);
    config_.fireSeconds = std::clamp(config_.fireSeconds, 0.25, 10.0);
    config_.fireChainWindow = std::clamp(config_.fireChainWindow, 0.25, 10.0);
    fieldConfig_.fireEnabled = config_.fireMeter;
    fieldConfig_.fireCombo = config_.fireCombo;
    fieldConfig_.fireTicks = static_cast<int>(std::lround(config_.fireSeconds * geom::kTickRate));
    fieldConfig_.chainWindowTicks = static_cast<int>(std::lround(config_.fireChainWindow * geom::kTickRate));
    // A heat ring does nothing without the fire meter, so rounds never get one then.
    if (!config_.fireMeter) std::erase(config_.powerups, PowerupKind::Heat);
    for (const ParticipantSpec& spec : participants) {
        if (spec.station < 0 || spec.station >= kMaxStations) continue;
        auto& slot = participants_[static_cast<std::size_t>(spec.station)];
        if (slot) continue; // one participant per station
        Participant p;
        p.station = spec.station;
        p.kind = spec.kind;
        p.tries = config_.tries;
        slot = p;
        ++participantCount_;
    }
    round_ = std::max(1, config_.startRound);
    prepareRound();
}

double Match::roundTime() const {
    if (phase_ != MatchPhase::Playing) return 0.0;
    return static_cast<double>(tick_ - roundStartTick_) * geom::kTickSeconds;
}

double Match::roundTimeLeft() const {
    if (phase_ != MatchPhase::Playing) return config_.roundTimeLimit;
    return std::max(0.0, config_.roundTimeLimit - roundTime());
}

int Match::aliveCount() const {
    int n = 0;
    for (const auto& p : participants_)
        if (p && p->tries > 0) ++n;
    return n;
}

bool Match::stationInRound(int station) const {
    if (station < 0 || station >= kMaxStations) return false;
    const auto& p = participants_[static_cast<std::size_t>(station)];
    return p && p->inRound;
}

void Match::prepareRound() {
    // Powerup rings in normal rounds only: sudden death is decided by skill alone.
    static const std::vector<PowerupKind> kNone;
    level_ = levels_.level(round_, config_.seed, suddenDeath_ ? kNone : config_.powerups);
    level_.round = round_;
    std::erase_if(level_.pickups, [&](const Pickup& p) { return suddenDeath_ || !contains(config_.powerups, p.kind); });
    rotor_ = Rotor(scaled(level_.rotation, config_.rotationScale));
    // No fire in sudden death either: like the rings, it is decided by skill alone.
    FieldConfig fieldConfig = fieldConfig_;
    if (suddenDeath_) fieldConfig.fireEnabled = false;
    for (int s = 0; s < kMaxStations; ++s) {
        auto& p = participants_[static_cast<std::size_t>(s)];
        Playfield& f = fields_[static_cast<std::size_t>(s)];
        if (!p) {
            f.reset();
            continue;
        }
        p->inRound = suddenDeath_ ? std::find(suddenDeathSet_.begin(), suddenDeathSet_.end(), s) != suddenDeathSet_.end()
                                  : p->tries > 0;
        if (p->inRound) {
            f.begin(level_, fieldConfig);
            f.setCombo(suddenDeath_ ? 0 : p->carriedCombo);
        } else {
            f.reset();
        }
    }
    phase_ = MatchPhase::Ready;
    phaseTime_ = 0.0;
    emit(MatchEventType::RoundReady, -1, round_);
}

void Match::startRound() {
    phase_ = MatchPhase::Playing;
    phaseTime_ = 0.0;
    roundStartTick_ = tick_;
    for (int s = 0; s < kMaxStations; ++s)
        if (stationInRound(s)) fields_[static_cast<std::size_t>(s)].startChain(tick_);
    emit(MatchEventType::RoundStarted, -1, round_);
}

void Match::tick(const std::array<int, kMaxStations>& fireRequests) {
    if (paused_ || phase_ == MatchPhase::Over) return;
    ++tick_;
    phaseTime_ += geom::kTickSeconds;

    switch (phase_) {
    case MatchPhase::Ready:
        if (phaseTime_ >= config_.readyDelay) startRound();
        return;
    case MatchPhase::Resolving:
        if (phaseTime_ >= config_.resolveDelay) {
            if (overAfterResolve_) {
                phase_ = MatchPhase::Over;
                phaseTime_ = 0.0;
                return;
            }
            ++round_;
            prepareRound();
        }
        return;
    case MatchPhase::Over: return;
    case MatchPhase::Playing: break;
    }

    const double before = rotor_.state().angle;
    rotor_.advance(geom::kTickSeconds);
    const double delta = collision::wrapAngle(rotor_.state().angle - before);

    bool anyPlaying = false;
    for (int s = 0; s < kMaxStations; ++s) {
        auto& p = participants_[static_cast<std::size_t>(s)];
        if (!p || !p->inRound) continue;
        Playfield& f = fields_[static_cast<std::size_t>(s)];
        if (f.status() != FieldStatus::Playing) continue;
        for (int k = 0; k < fireRequests[static_cast<std::size_t>(s)]; ++k) {
            if (f.fire(tick_)) emit(MatchEventType::NeedleLaunched, s);
        }
        const int placedBefore = f.placed();
        f.step(tick_, geom::kTickSeconds, before, delta);
        for (int k = placedBefore; k < f.placed(); ++k) emit(MatchEventType::NeedlePlaced, s);
        for (PowerupKind kind : f.takeClaims()) {
            ++p->powerupsClaimed;
            emit(MatchEventType::PowerupClaimed, s, static_cast<int>(kind));
            if (kind == PowerupKind::Mend && !suddenDeath_ && p->tries < config_.tries) {
                ++p->tries;
                emit(MatchEventType::TryGained, s, p->tries);
            }
        }
        for (int k = f.takeShieldBreaks(); k > 0; --k) emit(MatchEventType::ShieldBroke, s);
        for (int k = f.takeIgnitions(); k > 0; --k) {
            ++p->ignitions;
            emit(MatchEventType::Ignited, s);
        }
        if (const int burnt = f.takeBurns(); burnt > 0) {
            p->burnt += burnt;
            emit(MatchEventType::Burned, s, burnt);
        }
        if (f.status() == FieldStatus::Cleared) emit(MatchEventType::StationCleared, s);
        if (f.status() == FieldStatus::Failed) emit(MatchEventType::StationFailed, s, 0);
        if (f.status() == FieldStatus::Playing) anyPlaying = true;
    }

    const auto limitTicks = static_cast<std::uint64_t>(std::llround(config_.roundTimeLimit * geom::kTickRate));
    if (anyPlaying && tick_ - roundStartTick_ >= limitTicks) {
        for (int s = 0; s < kMaxStations; ++s) {
            auto& p = participants_[static_cast<std::size_t>(s)];
            Playfield& f = fields_[static_cast<std::size_t>(s)];
            if (p && p->inRound && f.status() == FieldStatus::Playing) {
                f.failTimeout(tick_);
                emit(MatchEventType::StationFailed, s, 1);
            }
        }
        anyPlaying = false;
    }
    if (!anyPlaying) resolveRound();
}

void Match::resolveRound() {
    std::vector<PlayerRecord> players;
    std::vector<RoundOutcome> outcomes;
    for (int s = 0; s < kMaxStations; ++s) {
        auto& p = participants_[static_cast<std::size_t>(s)];
        if (!p || !p->inRound) continue;
        const Playfield& f = fields_[static_cast<std::size_t>(s)];
        RoundOutcome o;
        o.id = s;
        o.cleared = f.status() == FieldStatus::Cleared;
        o.failTick = f.failTick();
        o.placed = f.placed();
        o.timedOut = f.failReason() == FailReason::Timeout;
        outcomes.push_back(o);
        players.push_back({s, p->tries});
        p->totalPlaced += f.placed();
        if (o.cleared) ++p->roundsCleared;
        // Clearing banks the combo for the next round; failing loses it.
        p->carriedCombo = o.cleared && !suddenDeath_ ? f.combo() : 0;
    }

    RoundDecision d = suddenDeath_ ? gameplay::resolveSuddenDeath(outcomes) : gameplay::resolveRound(players, outcomes, participantCount_);
    if (suddenDeath_ && d.rule == RuleCase::T7 && d.subRule == RuleCase::T4) {
        if (++tiedSuddenDeaths_ >= kMaxTiedSuddenDeaths) {
            std::vector<MatchTotals> totals;
            for (const RoundOutcome& o : outcomes) {
                const Participant& p = *participants_[static_cast<std::size_t>(o.id)];
                totals.push_back({o.id, p.totalPlaced, p.roundsCleared});
            }
            d = matchTotalsTieBreak(totals);
        }
    } else {
        tiedSuddenDeaths_ = 0;
    }
    for (int id : d.lostTry) {
        auto& p = participants_[static_cast<std::size_t>(id)];
        p->tries = std::max(0, p->tries - 1);
        emit(MatchEventType::TryLost, id, p->tries);
    }
    for (int id : d.eliminated) {
        auto& p = participants_[static_cast<std::size_t>(id)];
        p->eliminated = true;
        p->eliminatedRound = round_;
        p->eliminatedTick = fields_[static_cast<std::size_t>(id)].failTick();
        emit(MatchEventType::Eliminated, id);
    }
    if (suddenDeath_) {
        // Sudden-death losers are ranked by how far they got in it.
        for (const RoundOutcome& o : outcomes) {
            auto& p = participants_[static_cast<std::size_t>(o.id)];
            p->eliminatedRound = round_;
            p->eliminatedTick = o.cleared ? tick_ : o.failTick;
        }
    }

    switch (d.kind) {
    case DecisionKind::Continue:
        suddenDeath_ = false;
        suddenDeathSet_.clear();
        break;
    case DecisionKind::Winner:
        winner_ = d.winner;
        overAfterResolve_ = true;
        emit(MatchEventType::Winner, d.winner);
        break;
    case DecisionKind::NoWinner:
        overAfterResolve_ = true;
        emit(MatchEventType::NoWinner);
        break;
    case DecisionKind::SuddenDeath:
        suddenDeath_ = true;
        suddenDeathSet_ = d.suddenDeath;
        emit(MatchEventType::SuddenDeath, -1, static_cast<int>(d.suddenDeath.size()));
        break;
    }
    log::info("match: round {} resolved by {}{} -> {}", round_, ruleCaseName(d.rule),
              d.subRule != RuleCase::None ? std::string("/") + std::string(ruleCaseName(d.subRule)) : std::string(),
              d.kind == DecisionKind::Winner        ? "winner P" + std::to_string(d.winner + 1)
              : d.kind == DecisionKind::NoWinner    ? std::string("match over")
              : d.kind == DecisionKind::SuddenDeath ? std::string("sudden death")
                                                    : std::string("next round"));
    lastDecision_ = std::move(d);
    phase_ = MatchPhase::Resolving;
    phaseTime_ = 0.0;
}

std::vector<int> Match::standings() const {
    std::vector<int> order;
    for (int s = 0; s < kMaxStations; ++s)
        if (participants_[static_cast<std::size_t>(s)]) order.push_back(s);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (a == winner_) return true;
        if (b == winner_) return false;
        const Participant& pa = *participants_[static_cast<std::size_t>(a)];
        const Participant& pb = *participants_[static_cast<std::size_t>(b)];
        if (pa.eliminated != pb.eliminated) return !pa.eliminated;
        if (pa.eliminatedRound != pb.eliminatedRound) return pa.eliminatedRound > pb.eliminatedRound;
        if (pa.eliminatedTick != pb.eliminatedTick) return pa.eliminatedTick > pb.eliminatedTick;
        return pa.totalPlaced > pb.totalPlaced;
    });
    return order;
}

std::vector<MatchEvent> Match::takeEvents() {
    std::vector<MatchEvent> out;
    out.swap(events_);
    return out;
}

} // namespace spindle::gameplay
