// Lifetime stats, achievements and unlockable looks (arcade/Progress), and the arcade awarding them.

#include "Test.hpp"
#include "arcade/Progress.hpp"

using namespace spindle;
using arcade::Achievement;
using arcade::Look;
using arcade::Progress;
using arcade::StationProgress;

TEST_CASE(progress_achievements_are_earned_once_at_their_thresholds) {
    Progress p;
    StationProgress s;
    CHECK(p.update(s).empty());
    s.needles = 1;
    auto got = p.update(s);
    REQUIRE(got.size() == std::size_t{1});
    CHECK(got[0] == Achievement::FirstPin);
    CHECK(p.update(s).empty()); // once per cabinet
    s.round = 10;
    s.closeCalls = 4;
    s.ignitions = 2;
    got = p.update(s);
    REQUIRE(got.size() == std::size_t{1});
    CHECK(got[0] == Achievement::Round10);
    s.closeCalls = 5;
    s.ignitions = 3;
    s.crowns = 5;
    s.rings = 5;
    s.score = 100'000;
    got = p.update(s);
    CHECK(p.earned(Achievement::CloseShaves) && p.earned(Achievement::HotHands) && p.earned(Achievement::Crowned));
    CHECK(p.earned(Achievement::Royalty) && p.earned(Achievement::RingCollector));
    CHECK(p.earned(Achievement::TenThousand) && p.earned(Achievement::SixFigures));
    CHECK(!p.earned(Achievement::Round25) && !p.earned(Achievement::Flawless));
    s.finished = true;
    s.won = true;
    s.flawless = true;
    got = p.update(s);
    REQUIRE(got.size() == std::size_t{1});
    CHECK(got[0] == Achievement::Flawless);
}

TEST_CASE(progress_looks_unlock_with_their_achievement_and_stay_off_while_locked) {
    Progress p;
    CHECK(!p.unlocked(Look::BlueFlames));
    p.setLook(Look::BlueFlames, true);
    CHECK(!p.lookOn(Look::BlueFlames)); // locked: stays off
    StationProgress s;
    s.ignitions = 3;
    p.update(s);
    CHECK(p.unlocked(Look::BlueFlames));
    CHECK(p.lookOn(Look::BlueFlames)); // a new look starts on
    p.setLook(Look::BlueFlames, false);
    CHECK(!p.lookOn(Look::BlueFlames));
    CHECK(!p.unlocked(Look::RoyalCrown) && !p.unlocked(Look::Trails));
}

TEST_CASE(progress_lifetime_stats_complete_tourist_and_thousand_needles) {
    Progress p;
    StationProgress s;
    s.needles = 120;
    s.score = 4200;
    s.round = 7;
    for (gameplay::ModeId m : gameplay::kAllModes) {
        CHECK(!p.earned(Achievement::Tourist));
        s.mode = m;
        p.countMatch();
        p.addMatch(s, 6);
    }
    CHECK(p.earned(Achievement::Tourist));
    CHECK_EQ(p.stats().matches, std::uint64_t{9});
    CHECK_EQ(p.stats().needles, std::uint64_t{1080});
    CHECK(p.earned(Achievement::ThousandNeedles));
    CHECK_EQ(p.stats().roundsCleared, std::uint64_t{54});
    CHECK_EQ(p.stats().bestScore, std::int64_t{4200});
    CHECK_EQ(p.stats().bestRound, 7);
    // Lifetime needles plus this match's count during play.
    Progress q;
    StationProgress live;
    live.needles = 999;
    q.update(live);
    CHECK(!q.earned(Achievement::ThousandNeedles));
    live.needles = 1000;
    q.update(live);
    CHECK(q.earned(Achievement::ThousandNeedles));
}

TEST_CASE(progress_round_trips_through_its_ini_and_resets) {
    Progress p;
    StationProgress s;
    s.needles = 3;
    s.ignitions = 3;
    s.round = 25;
    s.mode = gameplay::ModeId::Chaos;
    p.update(s);
    p.countMatch();
    p.addMatch(s, 24);
    p.addHighScore();
    p.setLook(Look::Trails, false);
    const Progress back = Progress::parse(config::IniDocument::parse(p.serialize()));
    CHECK(back == p);
    CHECK(back.earned(Achievement::OnTheBoard));
    CHECK(back.lookOn(Look::BlueFlames) && !back.lookOn(Look::Trails) && back.unlocked(Look::Trails));
    CHECK(back.stats().modesFinished == (1u << static_cast<unsigned>(gameplay::ModeId::Chaos)));
    // A hand-edited file cannot switch on a look it has not unlocked.
    const Progress forged = Progress::parse(config::IniDocument::parse("[Looks]\nroyal_crown = 1\n"));
    CHECK(!forged.lookOn(Look::RoyalCrown));
    Progress r = back;
    r.reset();
    CHECK(r == Progress{});
    CHECK_EQ(r.earnedCount(), 0);
}
