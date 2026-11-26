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
    // Teams: P1 + P2 against P3 + P4, each team with one shared pool of tries (shown on every member).
    if (config_.mode.teams) {
        for (auto& p : participants_)
            if (p) p->team = p->station < 2 ? 0 : 1;
        for (int t = 0; t < 2; ++t) {
            const bool any = std::any_of(participants_.begin(), participants_.end(), [t](const auto& p) { return p && p->team == t; });
            teamPools_[static_cast<std::size_t>(t)] = any ? config_.tries * 2 : 0;
        }
        for (auto& p : participants_)
            if (p) p->tries = teamPools_[static_cast<std::size_t>(p->team)];
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
        if (p && p->tries > 0 && !p->eliminated) ++n;
    return n;
}

bool Match::stationInRound(int station) const {
    if (station < 0 || station >= kMaxStations) return false;
    const auto& p = participants_[static_cast<std::size_t>(station)];
    return p && p->inRound;
}

int Match::modePoints(int station) const {
    if (station < 0 || station >= kMaxStations) return 0;
    const auto& p = participants_[static_cast<std::size_t>(station)];
    if (!p) return 0;
    switch (config_.mode.end) {
    case MatchEnd::FirstTo: return p->raceWins;
    case MatchEnd::Rounds: return p->crownPoints;
    default: return 0;
    }
}

FieldConfig Match::roundFieldConfig(Mutator m) const {
    FieldConfig fc = fieldConfig_;
    if (suddenDeath_) fc.fireEnabled = false; // no fire in sudden death: like the rings, it is skill alone
    if (m == Mutator::Tiny || m == Mutator::Big) {
        fc.ballRadius *= m == Mutator::Tiny ? kTinyFactor : kBigFactor;
        fc.minSeparation = collision::minSeparation(fc.needleLength, fc.ballRadius);
        const double seconds = (2.0 * fc.ballRadius) / fc.needleSpeed;
        fc.minFireIntervalTicks = std::max(1, static_cast<int>(std::ceil(seconds * geom::kTickRate)));
    }
    return fc;
}

