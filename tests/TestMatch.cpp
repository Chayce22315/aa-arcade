// Match-level scenarios: shared rotor, independent stations, tries, elimination, no rejoin and the
// tie-break cases played out through real rounds.

#include <algorithm>
#include <functional>

#include "Test.hpp"
#include "collision/Collision.hpp"
#include "gameplay/Geometry.hpp"
#include "gameplay/Match.hpp"

using namespace spindle::gameplay;
using spindle::collision::angularDistance;
using spindle::collision::wrapAngle;

namespace {

class FixedLevels final : public LevelProvider {
public:
    explicit FixedLevels(int launch = 5, double speed = 2.0) : launch_(launch), speed_(speed) {}
    LevelDef level(int round, std::uint32_t, const std::vector<PowerupKind>&) override {
        LevelDef d;
        d.round = round;
        d.launchCount = launch_;
        d.rotation.initialSpeed = speed_;
        d.rotation.segments = {{speed_, 0.0, 30.0}};
        return d;
    }
    int calls = 0;

private:
    int launch_;
    double speed_;
};

// Fires only when the needle would land with at least 2x the minimum separation.
int oracle(const Match& m, int station) {
    const Playfield& f = m.field(station);
    if (!f.canFireAt(m.tickCount() + 1) || f.inFlight() > 0) return 0;
    const auto& c = f.config();
    const double flight = (c.launchDistance - c.needleLength) / c.needleSpeed;
    const RotorState& r = m.rotor().state();
    const double land = wrapAngle(geom::kAttachAngle - (r.angle + r.speed * (flight + geom::kTickSeconds)));
    for (double a : f.attachedAngles())
        if (angularDistance(land, a) < 2.5 * c.minSeparation) return 0;
    return 1;
}

int spam(const Match& m, int station) { return m.field(station).canFireAt(m.tickCount() + 1) ? 1 : 0; }
int idle(const Match&, int) { return 0; }

using Policy = std::function<int(const Match&, int)>;

void runTicks(Match& m, const std::array<Policy, 4>& policy, int ticks) {
    for (int i = 0; i < ticks && !m.over(); ++i) {
        std::array<int, 4> fire{};
        if (m.phase() == MatchPhase::Playing)
            for (int s = 0; s < 4; ++s)
                if (policy[static_cast<std::size_t>(s)] && m.stationInRound(s)) fire[static_cast<std::size_t>(s)] = policy[static_cast<std::size_t>(s)](m, s);
        m.tick(fire);
    }
}

void runUntilRound(Match& m, const std::array<Policy, 4>& policy, int round, int maxTicks = 600 * geom::kTickRate) {
    for (int i = 0; i < maxTicks && !m.over() && !(m.round() == round && m.phase() == MatchPhase::Ready); ++i) runTicks(m, policy, 1);
}

MatchConfig config(int tries) {
    MatchConfig c;
    c.tries = tries;
    c.roundTimeLimit = 20.0;
    c.readyDelay = 0.5;
    c.resolveDelay = 0.5;
    c.tally = scoring::TallyStyle::Off;
    c.seed = 7;
    return c;
}

} // namespace

TEST_CASE(match_clearing_rounds_costs_no_tries_and_all_start_together) {
    FixedLevels levels;
    Match m(config(3), {{0, ParticipantKind::Human}, {2, ParticipantKind::Bot}}, levels);
    CHECK_EQ(m.participantCount(), 2);
    runUntilRound(m, {oracle, nullptr, oracle, nullptr}, 4);
    CHECK_EQ(m.round(), 4);
    CHECK_EQ(m.participant(0)->tries, 3);
    CHECK_EQ(m.participant(2)->tries, 3);
    CHECK_EQ(m.participant(0)->roundsCleared, 3);
    // Both stations got the same level at the same moment.
    CHECK(m.stationInRound(0) && m.stationInRound(2));
    CHECK(!m.stationInRound(1));
    CHECK_EQ(m.field(0).launchCount(), m.field(2).launchCount());
}

TEST_CASE(match_failures_cost_tries_then_eliminate_and_never_rejoin) {
    FixedLevels levels;
    Match m(config(3), {{0, ParticipantKind::Human}, {1, ParticipantKind::Human}, {3, ParticipantKind::Bot}}, levels);
    runUntilRound(m, {oracle, spam, nullptr, oracle}, 2);
    CHECK_EQ(m.participant(1)->tries, 2);
    runUntilRound(m, {oracle, spam, nullptr, oracle}, 4);
    CHECK_EQ(m.participant(1)->tries, 0);
    CHECK(m.participant(1)->eliminated);
    CHECK_EQ(m.participant(1)->eliminatedRound, 3);
    // Station 1 keeps pressing fire: it is not in the round, its field stays idle, nothing changes.
    runUntilRound(m, {oracle, spam, nullptr, oracle}, 6);
    CHECK(!m.stationInRound(1));
    CHECK(m.field(1).status() == FieldStatus::Idle);
    CHECK_EQ(m.participant(1)->tries, 0);
    CHECK(!m.over());
}

