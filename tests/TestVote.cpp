// The game mode vote: cursors, votes, the clock rules, ties and the wheel, and the cabinet flow.

#include <cmath>
#include <map>
#include <memory>

#include "Test.hpp"
#include "arcade/ArcadeMachine.hpp"
#include "arcade/VoteBox.hpp"
#include "gameplay/Geometry.hpp"
#include "gameplay/LevelScript.hpp"

using namespace spindle;
using arcade::ArcadeMachine;
using arcade::ArcadeState;
using arcade::VoteBox;
using arcade::VotePhase;
using gameplay::ModeId;

namespace {

constexpr int kSecond = gameplay::geom::kTickRate;

std::vector<ModeId> allModes() { return {gameplay::kAllModes.begin(), gameplay::kAllModes.end()}; }

arcade::BotFactory factory() {
    return [](config::Difficulty d, std::uint64_t seed) -> std::unique_ptr<bots::BotController> {
        std::string err;
        auto brain = bots::BotBrain::create(bots::BotBrain::embeddedSource(), d, seed, &err);
        if (!brain) return nullptr;
        return std::make_unique<bots::BotController>(std::move(brain));
    };
}

arcade::ArcadeInput at(int station, int fire, bool start, int left = 0, int right = 0) {
    arcade::ArcadeInput in;
    auto& b = in.stations[static_cast<std::size_t>(station)];
    b.firePresses = fire;
    b.startPressed = start;
    b.leftPresses = left;
    b.rightPresses = right;
    return in;
}

} // namespace

TEST_CASE(vote_cursors_votes_and_revotes) {
    VoteBox v(allModes(), 30.0);
    v.join(0);
    v.join(2);
    CHECK(v.joined(0) && v.joined(2) && !v.joined(1));
    CHECK_EQ(v.cursor(0), 0);
    v.move(0, -1); // wraps to the last card
    CHECK_EQ(v.cursor(0), 8);
    v.move(0, 2);
    CHECK_EQ(v.cursor(0), 1);
    v.cast(0);
    CHECK(v.vote(0) == 1);
    CHECK_EQ(v.votesFor(1), 1);
    v.move(0, 1);
    v.cast(0); // changing a vote moves it
    CHECK_EQ(v.votesFor(1), 0);
    CHECK_EQ(v.votesFor(2), 1);
    v.move(1, 3); // not joined: nothing
    v.cast(1);
    CHECK_EQ(v.cursor(1), 0);
    CHECK(!v.vote(1));
    CHECK(!v.allVoted());
}

TEST_CASE(vote_clock_drops_when_all_voted_and_a_late_join_pushes_it_back) {
    VoteBox v(allModes(), 30.0);
    v.join(0);
    v.tick(10.0);
    CHECK_NEAR(v.timeLeft(), 20.0, 1e-9);
    v.cast(0);
    CHECK(v.allVoted());
    CHECK_NEAR(v.timeLeft(), VoteBox::kAllVotedSeconds, 1e-9);
    v.tick(4.0);
    v.join(1); // one second left: back up to five, and the new player has not voted
    CHECK_NEAR(v.timeLeft(), VoteBox::kLateJoinSeconds, 1e-9);
    CHECK(!v.allVoted());
    v.tick(6.0);
    CHECK(v.expired());
    CHECK_NEAR(v.timeLeft(), 0.0, 1e-9);
}

TEST_CASE(vote_leaders_ties_and_nobody_voting) {
    VoteBox v(allModes(), 30.0);
    v.join(0);
    v.join(1);
    v.join(2);
    CHECK_EQ(v.leaders().size(), std::size_t{9}); // nobody voted: every card
    v.cast(0);                                     // battle
    v.move(1, 2);
    v.cast(1); // blitz
    v.move(2, 2);
    v.cast(2); // blitz
    REQUIRE(v.leaders().size() == 1);
    CHECK_EQ(v.leaders().front(), 2);
    v.move(2, 1);
    v.cast(2); // race: battle, blitz and race tie one each
    CHECK_EQ(v.leaders().size(), std::size_t{3});
}

