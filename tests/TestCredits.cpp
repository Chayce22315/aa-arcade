#include "Test.hpp"
#include "arcade/Bookkeeping.hpp"
#include "arcade/Credits.hpp"

using namespace spindle::arcade;

TEST_CASE(credits_coins_add_and_cap) {
    Credits c({false, 2, 1, 5});
    CHECK(!c.canJoin());
    CHECK_EQ(c.insertCoin(), 2);
    CHECK_EQ(c.insertCoin(), 2);
    CHECK_EQ(c.insertCoin(), 1); // capped at 5
    CHECK_EQ(c.insertCoin(), 0);
    CHECK_EQ(c.credits(), 5);
    CHECK(c.payForJoin());
    CHECK_EQ(c.credits(), 4);
}

TEST_CASE(credits_cost_per_game_and_free_play) {
    Credits c({false, 1, 2, 99});
    c.insertCoin();
    CHECK(!c.canJoin());
    CHECK(!c.payForJoin());
    CHECK_EQ(c.credits(), 1);
    c.serviceCredit();
    CHECK(c.canJoin());
    CHECK(c.payForJoin());
    CHECK_EQ(c.credits(), 0);
    c.configure({true, 1, 2, 99});
    CHECK(c.canJoin());
    CHECK(c.payForJoin());
    CHECK_EQ(c.credits(), 0);
}

TEST_CASE(credits_config_is_sanitized_and_reconfigure_clamps) {
    Credits c({false, 0, 0, 500});
    CHECK_EQ(c.config().creditsPerCoin, 1);
    CHECK_EQ(c.config().creditsPerGame, 1);
    CHECK_EQ(c.config().maxCredits, 99);
    for (int i = 0; i < 20; ++i) c.insertCoin();
    c.configure({false, 1, 1, 10});
    CHECK_EQ(c.credits(), 10);
}

TEST_CASE(bookkeeping_round_trip_and_recent_winners) {
    Bookkeeping b;
    b.coins = 12;
    b.serviceCredits = 3;
    b.gamesSingleplayer = 4;
    b.gamesMultiplayer = 5;
    b.humanWins = 6;
    b.botWins = 3;
    b.playSeconds = 900;
    for (int i = 0; i < 7; ++i) b.addWinner({i % 4, i % 2 == 0, 10 + i});
    CHECK_EQ(b.recentWinners.size(), Bookkeeping::kMaxRecent);
    CHECK_EQ(b.recentWinners.front().round, 16);
    Bookkeeping r = Bookkeeping::parse(spindle::config::IniDocument::parse(b.serialize()));
    CHECK(r == b);
    Bookkeeping junk = Bookkeeping::parse(spindle::config::IniDocument::parse("[Counters]\ncoins = lots\n[RecentWinners]\nwinner1 = 9,human,3\n"));
    CHECK_EQ(junk.coins, std::uint64_t{0});
    CHECK(junk.recentWinners.empty());
}