TEST_CASE(match_T1_last_survivor_wins) {
    FixedLevels levels;
    Match m(config(2), {{0, ParticipantKind::Human}, {1, ParticipantKind::Bot}}, levels);
    runTicks(m, {oracle, spam, nullptr, nullptr}, 120 * geom::kTickRate);
    CHECK(m.over());
    CHECK_EQ(m.winnerStation(), 0);
    CHECK(m.lastDecision().rule == RuleCase::T1);
    CHECK_EQ(m.standings().front(), 0);
}

TEST_CASE(match_T2_later_failure_wins) {
    FixedLevels levels;
    Match m(config(1), {{0, ParticipantKind::Human}, {1, ParticipantKind::Human}}, levels);
    Policy late = [](const Match& mm, int s) { return mm.roundTime() > 0.4 ? spam(mm, s) : 0; };
    runTicks(m, {spam, late, nullptr, nullptr}, 60 * geom::kTickRate);
    CHECK(m.over());
    CHECK_EQ(m.winnerStation(), 1);
    CHECK(m.lastDecision().rule == RuleCase::T2);
}

TEST_CASE(match_T4_then_T5_sudden_death) {
    FixedLevels levels;
    Match m(config(1), {{0, ParticipantKind::Human}, {2, ParticipantKind::Human}}, levels);
    // Round 1: identical spamming -> same failure tick and needle count -> sudden death.
    runUntilRound(m, {spam, nullptr, spam, nullptr}, 2);
    CHECK(m.suddenDeath());
    CHECK(m.lastDecision().rule == RuleCase::T4);
    CHECK(m.stationInRound(0) && m.stationInRound(2));
    // Sudden death: station 2 plays carefully and wins.
    runTicks(m, {spam, nullptr, oracle, nullptr}, 60 * geom::kTickRate);
    CHECK(m.over());
    CHECK_EQ(m.winnerStation(), 2);
    CHECK(m.lastDecision().rule == RuleCase::T5);
}

TEST_CASE(match_T6_sudden_death_repeats_while_both_clear) {
    FixedLevels levels;
    Match m(config(1), {{0, ParticipantKind::Human}, {1, ParticipantKind::Human}}, levels);
    runUntilRound(m, {spam, spam, nullptr, nullptr}, 2);
    REQUIRE(m.suddenDeath());
    runUntilRound(m, {oracle, oracle, nullptr, nullptr}, 3);
    CHECK(m.suddenDeath());
    CHECK(m.lastDecision().rule == RuleCase::T6);
    runTicks(m, {oracle, spam, nullptr, nullptr}, 60 * geom::kTickRate);
    CHECK(m.over());
    CHECK_EQ(m.winnerStation(), 0);
}

TEST_CASE(match_T8_timeouts_then_T11_abandon_guard_gives_one_winner) {
    FixedLevels levels;
    MatchConfig c = config(1);
    c.roundTimeLimit = 10.0;
    Match m(c, {{1, ParticipantKind::Human}, {3, ParticipantKind::Human}}, levels);
    runUntilRound(m, {nullptr, idle, nullptr, idle}, 2);
    CHECK(m.lastDecision().rule == RuleCase::T8);
    CHECK(m.lastDecision().subRule == RuleCase::T4);
    CHECK(m.suddenDeath());
    // Nobody ever plays: after the guard's limit the lowest station wins on equal totals.
    runTicks(m, {nullptr, idle, nullptr, idle}, 200 * geom::kTickRate);
    CHECK(m.over());
    CHECK_EQ(m.winnerStation(), 1);
    CHECK(m.lastDecision().rule == RuleCase::T11);
}

TEST_CASE(match_single_participant_runs_until_eliminated_without_winner) {
    FixedLevels levels;
    Match m(config(2), {{2, ParticipantKind::Human}}, levels);
    runUntilRound(m, {nullptr, nullptr, oracle, nullptr}, 3);
    CHECK_EQ(m.participant(2)->tries, 2);
    runTicks(m, {nullptr, nullptr, spam, nullptr}, 60 * geom::kTickRate);
    CHECK(m.over());
    CHECK_EQ(m.winnerStation(), -1);
    CHECK(m.lastDecision().kind == DecisionKind::NoWinner);
    CHECK_EQ(m.participant(2)->roundsCleared, 2);
}

TEST_CASE(match_pause_freezes_everything) {
    FixedLevels levels;
    Match m(config(3), {{0, ParticipantKind::Human}, {1, ParticipantKind::Human}}, levels);
    runTicks(m, {oracle, oracle, nullptr, nullptr}, geom::kTickRate);
    const double angle = m.rotor().state().angle;
    const auto ticks = m.tickCount();
    m.setPaused(true);
    runTicks(m, {spam, spam, nullptr, nullptr}, geom::kTickRate);
    CHECK_EQ(m.rotor().state().angle, angle);
    CHECK_EQ(m.tickCount(), ticks);
    m.setPaused(false);
    runTicks(m, {oracle, oracle, nullptr, nullptr}, 10);
    CHECK(m.tickCount() > ticks);
}

// ------------------------------------------------------------------ score and crown

