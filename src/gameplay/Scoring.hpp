#pragma once

#include <cstdint>

// Points (docs/RULES.md "Score"). Pure functions; every value here is uncalibrated, tune by feel.
// R is the round number. Round points are earned while playing; the bonus is added at the round end.
namespace spindle::gameplay::scoring {

inline constexpr std::int64_t kMaxScore = 9'999'999; // 7 digits: what the round end screen types in

inline constexpr std::int64_t kNeedle = 10;      // per needle placed, x R
inline constexpr std::int64_t kCloseCall = 5;    // extra for a close call, x R
inline constexpr std::int64_t kBurn = 5;         // per needle your fire needle or purge burnt away, x R
inline constexpr std::int64_t kPowerup = 25;     // per ring taken, x R
inline constexpr std::int64_t kClear = 100;      // bonus for clearing, x R
inline constexpr std::int64_t kPerSecondLeft = 2; // bonus per whole second left on the round clock, x R
inline constexpr std::int64_t kFirstClear = 50;  // bonus for the first clear when 2+ players are in the round, x R
inline constexpr std::int64_t kFailPenalty = 50; // blitz: a fail costs this, x R

// How one needle landed, recorded by the playfield when it sticks.
struct Placement {
    bool closeCall = false;    // landed closer than kCloseCallFactor x the minimum gap to a neighbour
    double comboFill = 0.0;    // fire-meter fill (0..1) before this needle
    bool onFire = false;       // the station was on fire when it landed
    bool fireMeter = false;    // the fire meter is on this round (no meter = no combo multiplier)
    bool gold = false;         // a gold ring is active: the needle's points are doubled
};

std::int64_t placementPoints(const Placement& p, int round);
std::int64_t burnPoints(int needles, int round);
std::int64_t powerupPoints(int round);

struct ClearInfo {
    bool cleared = false;
    double secondsLeft = 0.0; // on the round clock when the last needle stuck
    bool firstClear = false;  // the earliest clear (ties share it) with 2+ players in the round
};
std::int64_t clearBonus(const ClearInfo& info, int round);
std::int64_t failPenalty(int round);

// total + delta, kept inside 0..kMaxScore.
std::int64_t addCapped(std::int64_t total, std::int64_t delta);

// Decimal digits the round end screen fires for a total (at least one: a zero is one "0").
int digitCount(std::int64_t total);

// ---------------------------------------------------------------------------------------- round end

enum class TallyStyle { Full, Short, Off };

// The round end screen's timeline. Everything is decided by the core so the screen and the tests agree.
struct TallyTiming {
    double lights = 0.0;    // circles light up
    double perDigit = 0.0;  // one ammo ball per digit
    int digits = 1;         // the longest total being typed this round
    double breakdown = 0.0; // total / round / bonus lines
    double crown = 0.0;     // the crown moves (or flashes)
    static constexpr double kFlight = 0.8; // share of a digit's slot its ball spends flying
    double digitsStart() const { return lights; }
    double digitLaunch(int k) const { return digitsStart() + perDigit * k; }        // k-th digit, biggest first
    double digitLand(int k) const { return digitLaunch(k) + perDigit * kFlight; }
    double breakdownStart() const { return lights + perDigit * digits; }
    double crownStart() const { return breakdownStart() + breakdown; }
    double total() const { return crownStart() + crown; }
};

// offSeconds is the plain pause used when the screen is off (the old resolve delay).
TallyTiming tallyTiming(TallyStyle style, int maxDigits, double offSeconds);

} // namespace spindle::gameplay::scoring
