// Cabinet flow: attract -> coin -> join -> match -> game over -> attract, with bots and humans.

#include <algorithm>
#include <memory>

#include "Test.hpp"
#include "arcade/ArcadeMachine.hpp"
#include "collision/Collision.hpp"
#include "gameplay/Geometry.hpp"
#include "gameplay/LevelScript.hpp"

using namespace spindle;
using arcade::ArcadeInput;
using arcade::ArcadeMachine;
using arcade::ArcadeState;

namespace {

arcade::BotFactory factory() {
    return [](config::Difficulty d, std::uint64_t seed) -> std::unique_ptr<bots::BotController> {
        std::string err;
        auto brain = bots::BotBrain::create(bots::BotBrain::embeddedSource(), d, seed, &err);
        if (!brain) return nullptr;
        return std::make_unique<bots::BotController>(std::move(brain));
    };
}

config::Settings testSettings() {
    config::Settings s = config::Settings::defaults();
    s.gameplay.seed = 1234;
    s.arcade.voteSeconds = 10;
    s.arcade.gameOverTime = 3;
    s.gameplay.roundTimeLimit = 20;
    return s;
}

void ticks(ArcadeMachine& m, int n, ArcadeInput in = {}) {
    for (int i = 0; i < n; ++i) m.tick(in);
}

ArcadeInput startAt(int station) {
    ArcadeInput in;
    in.stations[static_cast<std::size_t>(station)].startPressed = true;
    return in;
}

ArcadeInput coin() {
    ArcadeInput in;
    in.coins = 1;
    return in;
}

constexpr int kSecond = gameplay::geom::kTickRate;

// Each listed (joined) station votes for the card under its cursor - battle, the first card - and the
// vote screen runs out: everyone voted, so 5 s, then the mode's card, then the match.
void voteBattleAndPlay(ArcadeMachine& m, std::initializer_list<int> voters) {
    for (int s : voters) m.tick(startAt(s));
    for (int i = 0; i < 20 * kSecond && m.state() == ArcadeState::Vote; ++i) m.tick({});
}

} // namespace

TEST_CASE(arcade_attract_cycles_pages_and_runs_a_bot_demo) {
    gameplay::LevelScript levels;
    ArcadeMachine m(testSettings(), levels, factory(), {}, 1);
    CHECK(m.state() == ArcadeState::Attract);
    CHECK(m.attractPage() == arcade::AttractPage::Title);
    ticks(m, static_cast<int>(ArcadeMachine::kTitleTime * kSecond) + 1);
    CHECK(m.attractPage() == arcade::AttractPage::HowTo);
    ticks(m, static_cast<int>(ArcadeMachine::kHowToTime * kSecond) + 1);
    CHECK(m.attractPage() == arcade::AttractPage::Demo);
    REQUIRE(m.match() != nullptr);
    CHECK_EQ(m.lineup().bots, 4);
    ticks(m, 5 * kSecond);
    int placed = 0;
    for (int s = 0; s < 4; ++s) placed += m.match()->field(s).placed() + (m.match()->participant(s)->totalPlaced);
    CHECK(placed > 0); // bots are actually playing
    ticks(m, static_cast<int>(ArcadeMachine::kDemoMaxTime * kSecond));
    CHECK(m.state() == ArcadeState::Attract);
    CHECK(m.attractPage() != arcade::AttractPage::Demo);
}

TEST_CASE(arcade_start_without_credit_is_refused) {
    gameplay::LevelScript levels;
    ArcadeMachine m(testSettings(), levels, factory(), {}, 1);
    m.tick(startAt(0));
    CHECK(m.state() == ArcadeState::Attract);
    CHECK(m.needCoinFlash(0) > 0.0);
    bool refused = false;
    for (const auto& e : m.takeEvents()) refused |= e.type == arcade::ArcadeEventType::JoinRefused;
    CHECK(refused);
}

