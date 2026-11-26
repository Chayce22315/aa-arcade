// High score tables: ranking, today's tables, reservations while initials are shot, the file.

#include <algorithm>
#include <string_view>

#include "Test.hpp"
#include "arcade/HighScores.hpp"
#include "config/IniDocument.hpp"

using namespace spindle::arcade;

namespace {

ScoreEntry row(std::string name, std::int64_t score) {
    ScoreEntry e;
    e.name = std::move(name);
    e.score = score;
    e.round = 3;
    return e;
}

} // namespace

TEST_CASE(highscores_rank_order_ties_and_size) {
    HighScoreTable t;
    CHECK(!t.qualifies(0));
    CHECK(t.qualifies(1));
    for (int i = 1; i <= 10; ++i) CHECK(t.insert(row("R" + std::to_string(i), i * 100)).has_value());
    CHECK_EQ(t.rows().size(), std::size_t{10});
    CHECK_EQ(t.rows().front().score, std::int64_t{1000});
    CHECK_EQ(t.rows().back().score, std::int64_t{100});
    CHECK(!t.qualifies(100)); // equal to the last row is not enough
    CHECK(t.qualifies(101));
    const auto at = t.insert(row("TIE", 500)); // ties rank below the row already there
    REQUIRE(at.has_value());
    CHECK_EQ(*at, std::size_t{6});
    CHECK_EQ(t.rows()[5].name, std::string("R5"));
    CHECK_EQ(t.rows().size(), std::size_t{10});
    CHECK_EQ(t.rows().back().score, std::int64_t{200}); // 100 fell off
}

TEST_CASE(highscores_names_and_dates_are_sanitized) {
    CHECK_EQ(sanitizeName("ab c!9xyzQQ"), std::string("ABC9XY"));
    CHECK_EQ(sanitizeName("..."), std::string());
    CHECK(isIsoDate("2026-11-26"));
    CHECK(!isIsoDate("2026-13-01"));
    CHECK(!isIsoDate("26-11-2026"));
}

TEST_CASE(highscores_reserve_then_finish_and_never_push_a_reserved_row_out) {
    HighScores h;
    h.setToday("2026-11-26");
    for (int i = 1; i <= 10; ++i) {
        const auto tok = h.reserve("battle", 1000 + i * 10, 2, "OLD");
        h.finish(tok, "OLD" + std::to_string(i % 10));
    }
    CHECK_EQ(h.tables("battle").allTime.rows().size(), std::size_t{10});
    // A qualifies just above the bottom row and starts typing...
    const auto a = h.reserve("battle", 1015, 4, "P1");
    REQUIRE(a != 0);
    CHECK_EQ(h.tables("battle").allTime.rows().back().name, std::string("P1"));
    // ...then B qualifies higher at the same time: an old row leaves, A keeps its reserved row.
    const auto b = h.reserve("battle", 1095, 5, "P2");
    REQUIRE(b != 0);
    CHECK(h.rankOf("battle", a, false).has_value());
    CHECK(h.rankOf("battle", b, false).has_value());
    CHECK_EQ(h.tables("battle").allTime.rows().size(), std::size_t{10});
    CHECK(h.finish(a, "ACE"));
    CHECK(h.finish(b, ""));                // an empty name keeps the fallback
    CHECK(!h.finish(a, "AGAIN"));          // already finished
    CHECK(!h.finish(0, "X"));
    const auto& rows = h.tables("battle").allTime.rows();
    CHECK(std::any_of(rows.begin(), rows.end(), [](const ScoreEntry& e) { return e.name == "ACE" && e.score == 1015; }));
    CHECK(std::any_of(rows.begin(), rows.end(), [](const ScoreEntry& e) { return e.name == "P2" && e.score == 1095; }));
    CHECK_EQ(h.reserve("battle", 0, 1, "P3"), std::uint32_t{0});
}

TEST_CASE(highscores_today_rolls_over_and_file_round_trips) {
    HighScores h;
    h.setToday("2026-11-26");
    h.finish(h.reserve("battle", 500, 3, "P1"), "WREN");
    CHECK_EQ(h.tables("battle").today.rows().size(), std::size_t{1});
    CHECK(h.anyEntries());
    const std::string text = h.serialize();
    constexpr std::string_view kTables[] = {"battle"};
    HighScores back = HighScores::parse(spindle::config::IniDocument::parse(text), kTables);
    CHECK_EQ(back.today(), std::string("2026-11-26"));
    REQUIRE(back.tables("battle").allTime.rows().size() == 1);
    CHECK_EQ(back.tables("battle").allTime.rows()[0].name, std::string("WREN"));
    CHECK_EQ(back.tables("battle").allTime.rows()[0].score, std::int64_t{500});
    CHECK_EQ(back.tables("battle").allTime.rows()[0].date, std::string("2026-11-26"));
    // A new day empties today's table only.
    back.setToday("2026-11-27");
    CHECK(back.tables("battle").today.empty());
    CHECK_EQ(back.tables("battle").allTime.rows().size(), std::size_t{1});
    back.setToday("garbage"); // ignored
    CHECK_EQ(back.today(), std::string("2026-11-27"));
    // Broken rows are skipped, unknown tables ignored.
    const auto junk = HighScores::parse(spindle::config::IniDocument::parse(
                                            "[battle]\n1 = !!, 50, 2, 2026-11-26\n2 = OK, -5, 2, \n3 = GOOD, 70, 0, \n4 = FINE, 80, 2, \n"
                                            "[nonsense]\n1 = A, 1, 1, \n"),
                                        kTables);
    REQUIRE(junk.tables("battle").allTime.rows().size() == 1);
    CHECK_EQ(junk.tables("battle").allTime.rows()[0].name, std::string("FINE"));
    CHECK(junk.tables("nonsense").allTime.empty());
}
