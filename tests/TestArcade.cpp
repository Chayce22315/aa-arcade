// Cabinet flow: attract -> coin -> join -> match -> game over -> attract, with bots and humans.

#include <memory>

#include "Test.hpp"
#include "arcade/ArcadeMachine.hpp"
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
    s.arcade.joinWindow = 5;
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
    CHECK(m.state() == ArcadeState::Join);
    CHECK_EQ(m.credits().credits(), 0);
    CHECK(m.joined()[2]);
    ticks(m, s.arcade.joinWindow * kSecond + 2);
    REQUIRE(m.state() == ArcadeState::Playing);
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
    m.tick(startAt(0)); // joined player starts early
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
    CHECK(m.state() == ArcadeState::Join);
    ticks(m, s.arcade.joinWindow * kSecond + 2);
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
    m.tick(startAt(0));
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
