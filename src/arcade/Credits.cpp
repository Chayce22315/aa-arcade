#include "arcade/Credits.hpp"

#include <algorithm>

namespace spindle::arcade {

namespace {
CreditConfig sanitized(CreditConfig c) {
    c.creditsPerCoin = std::clamp(c.creditsPerCoin, 1, 9);
    c.creditsPerGame = std::clamp(c.creditsPerGame, 1, 9);
    c.maxCredits = std::clamp(c.maxCredits, 1, 99);
    return c;
}
} // namespace

Credits::Credits(const CreditConfig& config) : config_(sanitized(config)) {}

void Credits::configure(const CreditConfig& config) {
    config_ = sanitized(config);
    credits_ = std::min(credits_, config_.maxCredits);
}

int Credits::add(int amount) {
    const int before = credits_;
    credits_ = std::min(config_.maxCredits, credits_ + amount);
    return credits_ - before;
}

int Credits::insertCoin() { return add(config_.creditsPerCoin); }
int Credits::serviceCredit() { return add(1); }

bool Credits::canJoin() const { return config_.freePlay || credits_ >= config_.creditsPerGame; }

bool Credits::payForJoin() {
    if (config_.freePlay) return true;
    if (credits_ < config_.creditsPerGame) return false;
    credits_ -= config_.creditsPerGame;
    return true;
}

} // namespace spindle::arcade
