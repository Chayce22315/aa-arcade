// The nine game modes: the rule table, the pure resolvers, junk, mutators, daily seeds, and each mode
// played out through real rounds.

#include <algorithm>
#include <functional>
#include <set>

#include "Test.hpp"
#include "collision/Collision.hpp"
#include "gameplay/Geometry.hpp"
#include "gameplay/Match.hpp"
#include "gameplay/Modes.hpp"

using namespace spindle::gameplay;
using spindle::collision::angularDistance;
using spindle::collision::wrapAngle;

namespace {

class Levels final : public LevelProvider {
public:
    explicit Levels(int launch = 5) : launch_(launch) {}
    LevelDef level(int round, std::uint32_t, const std::vector<PowerupKind>&) override {
        LevelDef d;
        d.round = round;
        d.launchCount = launch_;
        d.rotation.initialSpeed = 2.0;
        d.rotation.segments = {{2.0, 0.0, 30.0}};
        return d;
    }

private:
    int launch_;
};

int oracle(const Match& m, int station) {
    const Playfield& f = m.field(station);
    if (!f.canFireAt(m.tickCount() + 1) || f.inFlight() > 0) return 0;
    const auto& c = f.config();
    const double flight = (c.launchDistance - c.needleLength) / c.needleSpeed;
    const RotorState& r = m.rotor().state();
    const double land = wrapAngle(geom::kAttachAngle - (r.angle + r.speed * (flight + geom::kTickSeconds)));
    for (double a : f.attachedAngles())
        if (angularDistance(land, a) < 2.5 * f.minSeparation()) return 0;
    return 1;
}
int spam(const Match& m, int station) { return m.field(station).canFireAt(m.tickCount() + 1) ? 1 : 0; }

using Policy = std::function<int(const Match&, int)>;

void run(Match& m, const std::array<Policy, 4>& policy, int ticks) {
    for (int i = 0; i < ticks && !m.over(); ++i) {
        std::array<int, 4> fire{};
        if (m.phase() == MatchPhase::Playing)
            for (int s = 0; s < 4; ++s)
                if (policy[static_cast<std::size_t>(s)] && m.stationInRound(s)) fire[static_cast<std::size_t>(s)] = policy[static_cast<std::size_t>(s)](m, s);
        m.tick(fire);
    }
}

void runUntilRound(Match& m, const std::array<Policy, 4>& policy, int round) {
    for (int i = 0; i < 600 * geom::kTickRate && !m.over() && !(m.round() == round && m.phase() == MatchPhase::Ready); ++i) run(m, policy, 1);
}

MatchConfig config(ModeId mode, int tries = 2) {
    MatchConfig c;
    c.tries = tries;
    c.roundTimeLimit = 15.0;
    c.readyDelay = 0.25;
    c.resolveDelay = 0.25;
    c.tally = scoring::TallyStyle::Off;
    c.seed = 99;
    c.mode = modeRules(mode);
    return c;
}

std::vector<ParticipantSpec> stations(std::initializer_list<int> list) {
    std::vector<ParticipantSpec> out;
    for (int s : list) out.push_back({s, ParticipantKind::Human});
    return out;
}

constexpr int kSec = geom::kTickRate;

} // namespace

TEST_CASE(modes_table_keys_and_rules) {
    std::set<std::string_view> keys;
    for (ModeId m : kAllModes) {
        keys.insert(modeKey(m));
        CHECK(modeFromKey(modeKey(m)) == m);
        CHECK(!modeBlurb(m).empty());
        CHECK(modeRules(m).id == m);
    }
    CHECK_EQ(keys.size(), kModeCount);
    CHECK(!modeFromKey("nope").has_value());
    CHECK(modeRules(ModeId::Battle).knockouts);
    CHECK(!modeRules(ModeId::Blitz).knockouts);
    CHECK(modeRules(ModeId::Blitz).failPenalty);
    CHECK(modeRules(ModeId::Race).end == MatchEnd::FirstTo);
    CHECK(modeRules(ModeId::Crown).end == MatchEnd::Rounds);
    CHECK(modeRules(ModeId::Teams).teams);
    CHECK(modeRules(ModeId::Sabotage).junk);
    CHECK(modeRules(ModeId::Chaos).mutators);
    CHECK(modeRules(ModeId::Daily).todayOnly && modeRules(ModeId::Daily).dailySeed);
}

TEST_CASE(modes_daily_seed_and_mutator_roll_are_deterministic) {
    CHECK_EQ(dailySeed("2026-11-26"), dailySeed("2026-11-26"));
    CHECK(dailySeed("2026-11-26") != dailySeed("2026-11-27"));
    CHECK(dailySeed("") != 0u);
    std::set<Mutator> seen;
    for (int r = 1; r <= 40; ++r) {
        const Mutator m = rollMutator(1234, r);
        CHECK(m == rollMutator(1234, r));
        CHECK(m != Mutator::None);
        seen.insert(m);
    }
    CHECK(seen.size() >= 4); // the rounds vary
}