namespace {

void runUntilTally(Match& m, const std::array<Policy, 4>& policy, int maxTicks = 120 * geom::kTickRate) {
    for (int i = 0; i < maxTicks && !m.over() && m.phase() != MatchPhase::Tally; ++i) runTicks(m, policy, 1);
}

// Places needles like the oracle until `count` are in, then waits for the round clock.
Policy upTo(int count) {
    return [count](const Match& m, int s) { return m.field(s).placed() + m.field(s).inFlight() < count ? oracle(m, s) : 0; };
}

std::vector<MatchEvent> eventsOf(std::vector<MatchEvent> all, MatchEventType t) {
    std::erase_if(all, [t](const MatchEvent& e) { return e.type != t; });
    return all;
}

} // namespace

TEST_CASE(match_score_adds_round_points_and_bonus_at_the_round_end) {
    FixedLevels levels;
    Match m(config(3), {{0, ParticipantKind::Human}, {2, ParticipantKind::Bot}}, levels);
    runUntilTally(m, {oracle, nullptr, oracle, nullptr});
    REQUIRE(m.phase() == MatchPhase::Tally);
    for (int s : {0, 2}) {
        const Participant& p = *m.participant(s);
        CHECK(m.tallied(s));
        CHECK(p.roundPoints >= 5 * scoring::kNeedle); // five needles at round 1
        CHECK(p.roundBonus >= scoring::kClear);       // both cleared
        CHECK_EQ(p.scoreBefore, std::int64_t{0});
        CHECK_EQ(p.score, p.roundPoints + p.roundBonus);
    }
    CHECK(!m.tallied(1));
    const auto scored = eventsOf(m.takeEvents(), MatchEventType::RoundScored);
    CHECK_EQ(scored.size(), std::size_t{2});
    // Round 2 starts from the banked totals with fresh round points.
    const std::int64_t before = m.participant(0)->score;
    runUntilRound(m, {oracle, nullptr, oracle, nullptr}, 2);
    CHECK_EQ(m.participant(0)->score, before);
    CHECK_EQ(m.participant(0)->roundPoints, std::int64_t{0});
}

TEST_CASE(match_crown_goes_to_most_round_points_and_ties_change_nothing) {
    FixedLevels levels;
    Match m(config(3), {{0, ParticipantKind::Human}, {1, ParticipantKind::Bot}}, levels);
    runUntilTally(m, {oracle, upTo(2), nullptr, nullptr});
    REQUIRE(m.phase() == MatchPhase::Tally);
    CHECK(m.participant(0)->roundPoints > m.participant(1)->roundPoints);
    CHECK_EQ(m.crownHolder(), 0);
    CHECK_EQ(m.participant(0)->crownsWon, 1);
    const auto taken = eventsOf(m.takeEvents(), MatchEventType::CrownTaken);
    REQUIRE(taken.size() == 1);
    CHECK_EQ(taken[0].station, 0);
    CHECK_EQ(taken[0].value, -1);

    // Identical play on the shared rotor ties the round points: nobody new takes the crown.
    FixedLevels levels2;
    Match tie(config(3), {{0, ParticipantKind::Human}, {3, ParticipantKind::Human}}, levels2);
    runUntilTally(tie, {oracle, nullptr, nullptr, oracle});
    REQUIRE(tie.phase() == MatchPhase::Tally);
    CHECK_EQ(tie.participant(0)->roundPoints, tie.participant(3)->roundPoints);
    CHECK_EQ(tie.crownHolder(), -1);
}

TEST_CASE(match_crown_holder_knocked_out_loses_it) {
    FixedLevels levels;
    Match m(config(2), {{0, ParticipantKind::Human}, {1, ParticipantKind::Bot}, {2, ParticipantKind::Bot}}, levels);
    // Round 1: station 0 clears with five needles; 1 and 2 place three each and time out.
    runUntilTally(m, {oracle, upTo(3), upTo(3), nullptr}, 60 * geom::kTickRate);
    REQUIRE(m.phase() == MatchPhase::Tally);
    CHECK_EQ(m.crownHolder(), 0);
    // Round 2: the holder crashes (one try left); 1 and 2 tie, so the crown stays with station 0.
    runUntilRound(m, {oracle, oracle, oracle, nullptr}, 2);
    m.takeEvents();
    runUntilTally(m, {spam, oracle, oracle, nullptr});
    REQUIRE(m.phase() == MatchPhase::Tally);
    CHECK(!m.participant(0)->eliminated);
    CHECK_EQ(m.crownHolder(), 0);
    // Round 3: the holder crashes out for good and loses the crown; the tie behind it gives it to nobody.
    runUntilRound(m, {oracle, oracle, oracle, nullptr}, 3);
    m.takeEvents();
    runUntilTally(m, {spam, oracle, oracle, nullptr});
    REQUIRE(m.phase() == MatchPhase::Tally);
    CHECK(m.participant(0)->eliminated);
    CHECK_EQ(m.crownHolder(), -1);
    const auto lost = eventsOf(m.takeEvents(), MatchEventType::CrownLost);
    REQUIRE(lost.size() == 1);
    CHECK_EQ(lost[0].station, 0);
}
