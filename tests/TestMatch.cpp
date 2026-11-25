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
    LevelDef level(int round, std::uint32_t) override {
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
