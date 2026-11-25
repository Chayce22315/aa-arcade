#include "Test.hpp"
#include "players/PlayerManager.hpp"

using namespace spindle::players;

TEST_CASE(lineup_mode_rule_for_every_human_count) {
    const BotFill fill{3, 2};
    for (int mask = 1; mask < 16; ++mask) {
        std::array<bool, 4> joined{};
        int humans = 0;
        for (int s = 0; s < 4; ++s) {
            joined[static_cast<std::size_t>(s)] = (mask >> s) & 1;
            humans += (mask >> s) & 1;
        }
        Lineup l = buildLineup(joined, fill);
        CHECK_EQ(l.humans, humans);
        CHECK(l.mode == (humans == 1 ? GameMode::Singleplayer : GameMode::Multiplayer));
        CHECK(l.humans + l.bots <= 4);
        const int wanted = humans == 1 ? 3 : 2;
        CHECK_EQ(l.bots, std::min(wanted, 4 - humans));
        for (int s = 0; s < 4; ++s)
            if (joined[static_cast<std::size_t>(s)]) CHECK(l.seats[static_cast<std::size_t>(s)] == SeatKind::Human);
    }
}

TEST_CASE(lineup_bots_take_lowest_free_stations_and_respect_zero) {
    Lineup a = buildLineup({false, false, true, false}, BotFill{2, 0});
    CHECK(a.seats[0] == SeatKind::Bot);
    CHECK(a.seats[1] == SeatKind::Bot);
    CHECK(a.seats[2] == SeatKind::Human);
    CHECK(a.seats[3] == SeatKind::Empty);
    Lineup b = buildLineup({true, false, false, false}, BotFill{0, 0});
    CHECK(b.mode == GameMode::Singleplayer);
    CHECK_EQ(b.bots, 0);
    CHECK_EQ(b.participants().size(), std::size_t{1});
    Lineup c = buildLineup({true, true, false, false}, BotFill{3, 0});
    CHECK(c.mode == GameMode::Multiplayer);
    CHECK_EQ(c.bots, 0);
    Lineup d = demoLineup();
    CHECK_EQ(d.bots, 4);
    CHECK(d.mode == GameMode::Demo);
}