TEST_CASE(modes_junk_keeps_levels_valid_and_stops_when_full) {
    LevelLimits limits = defaultLimits();
    LevelDef d;
    d.round = 3;
    d.launchCount = 8;
    d.rotation.initialSpeed = 1.5;
    d.rotation.segments = {{1.5, 0.0, 30.0}};
    spindle::scripting::Pcg32 rng(7, 9);
    CHECK_EQ(addJunk(d, 3, limits, rng), 3);
    CHECK_EQ(d.junk, 3);
    CHECK_EQ(d.prepins.size(), std::size_t{3});
    CHECK(!validateLevel(d, limits).has_value());
    // A crowded circle takes only what fits.
    LevelDef full = d;
    full.launchCount = 32; // (32 + 3 + junk) needles must still pack around the circle
    spindle::scripting::Pcg32 rng2(7, 9);
    const int added = addJunk(full, 10, limits, rng2);
    CHECK(added < 10);
    CHECK(!validateLevel(full, limits).has_value());
}

TEST_CASE(modes_resolvers_endless_and_teams) {
    // Endless: failures cost tries; the match ends only when nobody is left.
    const PlayerRecord players[] = {{0, 1}, {1, 2}};
    const RoundOutcome both[] = {{0, false, 100, 2, false}, {1, false, 120, 3, false}};
    const ScoreTotals totals[] = {{0, 500, 10, 2, 0}, {1, 400, 12, 3, 0}};
    RoundDecision d = resolveEndless(players, both, totals, 2);
    CHECK(d.kind == DecisionKind::Continue); // station 1 still has a try
    CHECK_EQ(d.eliminated.size(), std::size_t{1});
    const PlayerRecord last[] = {{1, 1}};
    const RoundOutcome lastFail[] = {{1, false, 50, 1, false}};
    d = resolveEndless(last, lastFail, totals, 2);
    CHECK(d.kind == DecisionKind::Winner);
    CHECK_EQ(d.winner, 0); // best total wins, not the last one out
    CHECK(d.rule == RuleCase::Score);

    // Teams: a failure costs the team's pool; the team at zero is out.
    const RoundOutcome four[] = {{0, false, 1, 0, false}, {1, true, 0, 5, false}, {2, false, 1, 0, false}, {3, false, 1, 0, false}};
    const int teamOf[] = {0, 0, 1, 1};
    const ScoreTotals t4[] = {{0, 10, 1, 0, 0}, {1, 90, 5, 1, 0}, {2, 10, 1, 0, 0}, {3, 20, 2, 0, 0}};
    TeamDecision td = resolveTeamRound({3, 2}, four, teamOf, t4);
    CHECK_EQ(td.pools[0], 2);
    CHECK_EQ(td.pools[1], 0);
    REQUIRE(td.winningTeam.has_value());
    CHECK_EQ(*td.winningTeam, 0);
    CHECK_EQ(td.decision.winner, 1); // the winning team's best scorer
    CHECK_EQ(td.decision.eliminated.size(), std::size_t{2});
}

TEST_CASE(modes_marathon_plays_on_until_everyone_is_out_and_best_score_wins) {
    Levels levels;
    Match m(config(ModeId::Marathon, 1), stations({0, 1}), levels);
    // Station 1 crashes out in round 1; station 0 keeps playing alone.
    runUntilRound(m, {oracle, spam, nullptr, nullptr}, 2);
    CHECK(m.participant(1)->eliminated);
    CHECK(!m.over());
    runUntilRound(m, {oracle, spam, nullptr, nullptr}, 3);
    CHECK(!m.over());
    run(m, {spam, nullptr, nullptr, nullptr}, 60 * kSec);
    CHECK(m.over());
    CHECK_EQ(m.winnerStation(), 0);
    CHECK(m.lastDecision().rule == RuleCase::Score);
    CHECK_EQ(m.standings().front(), 0);
}

TEST_CASE(modes_blitz_has_no_knockouts_a_clock_and_a_fail_penalty) {
    Levels levels;
    MatchConfig c = config(ModeId::Blitz, 1);
    c.mode.clockSeconds = 8.0;
    Match m(c, stations({0, 2}), levels);
    run(m, {oracle, nullptr, spam, nullptr}, 4 * kSec);
    // Spamming fails every round but never costs a try.
    run(m, {oracle, nullptr, spam, nullptr}, 120 * kSec);
    REQUIRE(m.over());
    CHECK_EQ(m.participant(2)->tries, 1);
    CHECK(!m.participant(2)->eliminated);
    CHECK(m.lastDecision().rule == RuleCase::Clock);
    CHECK_EQ(m.winnerStation(), 0);
    CHECK(m.clockLeft() <= 0.0);
    CHECK(m.participant(0)->score > m.participant(2)->score);
}