TEST_CASE(arcade_full_cycle_singleplayer_with_bots) {
    gameplay::LevelScript levels;
    config::Settings s = testSettings();
    s.bots.singleplayerBots = 3;
    s.bots.difficulty = config::Difficulty::Expert;
    ArcadeMachine m(s, levels, factory(), {}, 1);
    m.tick(coin());
    CHECK(m.state() == ArcadeState::InsertCoin);
    CHECK_EQ(m.credits().credits(), 1);
    CHECK_EQ(m.bookkeeping().coins, std::uint64_t{1});
    m.tick(startAt(2));
    CHECK(m.state() == ArcadeState::Vote);
    CHECK_EQ(m.credits().credits(), 0);
    CHECK(m.joined()[2]);
    voteBattleAndPlay(m, {2});
    REQUIRE(m.state() == ArcadeState::Playing);
    CHECK(m.currentMode() == gameplay::ModeId::Battle);
    CHECK(m.lineup().mode == players::GameMode::Singleplayer);
    CHECK_EQ(m.lineup().humans, 1);
    CHECK_EQ(m.lineup().bots, 3);
    CHECK(m.lineup().seats[2] == players::SeatKind::Human);
    CHECK_EQ(m.bookkeeping().gamesSingleplayer, std::uint64_t{1});
    // The human never fires: each round costs a try; expert bots finish it off.
    ticks(m, 900 * kSecond);
    REQUIRE(m.state() != ArcadeState::Playing);
    REQUIRE(m.lastResult().has_value());
    CHECK(m.lastResult()->winner >= 0);
    CHECK(m.lastResult()->winner != 2);
    CHECK_EQ(m.bookkeeping().recentWinners.size(), std::size_t{1});
    CHECK_EQ(m.bookkeeping().botWins, std::uint64_t{1});
    ticks(m, (s.arcade.gameOverTime + 1) * kSecond);
    CHECK(m.state() == ArcadeState::Attract); // no credits left
}

TEST_CASE(arcade_multiplayer_join_and_early_start) {
    gameplay::LevelScript levels;
    config::Settings s = testSettings();
    s.bots.multiplayerBots = 1;
    ArcadeMachine m(s, levels, factory(), {}, 1);
    m.tick(coin());
    m.tick(coin());
    m.tick(startAt(0));
    m.tick(startAt(3));
    CHECK(m.joined()[0] && m.joined()[3]);
    m.tick(startAt(1)); // no credit left
    CHECK(!m.joined()[1]);
    CHECK(m.needCoinFlash(1) > 0.0);
    voteBattleAndPlay(m, {0, 3}); // both vote: the clock drops to 5 s
    REQUIRE(m.state() == ArcadeState::Playing);
    CHECK(m.lineup().mode == players::GameMode::Multiplayer);
    CHECK_EQ(m.lineup().humans, 2);
    CHECK_EQ(m.lineup().bots, 1);
    CHECK(m.lineup().seats[1] == players::SeatKind::Bot);
    // Start presses during a match never add players.
    m.tick(startAt(2));
    CHECK(m.lineup().seats[2] == players::SeatKind::Empty);
    CHECK_EQ(m.bookkeeping().gamesMultiplayer, std::uint64_t{1});
}

TEST_CASE(arcade_free_play_and_coin_during_game_over_returns_to_press_start) {
    gameplay::LevelScript levels;
    config::Settings s = testSettings();
    s.arcade.freePlay = true;
    s.bots.singleplayerBots = 1;
    s.bots.difficulty = config::Difficulty::Expert;
    ArcadeMachine m(s, levels, factory(), {}, 1);
    m.tick(startAt(1));
    CHECK(m.state() == ArcadeState::Vote);
    voteBattleAndPlay(m, {1});
    REQUIRE(m.state() == ArcadeState::Playing);
    for (int i = 0; i < 900 * kSecond && m.state() == ArcadeState::Playing; ++i) m.tick({});
    REQUIRE(m.state() == ArcadeState::GameOver);
    ticks(m, (s.arcade.gameOverTime + 1) * kSecond);
    CHECK(m.state() == ArcadeState::InsertCoin); // free play: straight back to "press start"
}

