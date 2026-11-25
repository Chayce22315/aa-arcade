// One test per rule case of docs/RULES.md (T1..T10), plus the basic try accounting.

#include <algorithm>

#include "Test.hpp"
#include "gameplay/MatchRules.hpp"

using namespace spindle::gameplay;

namespace {
RoundOutcome cleared(int id, int placed = 8) { return {id, true, 0, placed, false}; }
RoundOutcome failed(int id, std::uint64_t tick, int placed, bool timeout = false) { return {id, false, tick, placed, timeout}; }
bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }
} // namespace

TEST_CASE(rules_clear_costs_nothing_fail_costs_one_try) {
    const std::vector<PlayerRecord> players{{0, 3}, {1, 3}, {2, 2}};
    const std::vector<RoundOutcome> out{cleared(0), failed(1, 100, 3), failed(2, 120, 4)};
    RoundDecision d = resolveRound(players, out, 3);
    CHECK(d.kind == DecisionKind::Continue);
    CHECK(!contains(d.lostTry, 0));
    CHECK(contains(d.lostTry, 1));
    CHECK(contains(d.lostTry, 2));
    CHECK(d.eliminated.empty());
    CHECK(d.rule == RuleCase::None);
}

TEST_CASE(rules_T1_single_survivor_wins) {
    const std::vector<PlayerRecord> players{{0, 1}, {1, 2}, {3, 1}};
    const std::vector<RoundOutcome> out{failed(0, 50, 2), cleared(1), failed(3, 70, 5)};
    RoundDecision d = resolveRound(players, out, 4);
    CHECK(d.kind == DecisionKind::Winner);
    CHECK_EQ(d.winner, 1);
    CHECK(d.rule == RuleCase::T1);
    CHECK(contains(d.eliminated, 0) && contains(d.eliminated, 3));
}

TEST_CASE(rules_T1_survivor_with_lost_try_still_wins) {
    const std::vector<PlayerRecord> players{{0, 1}, {1, 2}};
    const std::vector<RoundOutcome> out{failed(0, 50, 2), failed(1, 60, 1)};
    RoundDecision d = resolveRound(players, out, 2);
    CHECK(d.kind == DecisionKind::Winner);
    CHECK_EQ(d.winner, 1);
    CHECK(d.rule == RuleCase::T1);
}

TEST_CASE(rules_T2_last_to_fail_wins_when_all_eliminated) {
    const std::vector<PlayerRecord> players{{0, 1}, {2, 1}, {3, 1}};
    const std::vector<RoundOutcome> out{failed(0, 300, 9), failed(2, 480, 2), failed(3, 410, 7)};
    RoundDecision d = resolveRound(players, out, 4);
    CHECK(d.kind == DecisionKind::Winner);
    CHECK_EQ(d.winner, 2);
    CHECK(d.rule == RuleCase::T2);
    CHECK_EQ(d.eliminated.size(), std::size_t{3});
}

TEST_CASE(rules_T3_same_tick_most_needles_wins) {
    const std::vector<PlayerRecord> players{{0, 1}, {1, 1}, {2, 1}};
    const std::vector<RoundOutcome> out{failed(0, 480, 4), failed(1, 480, 6), failed(2, 300, 9)};
    RoundDecision d = resolveRound(players, out, 3);
    CHECK(d.kind == DecisionKind::Winner);
    CHECK_EQ(d.winner, 1); // player 2 placed more but failed earlier
    CHECK(d.rule == RuleCase::T3);
}

TEST_CASE(rules_T4_full_tie_goes_to_sudden_death_between_tied_only) {
    const std::vector<PlayerRecord> players{{0, 1}, {1, 1}, {2, 1}, {3, 1}};
    const std::vector<RoundOutcome> out{failed(0, 480, 5), failed(1, 480, 5), failed(2, 480, 4), failed(3, 200, 9)};
    RoundDecision d = resolveRound(players, out, 4);
    CHECK(d.kind == DecisionKind::SuddenDeath);
    CHECK_EQ(d.suddenDeath, (std::vector<int>{0, 1}));
    CHECK(d.rule == RuleCase::T4);
    CHECK_EQ(d.winner, -1);
}

TEST_CASE(rules_T5_sudden_death_single_clear_wins) {
    const std::vector<RoundOutcome> out{failed(0, 100, 3), cleared(1)};
    RoundDecision d = resolveSuddenDeath(out);
    CHECK(d.kind == DecisionKind::Winner);
    CHECK_EQ(d.winner, 1);
    CHECK(d.rule == RuleCase::T5);
    CHECK(d.lostTry.empty());
}

TEST_CASE(rules_T6_sudden_death_multiple_clears_repeat_between_them) {
    const std::vector<RoundOutcome> out{cleared(3), failed(0, 100, 3), cleared(1)};
    RoundDecision d = resolveSuddenDeath(out);
    CHECK(d.kind == DecisionKind::SuddenDeath);
    CHECK_EQ(d.suddenDeath, (std::vector<int>{1, 3}));
    CHECK(d.rule == RuleCase::T6);
}