TEST_CASE(modes_race_first_clear_takes_the_round_and_the_guard_ends_a_dead_race) {
    Levels levels;
    MatchConfig c = config(ModeId::Race);
    c.mode.target = 2;
    Match m(c, stations({0, 1}), levels);
    run(m, {oracle, spam, nullptr, nullptr}, 120 * kSec);
    REQUIRE(m.over());
    CHECK_EQ(m.winnerStation(), 0);
    CHECK_EQ(m.participant(0)->raceWins, 2);
    CHECK_EQ(m.participant(1)->raceWins, 0);
    CHECK(m.lastDecision().rule == RuleCase::Race);

    Levels levels2;
    Match dead(c, stations({0, 1}), levels2);
    run(dead, {spam, spam, nullptr, nullptr}, 120 * kSec);
    REQUIRE(dead.over());
    CHECK_EQ(dead.roundsPlayed(), kRaceNoClearGuard);
}

TEST_CASE(modes_crown_counts_round_ends_spent_holding_it) {
    Levels levels;
    MatchConfig c = config(ModeId::Crown);
    c.mode.target = 3;
    Match m(c, stations({0, 3}), levels);
    const Policy few = [](const Match& mm, int s) { return mm.field(s).placed() + mm.field(s).inFlight() < 2 ? oracle(mm, s) : 0; };
    run(m, {oracle, nullptr, nullptr, few}, 200 * kSec);
    REQUIRE(m.over());
    CHECK_EQ(m.roundsPlayed(), 3);
    CHECK_EQ(m.participant(0)->crownPoints, 3);
    CHECK_EQ(m.winnerStation(), 0);
    CHECK(m.lastDecision().rule == RuleCase::Crown);
}

TEST_CASE(modes_sabotage_sends_junk_into_rivals_next_round) {
    Levels levels;
    Match m(config(ModeId::Sabotage, 3), stations({0, 1}), levels);
    // Both clear round 1, so each gets one junk needle in round 2.
    runUntilRound(m, {oracle, oracle, nullptr, nullptr}, 2);
    CHECK_EQ(m.junkFor(0), 1);
    CHECK_EQ(m.junkFor(1), 1);
    const auto& att = m.field(1).attached();
    CHECK(std::count_if(att.begin(), att.end(), [](const AttachedNeedle& a) { return a.junk; }) == 1);
    // A battle match never gets junk.
    Levels levels2;
    Match b(config(ModeId::Battle, 3), stations({0, 1}), levels2);
    runUntilRound(b, {oracle, oracle, nullptr, nullptr}, 2);
    CHECK_EQ(b.junkFor(0), 0);
}

TEST_CASE(modes_teams_share_a_pool_and_the_last_team_in_wins) {
    Levels levels;
    Match m(config(ModeId::Teams, 1), stations({0, 1, 2, 3}), levels);
    CHECK_EQ(m.participant(0)->team, 0);
    CHECK_EQ(m.participant(3)->team, 1);
    CHECK_EQ(m.teamPool(0), 2);
    CHECK_EQ(m.maxTries(), 2);
    // Team 0 crashes twice in round 1 (pool 2 -> 0); team 1 clears.
    run(m, {spam, spam, oracle, oracle}, 60 * kSec);
    REQUIRE(m.over());
    REQUIRE(m.winningTeam().has_value());
    CHECK_EQ(*m.winningTeam(), 1);
    CHECK(m.winnerStation() == 2 || m.winnerStation() == 3);
    CHECK(m.participant(0)->eliminated && m.participant(1)->eliminated);
    CHECK(m.lastDecision().rule == RuleCase::Team);
}

TEST_CASE(modes_chaos_rolls_mutators_that_keep_rounds_playable) {
    Levels levels;
    Match m(config(ModeId::Chaos, 9), stations({0, 1}), levels);
    std::set<Mutator> seen;
    for (int r = 2; r <= 12 && !m.over(); ++r) {
        seen.insert(m.mutator());
        const double radius = m.field(0).config().ballRadius;
        if (m.mutator() == Mutator::Tiny) CHECK(radius < geom::kBallRadius);
        if (m.mutator() == Mutator::Big) CHECK(radius > geom::kBallRadius);
        if (m.mutator() == Mutator::Extra) CHECK_EQ(m.field(0).needlesLeft(), 5 + kExtraNeedles);
        CHECK_EQ(m.field(0).config().ballRadius, m.field(1).config().ballRadius); // the same for everyone
        runUntilRound(m, {oracle, oracle, nullptr, nullptr}, r);
    }
    CHECK(seen.size() >= 3);
    Levels levels2;
    Match b(config(ModeId::Battle), stations({0, 1}), levels2);
    CHECK(b.mutator() == Mutator::None);
}