TEST_CASE(arcade_disconnect_pauses_and_start_resumes) {
    gameplay::LevelScript levels;
    config::Settings s = testSettings();
    s.arcade.freePlay = true;
    s.bots.singleplayerBots = 1;
    ArcadeMachine m(s, levels, factory(), {}, 1);
    m.tick(startAt(0));
    voteBattleAndPlay(m, {0});
    REQUIRE(m.state() == ArcadeState::Playing);
    ticks(m, 3 * kSecond);
    m.stationDisconnected(0);
    CHECK(m.pausedForDisconnect());
    CHECK(m.match()->paused());
    const auto t = m.match()->tickCount();
    ticks(m, kSecond);
    CHECK_EQ(m.match()->tickCount(), t);
    m.tick(startAt(0)); // keyboard takes over
    CHECK(!m.pausedForDisconnect());
    ticks(m, 10);
    CHECK(m.match()->tickCount() > t);
    // A bot station losing a device does not pause.
    m.stationDisconnected(1);
    CHECK(!m.pausedForDisconnect());
    // continue mode never pauses.
    s.input.disconnectAction = config::DisconnectAction::Continue;
    m.applySettings(s);
    m.stationDisconnected(0);
    CHECK(!m.pausedForDisconnect());
}

TEST_CASE(arcade_service_menu_freezes_the_cabinet) {
    gameplay::LevelScript levels;
    ArcadeMachine m(testSettings(), levels, factory(), {}, 1);
    m.setServiceOpen(true);
    m.tick(coin());
    CHECK_EQ(m.credits().credits(), 0);
    const double t = m.stateTime();
    ticks(m, kSecond);
    CHECK_EQ(m.stateTime(), t);
    m.setServiceOpen(false);
    m.tick(coin());
    CHECK_EQ(m.credits().credits(), 1);
}

// ------------------------------------------------------------------ high scores and initials

namespace {

// Fires when the needle would land well clear of every attached needle (as in TestMatch).
int aimed(const gameplay::Match& m, int s) {
    const gameplay::Playfield& f = m.field(s);
    if (!f.canFireAt(m.tickCount() + 1) || f.inFlight() > 0) return 0;
    const auto& c = f.config();
    const double flight = (c.launchDistance - c.needleLength) / c.needleSpeed;
    const auto& r = m.rotor().state();
    const double land = collision::wrapAngle(gameplay::geom::kAttachAngle - (r.angle + r.speed * (flight + gameplay::geom::kTickSeconds)));
    for (double a : f.attachedAngles())
        if (collision::angularDistance(land, a) < 2.5 * c.minSeparation) return 0;
    return 1;
}

int spamFire(const gameplay::Match& m, int s) { return m.field(s).canFireAt(m.tickCount() + 1) ? 1 : 0; }

// One tick with station `s` driven by `policy` while it is in the round.
void playTick(ArcadeMachine& m, int s, int (*policy)(const gameplay::Match&, int)) {
    ArcadeInput in;
    const gameplay::Match* match = m.match();
    if (m.state() == ArcadeState::Playing && match && match->phase() == gameplay::MatchPhase::Playing && match->stationInRound(s))
        in.stations[static_cast<std::size_t>(s)].firePresses = policy(*match, s);
    m.tick(in);
}

void press(ArcadeMachine& m, int s, arcade::StationButtons b) {
    ArcadeInput in;
    in.stations[static_cast<std::size_t>(s)] = b;
    m.tick(in);
}

std::vector<arcade::ArcadeEventType> eventTypes(ArcadeMachine& m) {
    std::vector<arcade::ArcadeEventType> out;
    for (const auto& e : m.takeEvents()) out.push_back(e.type);
    return out;
}

} // namespace