TEST_CASE(rules_T7_sudden_death_all_fail_reapplies_tiebreak) {
    {
        const std::vector<RoundOutcome> out{failed(0, 100, 3), failed(1, 140, 1)};
        RoundDecision d = resolveSuddenDeath(out);
        CHECK(d.kind == DecisionKind::Winner);
        CHECK_EQ(d.winner, 1);
        CHECK(d.rule == RuleCase::T7);
        CHECK(d.subRule == RuleCase::T2);
    }
    {
        const std::vector<RoundOutcome> out{failed(0, 140, 3), failed(1, 140, 1)};
        RoundDecision d = resolveSuddenDeath(out);
        CHECK(d.kind == DecisionKind::Winner);
        CHECK_EQ(d.winner, 0);
        CHECK(d.subRule == RuleCase::T3);
    }
    {
        const std::vector<RoundOutcome> out{failed(0, 140, 3), failed(1, 140, 3)};
        RoundDecision d = resolveSuddenDeath(out);
        CHECK(d.kind == DecisionKind::SuddenDeath);
        CHECK_EQ(d.suddenDeath, (std::vector<int>{0, 1}));
        CHECK(d.subRule == RuleCase::T4);
    }
}

TEST_CASE(rules_T8_simultaneous_timeouts_use_needles_then_sudden_death) {
    const std::vector<PlayerRecord> players{{0, 1}, {1, 1}, {2, 1}};
    {
        const std::vector<RoundOutcome> out{failed(0, 10800, 3, true), failed(1, 10800, 5, true), failed(2, 10800, 2, true)};
        RoundDecision d = resolveRound(players, out, 3);
        CHECK(d.kind == DecisionKind::Winner);
        CHECK_EQ(d.winner, 1);
        CHECK(d.rule == RuleCase::T8);
        CHECK(d.subRule == RuleCase::T3);
    }
    {
        const std::vector<RoundOutcome> out{failed(0, 10800, 5, true), failed(1, 10800, 5, true), failed(2, 10800, 2, true)};
        RoundDecision d = resolveRound(players, out, 3);
        CHECK(d.kind == DecisionKind::SuddenDeath);
        CHECK_EQ(d.suddenDeath, (std::vector<int>{0, 1}));
        CHECK(d.rule == RuleCase::T8);
        CHECK(d.subRule == RuleCase::T4);
    }
}

TEST_CASE(rules_T9_some_eliminated_others_continue) {
    const std::vector<PlayerRecord> players{{0, 1}, {1, 2}, {2, 3}};
    const std::vector<RoundOutcome> out{failed(0, 100, 1), failed(1, 120, 2), cleared(2)};
    RoundDecision d = resolveRound(players, out, 3);
    CHECK(d.kind == DecisionKind::Continue);
    CHECK(d.rule == RuleCase::T9);
    CHECK_EQ(d.eliminated, (std::vector<int>{0}));
    CHECK(contains(d.lostTry, 1));
}

TEST_CASE(rules_T10_single_participant_plays_until_eliminated) {
    const std::vector<PlayerRecord> one{{2, 2}};
    RoundDecision a = resolveRound(one, std::vector<RoundOutcome>{failed(2, 50, 1)}, 1);
    CHECK(a.kind == DecisionKind::Continue);
    CHECK(a.rule == RuleCase::T10);
    const std::vector<PlayerRecord> last{{2, 1}};
    RoundDecision b = resolveRound(last, std::vector<RoundOutcome>{failed(2, 50, 1)}, 1);
    CHECK(b.kind == DecisionKind::NoWinner);
    CHECK_EQ(b.winner, -1);
    RoundDecision c = resolveRound(last, std::vector<RoundOutcome>{cleared(2)}, 1);
    CHECK(c.kind == DecisionKind::Continue);
}

TEST_CASE(rules_never_produce_two_winners) {
    // Exhaustive small search: 3 players, tries 1..2, every clear/fail combination with tied ticks.
    for (int mask = 0; mask < 8; ++mask) {
        for (int triesMask = 0; triesMask < 8; ++triesMask) {
            std::vector<PlayerRecord> players;
            std::vector<RoundOutcome> out;
            for (int p = 0; p < 3; ++p) {
                players.push_back({p, (triesMask >> p & 1) ? 2 : 1});
                out.push_back((mask >> p & 1) ? cleared(p) : failed(p, 100, 3));
            }
            RoundDecision d = resolveRound(players, out, 3);
            if (d.kind == DecisionKind::Winner) CHECK(d.winner >= 0 && d.winner < 3);
            if (d.kind == DecisionKind::SuddenDeath) CHECK(d.suddenDeath.size() >= 2);
            if (d.kind == DecisionKind::NoWinner) CHECK(false); // impossible with 3 participants
        }
    }
}
