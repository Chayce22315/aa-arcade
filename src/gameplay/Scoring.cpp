#include "gameplay/Scoring.hpp"

#include <algorithm>
#include <cmath>

namespace spindle::gameplay::scoring {
namespace {

std::int64_t r(int round) { return std::max(1, round); }

} // namespace

std::int64_t placementPoints(const Placement& p, int round) {
    const std::int64_t base = (kNeedle + (p.closeCall ? kCloseCall : 0)) * r(round);
    double mult = 1.0;
    if (p.onFire)
        mult = 2.0;
    else if (p.fireMeter)
        mult = 1.0 + std::clamp(std::isfinite(p.comboFill) ? p.comboFill : 0.0, 0.0, 1.0);
    if (p.gold) mult *= 2.0;
    return static_cast<std::int64_t>(std::llround(static_cast<double>(base) * mult));
}

std::int64_t burnPoints(int needles, int round) { return std::max(0, needles) * kBurn * r(round); }

std::int64_t powerupPoints(int round) { return kPowerup * r(round); }

std::int64_t clearBonus(const ClearInfo& info, int round) {
    if (!info.cleared) return 0;
    const double left = std::isfinite(info.secondsLeft) ? std::max(0.0, info.secondsLeft) : 0.0;
    std::int64_t b = kClear * r(round) + static_cast<std::int64_t>(std::floor(left)) * kPerSecondLeft * r(round);
    if (info.firstClear) b += kFirstClear * r(round);
    return b;
}

std::int64_t failPenalty(int round) { return kFailPenalty * r(round); }

std::int64_t addCapped(std::int64_t total, std::int64_t delta) {
    const std::int64_t t = std::clamp<std::int64_t>(total, 0, kMaxScore);
    if (delta > 0) return std::min(kMaxScore, t + std::min(delta, kMaxScore));
    if (delta < 0) return std::max<std::int64_t>(0, t + std::max(delta, -kMaxScore));
    return t;
}

int digitCount(std::int64_t total) {
    std::int64_t v = std::clamp<std::int64_t>(total, 0, kMaxScore);
    int n = 1;
    while (v >= 10) {
        v /= 10;
        ++n;
    }
    return n;
}

TallyTiming tallyTiming(TallyStyle style, int maxDigits, double offSeconds) {
    TallyTiming t;
    t.digits = std::clamp(maxDigits, 1, 7);
    switch (style) {
    case TallyStyle::Full:
        t.lights = 0.5;
        t.perDigit = 0.22;
        t.breakdown = 1.5;
        t.crown = 1.0;
        break;
    case TallyStyle::Short:
        t.lights = 0.2;
        t.perDigit = 0.1;
        t.breakdown = 0.6;
        t.crown = 0.5;
        break;
    case TallyStyle::Off:
        // No typing: the numbers just appear, the crown flashes at the end.
        t.lights = 0.0;
        t.perDigit = 0.0;
        t.breakdown = std::max(0.0, offSeconds - 0.5);
        t.crown = std::min(0.5, std::max(0.0, offSeconds));
        break;
    }
    return t;
}

} // namespace spindle::gameplay::scoring
