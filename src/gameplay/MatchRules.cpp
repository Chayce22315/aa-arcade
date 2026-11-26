#include "gameplay/MatchRules.hpp"

#include <algorithm>

namespace spindle::gameplay {

std::string_view ruleCaseName(RuleCase c) {
    switch (c) {
    case RuleCase::None: return "-";
    case RuleCase::T1: return "T1";
    case RuleCase::T2: return "T2";
    case RuleCase::T3: return "T3";
    case RuleCase::T4: return "T4";
    case RuleCase::T5: return "T5";
    case RuleCase::T6: return "T6";
    case RuleCase::T7: return "T7";
    case RuleCase::T8: return "T8";
    case RuleCase::T9: return "T9";
    case RuleCase::T10: return "T10";
    case RuleCase::T11: return "T11";
    case RuleCase::Score: return "score";
    case RuleCase::Race: return "race";
    case RuleCase::Crown: return "crown";
    case RuleCase::Clock: return "clock";
    case RuleCase::Team: return "team";
    }
    return "-";
}

RoundDecision tieBreak(std::span<const RoundOutcome> failed) {
    RoundDecision d;
    if (failed.empty()) return d;
    // T2: the player who failed last survived longest.
    std::uint64_t lastTick = 0;
    for (const auto& o : failed) lastTick = std::max(lastTick, o.failTick);
    std::vector<RoundOutcome> latest;
    for (const auto& o : failed)
        if (o.failTick == lastTick) latest.push_back(o);
    if (latest.size() == 1) {
        d.kind = DecisionKind::Winner;
        d.winner = latest.front().id;
        d.rule = RuleCase::T2;
        return d;
    }
    // T3: most needles placed in the round.
    int best = 0;
    for (const auto& o : latest) best = std::max(best, o.placed);
    std::vector<int> top;
    for (const auto& o : latest)
        if (o.placed == best) top.push_back(o.id);
    if (top.size() == 1) {
        d.kind = DecisionKind::Winner;
        d.winner = top.front();
        d.rule = RuleCase::T3;
        return d;
    }
    // T4: still tied, sudden death between exactly the tied players.
    std::sort(top.begin(), top.end());
    d.kind = DecisionKind::SuddenDeath;
    d.suddenDeath = std::move(top);
    d.rule = RuleCase::T4;
    return d;
}

RoundDecision resolveRound(std::span<const PlayerRecord> players, std::span<const RoundOutcome> outcomes, int participants) {
    RoundDecision d;
    std::vector<int> alive;
    std::vector<RoundOutcome> failedOut;
    for (const auto& p : players) {
        const auto it = std::find_if(outcomes.begin(), outcomes.end(), [&](const RoundOutcome& o) { return o.id == p.id; });
        const bool cleared = it != outcomes.end() && it->cleared;
        int tries = p.tries;
        if (!cleared) {
            // A player with no outcome is treated as failed (defensive: the match always reports one).
            RoundOutcome o = it != outcomes.end() ? *it : RoundOutcome{p.id, false, 0, 0};
            d.lostTry.push_back(p.id);
            --tries;
            if (tries <= 0) {
                d.eliminated.push_back(p.id);
                failedOut.push_back(o);
            }
        }
        if (tries > 0) alive.push_back(p.id);
    }

    if (participants <= 1) {
        // T10 / assumption A1: a lone player plays until eliminated; there is nobody to beat.
        d.kind = alive.empty() ? DecisionKind::NoWinner : DecisionKind::Continue;
        d.rule = RuleCase::T10;
        return d;
    }
    if (alive.size() == 1) {
        d.kind = DecisionKind::Winner;
        d.winner = alive.front();
        d.rule = RuleCase::T1;
        return d;
    }
    if (alive.size() >= 2) {
        d.kind = DecisionKind::Continue;
        d.rule = d.eliminated.empty() ? RuleCase::None : RuleCase::T9;
        return d;
    }
    // Nobody left: every player alive at the start of the round was eliminated in it.
    RoundDecision tb = tieBreak(failedOut);
    tb.lostTry = std::move(d.lostTry);
    tb.eliminated = std::move(d.eliminated);
    // T8: everyone ran out of time together, so failure ticks cannot separate them; T3/T4 decide.
    const bool allTimedOutSameTick =
        failedOut.size() >= 2 && std::all_of(failedOut.begin(), failedOut.end(), [&](const RoundOutcome& o) {
            return o.timedOut && o.failTick == failedOut.front().failTick;
        });
    if (allTimedOutSameTick) {
        tb.subRule = tb.rule;
        tb.rule = RuleCase::T8;
    }
    return tb;
}

RoundDecision resolveSuddenDeath(std::span<const RoundOutcome> outcomes) {
    std::vector<int> cleared;
    std::vector<RoundOutcome> failed;
    for (const auto& o : outcomes) {
        if (o.cleared)
            cleared.push_back(o.id);
        else
            failed.push_back(o);
    }
    RoundDecision d;
    if (cleared.size() == 1) {
        d.kind = DecisionKind::Winner;
        d.winner = cleared.front();
        d.rule = RuleCase::T5;
        return d;
    }
    if (cleared.size() >= 2) {
        std::sort(cleared.begin(), cleared.end());
        d.kind = DecisionKind::SuddenDeath;
        d.suddenDeath = std::move(cleared);
        d.rule = RuleCase::T6;
        return d;
    }
    RoundDecision tb = tieBreak(failed);
    tb.subRule = tb.rule;
    tb.rule = RuleCase::T7;
    return tb;
}

RoundDecision matchTotalsTieBreak(std::span<const MatchTotals> players) {
    RoundDecision d;
    if (players.empty()) return d;
    const MatchTotals* best = &players.front();
    for (const MatchTotals& p : players) {
        if (p.totalPlaced != best->totalPlaced) {
            if (p.totalPlaced > best->totalPlaced) best = &p;
            continue;
        }
        if (p.roundsCleared != best->roundsCleared) {
            if (p.roundsCleared > best->roundsCleared) best = &p;
            continue;
        }
        if (p.id < best->id) best = &p;
    }
    d.kind = DecisionKind::Winner;
    d.winner = best->id;
    d.rule = RuleCase::T11;
    return d;
}

} // namespace spindle::gameplay