void Match::prepareRound() {
    // Powerup rings in normal rounds only: sudden death is decided by skill alone.
    static const std::vector<PowerupKind> kNone;
    level_ = levels_.level(round_, config_.seed, suddenDeath_ ? kNone : config_.powerups);
    level_.round = round_;
    level_.junk = 0;
    std::erase_if(level_.pickups, [&](const Pickup& p) { return suddenDeath_ || !contains(config_.powerups, p.kind); });

    // Chaos: one mutator for every station, kept only if the round stays playable with it.
    mutator_ = config_.mode.mutators && !suddenDeath_ ? rollMutator(config_.seed, round_) : Mutator::None;
    FieldConfig fieldConfig = roundFieldConfig(mutator_);
    LevelLimits limits = defaultLimits();
    limits.powerups.assign(kAllPowerups.begin(), kAllPowerups.end());
    limits.minSeparation = fieldConfig.minSeparation;
    double rotationScale = config_.rotationScale;
    switch (mutator_) {
    case Mutator::Tiny:
    case Mutator::Big:
        if (validateLevel(level_, limits)) {
            mutator_ = Mutator::None;
            fieldConfig = roundFieldConfig(mutator_);
            limits.minSeparation = fieldConfig.minSeparation;
        }
        break;
    case Mutator::Fast: {
        double fastest = std::fabs(level_.rotation.initialSpeed);
        for (const auto& seg : level_.rotation.segments) fastest = std::max(fastest, std::fabs(seg.targetSpeed));
        if (fastest * kFastFactor <= limits.maxSpeed)
            rotationScale *= kFastFactor;
        else
            mutator_ = Mutator::None;
        break;
    }
    case Mutator::Extra: {
        LevelDef more = level_;
        more.launchCount += kExtraNeedles;
        if (!validateLevel(more, limits))
            level_ = std::move(more);
        else
            mutator_ = Mutator::None;
        break;
    }
    case Mutator::Blind:
    case Mutator::None: break;
    }
    rotor_ = Rotor(scaled(level_.rotation, rotationScale));

    junkThisRound_.fill(0);
    closeCallsThisRound_.fill(0);
    for (int s = 0; s < kMaxStations; ++s) {
        auto& p = participants_[static_cast<std::size_t>(s)];
        Playfield& f = fields_[static_cast<std::size_t>(s)];
        if (!p) {
            f.reset();
            continue;
        }
        p->inRound = suddenDeath_ ? std::find(suddenDeathSet_.begin(), suddenDeathSet_.end(), s) != suddenDeathSet_.end()
                                  : p->tries > 0 && !p->eliminated;
        p->roundPoints = 0;
        p->roundBonus = 0;
        if (p->inRound) {
            // Sabotage: junk sent by rivals lands in this station's copy of the round only.
            LevelDef mine = level_;
            auto& pending = pendingJunk_[static_cast<std::size_t>(s)];
            if (config_.mode.junk && !suddenDeath_ && pending > 0) {
                scripting::Pcg32 rng((static_cast<std::uint64_t>(config_.seed) << 20) ^ (static_cast<std::uint64_t>(round_) << 4) ^
                                         static_cast<std::uint64_t>(s),
                                     std::uint64_t{0x2545F4914F6CDD1DU} + static_cast<std::uint64_t>(s));
                junkThisRound_[static_cast<std::size_t>(s)] = addJunk(mine, std::min(pending, kJunkCap), limits, rng);
            }
            pending = 0;
            f.begin(mine, fieldConfig);
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

void Match::sendJunk(int from, int count) {
    // To every rival still in the match, capped per round.
    for (int s = 0; s < kMaxStations; ++s) {
        const auto& p = participants_[static_cast<std::size_t>(s)];
        if (s == from || !p || p->eliminated || p->tries <= 0) continue;
        auto& pending = pendingJunk_[static_cast<std::size_t>(s)];
        pending = std::min(kJunkCap, pending + count);
    }
}

void Match::tick(const std::array<int, kMaxStations>& fireRequests) {
    if (paused_ || phase_ == MatchPhase::Over) return;
    ++tick_;
    phaseTime_ += geom::kTickSeconds;

    switch (phase_) {
    case MatchPhase::Ready:
        if (phaseTime_ >= config_.readyDelay) startRound();
        return;
    case MatchPhase::Tally:
        if (phaseTime_ >= tally_.total()) {
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

    clockUsed_ += geom::kTickSeconds;
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
        for (const scoring::Placement& pl : f.takePlacements()) {
            p->roundPoints += scoring::placementPoints(pl, round_);
            if (!pl.closeCall) continue;
            ++p->closeCalls;
            // Sabotage: every third close call in a round sends junk to the rivals.
            if (config_.mode.junk && !suddenDeath_ && ++closeCallsThisRound_[static_cast<std::size_t>(s)] % kCloseCallsPerJunk == 0)
                sendJunk(s, 1);
        }
        p->bestCombo = std::max(p->bestCombo, f.combo());
        for (PowerupKind kind : f.takeClaims()) {
            ++p->powerupsClaimed;
            p->roundPoints += scoring::powerupPoints(round_);
            emit(MatchEventType::PowerupClaimed, s, static_cast<int>(kind));
            if (kind == PowerupKind::Mend && !suddenDeath_ && config_.mode.knockouts && !config_.mode.teams && p->tries < config_.tries) {
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
            p->roundPoints += scoring::burnPoints(burnt, round_);
            emit(MatchEventType::Burned, s, burnt);
        }
        if (f.status() == FieldStatus::Cleared) emit(MatchEventType::StationCleared, s);
        if (f.status() == FieldStatus::Failed) emit(MatchEventType::StationFailed, s, 0);
        if (f.status() == FieldStatus::Playing) anyPlaying = true;
    }

    // Blitz: when the match clock runs out the round stops where it is (no failure, no bonus).
    if (anyPlaying && config_.mode.end == MatchEnd::Clock && clockUsed_ >= config_.mode.clockSeconds) {
        for (int s = 0; s < kMaxStations; ++s) {
            auto& p = participants_[static_cast<std::size_t>(s)];
            Playfield& f = fields_[static_cast<std::size_t>(s)];
            if (p && p->inRound && f.status() == FieldStatus::Playing) f.stopForClock(tick_);
        }
        anyPlaying = false;
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

std::vector<ScoreTotals> Match::totals(bool aliveOnly) const {
    std::vector<ScoreTotals> out;
    for (int s = 0; s < kMaxStations; ++s) {
        const auto& p = participants_[static_cast<std::size_t>(s)];
        if (!p || (aliveOnly && (p->eliminated || p->tries <= 0))) continue;
        out.push_back({s, p->score, p->totalPlaced, p->roundsCleared, modePoints(s)});
    }
    return out;
}

RoundDecision Match::decideRound(const std::vector<PlayerRecord>& players, const std::vector<RoundOutcome>& outcomes) {
    const ModeRules& mode = config_.mode;
    if (mode.teams) {
        std::vector<int> teamOf;
        for (const RoundOutcome& o : outcomes) teamOf.push_back(participants_[static_cast<std::size_t>(o.id)]->team);
        TeamDecision t = resolveTeamRound(teamPools_, outcomes, teamOf, totals());
        teamPools_ = t.pools; // applyDecision copies the pools onto every member
        winningTeam_ = t.winningTeam;
        return t.decision;
    }
    switch (mode.end) {
    case MatchEnd::LastStanding: {
        RoundDecision d = suddenDeath_ ? gameplay::resolveSuddenDeath(outcomes) : gameplay::resolveRound(players, outcomes, participantCount_);
        if (suddenDeath_ && d.rule == RuleCase::T7 && d.subRule == RuleCase::T4) {
            if (++tiedSuddenDeaths_ >= kMaxTiedSuddenDeaths) {
                std::vector<MatchTotals> t;
                for (const RoundOutcome& o : outcomes) {
                    const Participant& p = *participants_[static_cast<std::size_t>(o.id)];
                    t.push_back({o.id, p.totalPlaced, p.roundsCleared});
                }
                d = matchTotalsTieBreak(t);
            }
        } else {
            tiedSuddenDeaths_ = 0;
        }
        return d;
    }
    case MatchEnd::AllOut: return resolveEndless(players, outcomes, totals(), participantCount_);
    case MatchEnd::Clock:
    case MatchEnd::FirstTo:
    case MatchEnd::Rounds: return {}; // no knock-outs; the end is checked after the crown and wins are counted
    }
    return {};
}

void Match::applyDecision(const RoundDecision& d) {
    for (int id : d.lostTry) {
        auto& p = participants_[static_cast<std::size_t>(id)];
        p->tries = std::max(0, p->tries - 1);
        emit(MatchEventType::TryLost, id, p->tries);
    }
    if (config_.mode.teams)
        for (auto& p : participants_)
            if (p && p->team >= 0) p->tries = teamPools_[static_cast<std::size_t>(p->team)];
    for (int id : d.eliminated) {
        auto& p = participants_[static_cast<std::size_t>(id)];
        p->eliminated = true;
        p->tries = 0;
        p->eliminatedRound = round_;
        p->eliminatedTick = fields_[static_cast<std::size_t>(id)].failTick();
        emit(MatchEventType::Eliminated, id);
    }
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

    scoreRound(outcomes);

    RoundDecision d = decideRound(players, outcomes);
    applyDecision(d);
    if (suddenDeath_) {
        // Sudden-death losers are ranked by how far they got in it.
        for (const RoundOutcome& o : outcomes) {
            auto& p = participants_[static_cast<std::size_t>(o.id)];
            p->eliminatedRound = round_;
            p->eliminatedTick = o.cleared ? tick_ : o.failTick;
        }
    }
    updateCrown();
    ++roundsPlayed_;

    // The modes without knock-outs end on their own counts.
    const ModeRules& mode = config_.mode;
    if (mode.end == MatchEnd::Rounds && crown_ >= 0) ++participants_[static_cast<std::size_t>(crown_)]->crownPoints;
    if (mode.end == MatchEnd::FirstTo) {
        std::optional<std::uint64_t> first;
        for (const RoundOutcome& o : outcomes) {
            if (!o.cleared) continue;
            const std::uint64_t t = fields_[static_cast<std::size_t>(o.id)].clearTick();
            if (!first || t < *first) first = t;
        }
        if (first) {
            noClearStreak_ = 0;
            for (const RoundOutcome& o : outcomes)
                if (o.cleared && fields_[static_cast<std::size_t>(o.id)].clearTick() == *first) ++participants_[static_cast<std::size_t>(o.id)]->raceWins;
        } else {
            ++noClearStreak_;
        }
    }
    const auto finishOnTotals = [&](RuleCase rule, std::vector<ScoreTotals> pool) {
        d.rule = rule;
        if (participantCount_ <= 1) {
            d.kind = DecisionKind::NoWinner;
            return;
        }
        d.kind = DecisionKind::Winner;
        d.winner = bestByTotals(pool);
    };
    switch (mode.end) {
    case MatchEnd::Clock:
        d.rule = RuleCase::Clock;
        if (clockUsed_ >= mode.clockSeconds) finishOnTotals(RuleCase::Clock, totals());
        break;
    case MatchEnd::FirstTo: {
        d.rule = RuleCase::Race;
        std::vector<ScoreTotals> reached;
        for (const ScoreTotals& t : totals())
            if (t.points >= mode.target) reached.push_back(t);
        if (!reached.empty())
            finishOnTotals(RuleCase::Race, reached);
        else if (noClearStreak_ >= kRaceNoClearGuard)
            finishOnTotals(RuleCase::Race, totals());
        break;
    }
    case MatchEnd::Rounds:
        d.rule = RuleCase::Crown;
        if (roundsPlayed_ >= mode.target) finishOnTotals(RuleCase::Crown, totals());
        break;
    default: break;
    }

    // Sabotage: each clear sends one junk needle to every rival still in.
    if (mode.junk && !suddenDeath_ && d.kind == DecisionKind::Continue)
        for (const RoundOutcome& o : outcomes)
            if (o.cleared) sendJunk(o.id, 1);

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
    log::info("match: {} round {} resolved by {}{} -> {}", modeKey(mode.id), round_, ruleCaseName(d.rule),
              d.subRule != RuleCase::None ? std::string("/") + std::string(ruleCaseName(d.subRule)) : std::string(),
              d.kind == DecisionKind::Winner        ? "winner P" + std::to_string(d.winner + 1)
              : d.kind == DecisionKind::NoWinner    ? std::string("match over")
              : d.kind == DecisionKind::SuddenDeath ? std::string("sudden death")
                                                    : std::string("next round"));
    lastDecision_ = std::move(d);

    int digits = 1;
    for (int s = 0; s < kMaxStations; ++s)
        if (tallied_[static_cast<std::size_t>(s)]) digits = std::max(digits, scoring::digitCount(participants_[static_cast<std::size_t>(s)]->score));
    tally_ = scoring::tallyTiming(config_.tally, digits, config_.resolveDelay);
    phase_ = MatchPhase::Tally;
    phaseTime_ = 0.0;
}

void Match::scoreRound(const std::vector<RoundOutcome>& outcomes) {
    // The first clear (ties share it) earns a bonus when two or more players are in the round.
    std::optional<std::uint64_t> firstClear;
    for (const RoundOutcome& o : outcomes) {
        if (!o.cleared) continue;
        const std::uint64_t t = fields_[static_cast<std::size_t>(o.id)].clearTick();
        if (!firstClear || t < *firstClear) firstClear = t;
    }
    tallied_.fill(false);
    for (const RoundOutcome& o : outcomes) {
        auto& p = participants_[static_cast<std::size_t>(o.id)];
        const Playfield& f = fields_[static_cast<std::size_t>(o.id)];
        scoring::ClearInfo info;
        info.cleared = o.cleared;
        if (o.cleared) {
            const double used = static_cast<double>(f.clearTick() - roundStartTick_) * geom::kTickSeconds;
            info.secondsLeft = config_.roundTimeLimit - used;
            info.firstClear = outcomes.size() >= 2 && firstClear && f.clearTick() == *firstClear;
        }
        p->roundBonus = scoring::clearBonus(info, round_);
        // Blitz: a real failure costs points (the clock running out does not).
        if (config_.mode.failPenalty && !o.cleared && f.failReason() != FailReason::Clock) p->roundBonus = -scoring::failPenalty(round_);
        p->scoreBefore = p->score;
        p->score = scoring::addCapped(p->score, p->roundPoints + p->roundBonus);
        tallied_[static_cast<std::size_t>(o.id)] = true;
        emit(MatchEventType::RoundScored, o.id, static_cast<int>(std::min<std::int64_t>(p->roundPoints, scoring::kMaxScore)));
    }
}

void Match::updateCrown() {
    // A holder knocked out loses the crown; the round's outright winner among those still in takes it.
    crownBefore_ = crown_;
    if (crown_ >= 0) {
        const auto& h = participants_[static_cast<std::size_t>(crown_)];
        if (!h || h->eliminated) {
            emit(MatchEventType::CrownLost, crown_);
            crown_ = -1;
        }
    }
    std::int64_t best = 0;
    std::vector<int> top;
    for (int s = 0; s < kMaxStations; ++s) {
        const auto& p = participants_[static_cast<std::size_t>(s)];
        if (!tallied_[static_cast<std::size_t>(s)] || !p || p->eliminated || p->roundPoints <= 0) continue;
        if (p->roundPoints > best) {
            best = p->roundPoints;
            top = {s};
        } else if (p->roundPoints == best) {
            top.push_back(s);
        }
    }
    if (top.size() != 1) return; // nobody scored, or a tie: the crown stays where it is
    auto& winner = participants_[static_cast<std::size_t>(top.front())];
    ++winner->crownsWon;
    if (crown_ == top.front()) return;
    const int previous = crown_;
    crown_ = top.front();
    emit(MatchEventType::CrownTaken, crown_, previous);
}

std::vector<int> Match::standings() const {
    std::vector<int> order;
    for (int s = 0; s < kMaxStations; ++s)
        if (participants_[static_cast<std::size_t>(s)]) order.push_back(s);
    const bool lastStanding = config_.mode.end == MatchEnd::LastStanding && !config_.mode.teams;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (a == winner_) return true;
        if (b == winner_) return false;
        const Participant& pa = *participants_[static_cast<std::size_t>(a)];
        const Participant& pb = *participants_[static_cast<std::size_t>(b)];
        if (config_.mode.teams && winningTeam_ && (pa.team == *winningTeam_) != (pb.team == *winningTeam_)) return pa.team == *winningTeam_;
        if (!lastStanding) {
            // Score modes: the mode's own count, then score, then needles placed.
            if (modePoints(a) != modePoints(b)) return modePoints(a) > modePoints(b);
            if (pa.score != pb.score) return pa.score > pb.score;
            return pa.totalPlaced > pb.totalPlaced;
        }
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