TEST_CASE(vote_wheel_stops_on_its_result_and_every_slice_is_fair) {
    for (int n = 2; n <= 9; ++n)
        for (int r = 0; r < n; ++r) {
            const double end = arcade::wheelAngle(VoteBox::kSpinSeconds, n, r);
            CHECK_EQ(arcade::wheelSliceAt(end, n), r);
            CHECK(arcade::wheelAngle(0.0, n, r) == 0.0);
            CHECK(end > arcade::wheelAngle(VoteBox::kSpinSeconds * 0.5, n, r)); // it keeps turning one way
        }
    // The arcade draws the result uniformly: over many machines both slices of a 50/50 come up evenly.
    std::map<ModeId, int> wins;
    gameplay::LevelScript levels;
    config::Settings s = config::Settings::defaults();
    s.arcade.freePlay = true;
    s.arcade.voteSeconds = 10;
    for (std::uint64_t seed = 1; seed <= 200; ++seed) {
        ArcadeMachine m(s, levels, factory(), {}, seed * 7919);
        m.tick(at(0, 0, true));
        m.tick(at(1, 0, true));
        m.tick(at(0, 1, false));        // battle
        m.tick(at(1, 1, false, 0, 1)); // right to marathon, vote
        for (int i = 0; i < 12 * kSecond && m.votePhase() == VotePhase::Voting; ++i) m.tick({});
        REQUIRE(m.votePhase() == VotePhase::Spin);
        CHECK_EQ(m.wheel().size(), std::size_t{2});
        for (int i = 0; i < 5 * kSecond && m.votePhase() == VotePhase::Spin; ++i) m.tick({});
        REQUIRE(m.votePhase() == VotePhase::Intro);
        ++wins[m.chosenMode()];
    }
    CHECK_EQ(wins.size(), std::size_t{2});
    CHECK(wins[ModeId::Battle] > 70 && wins[ModeId::Marathon] > 70);
}

TEST_CASE(vote_outright_winner_plays_its_mode) {
    gameplay::LevelScript levels;
    config::Settings s = config::Settings::defaults();
    s.arcade.freePlay = true;
    s.arcade.voteSeconds = 10;
    s.bots.difficulty = config::Difficulty::Expert;
    ArcadeMachine m(s, levels, factory(), {}, 3);
    m.tick(at(2, 0, true));
    REQUIRE(m.state() == ArcadeState::Vote);
    m.tick(at(2, 0, false, 0, 2)); // two cards right: blitz
    m.tick(at(2, 1, false));
    CHECK_NEAR(m.voteTimeLeft(), VoteBox::kAllVotedSeconds, 0.01);
    for (int i = 0; i < 12 * kSecond && m.state() == ArcadeState::Vote; ++i) m.tick({});
    REQUIRE(m.state() == ArcadeState::Playing);
    CHECK(m.currentMode() == ModeId::Blitz);
    CHECK(m.match()->mode().id == ModeId::Blitz);
    CHECK_EQ(m.scoreTable(), std::string_view("blitz"));
    // Joining is closed once the match runs.
    m.tick(at(0, 0, true));
    CHECK(m.lineup().seats[0] != players::SeatKind::Human);
}

TEST_CASE(vote_settings_switch_modes_off_and_teams_fill_every_seat) {
    gameplay::LevelScript levels;
    config::Settings s = config::Settings::defaults();
    s.arcade.freePlay = true;
    s.modes.enabled.fill(false);
    {
        ArcadeMachine m(s, levels, factory(), {}, 5);
        CHECK_EQ(m.enabledModes().size(), std::size_t{1}); // all off: battle stays on
        CHECK(m.enabledModes().front() == ModeId::Battle);
    }
    s.modes.enabled[static_cast<std::size_t>(ModeId::Teams)] = true;
    ArcadeMachine m(s, levels, factory(), {}, 5);
    REQUIRE(m.enabledModes().size() == 1);
    m.tick(at(1, 0, true));
    m.tick(at(1, 0, true)); // one mode on: pressing again starts (via its card)
    CHECK(m.votePhase() == VotePhase::Intro);
    for (int i = 0; i < 3 * kSecond && m.state() == ArcadeState::Vote; ++i) m.tick({});
    REQUIRE(m.state() == ArcadeState::Playing);
    CHECK(m.currentMode() == ModeId::Teams);
    CHECK_EQ(m.lineup().humans + m.lineup().bots, 4); // bots fill both teams
    CHECK_EQ(m.match()->participant(1)->team, 0);
}

TEST_CASE(vote_daily_uses_the_date_seed_and_a_today_only_table) {
    gameplay::LevelScript levels;
    config::Settings s = config::Settings::defaults();
    s.arcade.freePlay = true;
    s.gameplay.seed = 777; // ignored by daily
    s.modes.enabled.fill(false);
    s.modes.enabled[static_cast<std::size_t>(ModeId::Daily)] = true;
    ArcadeMachine m(s, levels, factory(), {}, 9);
    m.setToday("2026-11-26");
    m.tick(at(0, 0, true));
    m.tick(at(0, 0, true));
    for (int i = 0; i < 3 * kSecond && m.state() == ArcadeState::Vote; ++i) m.tick({});
    REQUIRE(m.state() == ArcadeState::Playing);
    CHECK_EQ(m.match()->config().seed, gameplay::dailySeed("2026-11-26"));
    CHECK(gameplay::modeRules(m.currentMode()).todayOnly);
}