TEST_CASE(arcade_knocked_out_player_shoots_initials_while_the_match_goes_on) {
    gameplay::LevelScript levels;
    config::Settings s = testSettings();
    s.gameplay.tries = 2;
    s.gameplay.roundEndScreen = config::RoundEndScreen::Short;
    s.bots.singleplayerBots = 3;
    s.bots.difficulty = config::Difficulty::Expert;
    s.arcade.freePlay = true;
    ArcadeMachine m(s, levels, factory(), {}, 1);
    m.setToday("2026-11-26");
    m.tick(startAt(0));
    voteBattleAndPlay(m, {0});
    REQUIRE(m.state() == ArcadeState::Playing);
    // Round 1: aim properly and score.
    for (int i = 0; i < 60 * kSecond && m.match()->round() == 1; ++i) playTick(m, 0, aimed);
    REQUIRE(m.match()->participant(0)->score > 0);
    // Then crash every round until knocked out.
    for (int i = 0; i < 200 * kSecond && !m.match()->participant(0)->eliminated; ++i) playTick(m, 0, spamFire);
    REQUIRE(m.match()->participant(0)->eliminated);
    // The entry starts once that round's end screen is over.
    for (int i = 0; i < 10 * kSecond && !m.entry(0); ++i) m.tick({});
    REQUIRE(m.entry(0).has_value());
    CHECK(m.entering(0));
    m.takeEvents();
    // W / D (left / right) and fire: "AB", then back to "end".
    press(m, 0, {1, false, 0, 0, false, false}); // fire: A
    press(m, 0, {0, false, 0, 1, false, false}); // right
    press(m, 0, {1, false, 0, 0, false, false}); // fire: B
    press(m, 0, {0, false, 2, 0, false, false}); // left twice: wraps to "end"
    CHECK(m.entry(0)->cursor() == arcade::InitialsEntry::kEnd);
    press(m, 0, {1, false, 0, 0, false, false});
    CHECK(!m.entering(0));
    const auto types = eventTypes(m);
    CHECK(std::find(types.begin(), types.end(), arcade::ArcadeEventType::SaveHighScores) != types.end());
    const auto& rows = m.highScores().tables("battle").allTime.rows();
    REQUIRE(!rows.empty());
    CHECK_EQ(rows.front().name, std::string("AB"));
    CHECK_EQ(rows.front().score, m.match() ? m.match()->participant(0)->score : rows.front().score);
    CHECK_EQ(rows.front().date, std::string("2026-11-26"));
}

TEST_CASE(arcade_last_player_out_gets_the_initials_screen_before_the_results) {
    gameplay::LevelScript levels;
    config::Settings s = testSettings();
    s.gameplay.tries = 1;
    s.gameplay.roundEndScreen = config::RoundEndScreen::Off;
    s.bots.singleplayerBots = 0; // alone: the match ends when the player is out
    s.arcade.freePlay = true;
    ArcadeMachine m(s, levels, factory(), {}, 1);
    m.tick(startAt(3));
    voteBattleAndPlay(m, {3});
    REQUIRE(m.state() == ArcadeState::Playing);
    for (int i = 0; i < 60 * kSecond && m.match()->round() == 1; ++i) playTick(m, 3, aimed);
    for (int i = 0; i < 120 * kSecond && m.state() == ArcadeState::Playing; ++i) playTick(m, 3, spamFire);
    REQUIRE(m.state() == ArcadeState::Initials);
    REQUIRE(m.entering(3));
    // Nobody types: the clock runs out and the station tag is saved, then the results follow.
    for (int i = 0; i < (s.arcade.initialsSeconds + 3) * kSecond && m.state() == ArcadeState::Initials; ++i) m.tick({});
    CHECK(m.state() == ArcadeState::GameOver);
    const auto& rows = m.highScores().tables("battle").allTime.rows();
    REQUIRE(rows.size() == 1);
    CHECK_EQ(rows[0].name, std::string("P4"));
    REQUIRE(m.lastResult().has_value());
    CHECK_EQ(m.lastResult()->scores[3], rows[0].score);
}

TEST_CASE(arcade_attract_shows_high_scores_once_there_are_any) {
    gameplay::LevelScript levels;
    ArcadeMachine m(testSettings(), levels, factory(), {}, 1);
    arcade::HighScores h;
    h.finish(h.reserve("battle", 1234, 3, "P1"), "WREN");
    m.setHighScores(h);
    const int toDemoEnd = static_cast<int>((ArcadeMachine::kTitleTime + ArcadeMachine::kHowToTime + ArcadeMachine::kDemoMaxTime) * kSecond) + 3;
    bool sawPage = false;
    for (int i = 0; i < toDemoEnd + 2 * kSecond; ++i) {
        m.tick({});
        if (m.attractPage() == arcade::AttractPage::HighScores) sawPage = true;
    }
    CHECK(sawPage);
}
