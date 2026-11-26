// Score values and the round end screen timeline (docs/RULES.md "Score").

#include <limits>

#include "Test.hpp"
#include "gameplay/Scoring.hpp"

using namespace spindle::gameplay::scoring;

TEST_CASE(score_needles_close_calls_combo_and_fire) {
    Placement plain;
    CHECK_EQ(placementPoints(plain, 1), std::int64_t{10});
    CHECK_EQ(placementPoints(plain, 4), std::int64_t{40});
    Placement close;
    close.closeCall = true;
    CHECK_EQ(placementPoints(close, 2), std::int64_t{30});
    Placement combo;
    combo.fireMeter = true;
    combo.comboFill = 0.5;
    CHECK_EQ(placementPoints(combo, 2), std::int64_t{30}); // 20 x 1.5
    combo.comboFill = 7.0;                                 // clamped to a full meter
    CHECK_EQ(placementPoints(combo, 1), std::int64_t{20});
    Placement noMeter;
    noMeter.comboFill = 1.0; // ignored without the fire meter
    CHECK_EQ(placementPoints(noMeter, 1), std::int64_t{10});
    Placement fire;
    fire.onFire = true;
    fire.fireMeter = true;
    fire.comboFill = 1.0;
    fire.closeCall = true;
    CHECK_EQ(placementPoints(fire, 3), std::int64_t{90}); // (10+5) x 3 x 2, fire replaces the combo multiplier
    CHECK_EQ(placementPoints(plain, 0), std::int64_t{10}); // round numbers below 1 count as 1
    CHECK_EQ(burnPoints(3, 2), std::int64_t{30});
    CHECK_EQ(burnPoints(-1, 2), std::int64_t{0});
    CHECK_EQ(powerupPoints(2), std::int64_t{50});
}

TEST_CASE(score_clear_bonus_time_and_first_clear) {
    ClearInfo none;
    CHECK_EQ(clearBonus(none, 5), std::int64_t{0});
    ClearInfo c;
    c.cleared = true;
    c.secondsLeft = 12.7; // whole seconds only
    CHECK_EQ(clearBonus(c, 1), std::int64_t{100 + 24});
    c.firstClear = true;
    CHECK_EQ(clearBonus(c, 2), std::int64_t{(100 + 24 + 50) * 2});
    c.secondsLeft = -3.0;
    c.firstClear = false;
    CHECK_EQ(clearBonus(c, 1), std::int64_t{100});
    CHECK_EQ(failPenalty(3), std::int64_t{150});
}

TEST_CASE(score_total_is_capped_and_digits_counted) {
    CHECK_EQ(addCapped(10, 5), std::int64_t{15});
    CHECK_EQ(addCapped(10, -50), std::int64_t{0});
    CHECK_EQ(addCapped(kMaxScore - 1, 100), kMaxScore);
    CHECK_EQ(addCapped(kMaxScore, std::numeric_limits<std::int64_t>::max()), kMaxScore);
    CHECK_EQ(addCapped(0, std::numeric_limits<std::int64_t>::min()), std::int64_t{0});
    CHECK_EQ(digitCount(0), 1);
    CHECK_EQ(digitCount(9), 1);
    CHECK_EQ(digitCount(10), 2);
    CHECK_EQ(digitCount(123456), 6);
    CHECK_EQ(digitCount(kMaxScore), 7);
    CHECK_EQ(digitCount(-5), 1);
}

TEST_CASE(score_tally_timeline_per_style) {
    const TallyTiming full = tallyTiming(TallyStyle::Full, 7, 1.75);
    CHECK_NEAR(full.total(), 0.5 + 7 * 0.22 + 1.5 + 1.0, 1e-9);
    CHECK_NEAR(full.breakdownStart(), 0.5 + 7 * 0.22, 1e-9);
    CHECK(full.digitsStart() < full.breakdownStart());
    CHECK(full.breakdownStart() < full.crownStart());
    const TallyTiming shortOne = tallyTiming(TallyStyle::Short, 3, 1.75);
    CHECK(shortOne.total() < full.total());
    const TallyTiming off = tallyTiming(TallyStyle::Off, 5, 1.75);
    CHECK_NEAR(off.total(), 1.75, 1e-9);
    CHECK_EQ(tallyTiming(TallyStyle::Full, 40, 1.0).digits, 7); // clamped to what a score can have
    CHECK_EQ(tallyTiming(TallyStyle::Full, 0, 1.0).digits, 1);
}
