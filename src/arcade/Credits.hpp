#pragma once

namespace spindle::arcade {

struct CreditConfig {
    bool freePlay = false;
    int creditsPerCoin = 1;
    int creditsPerGame = 1;
    int maxCredits = 99;
};

// Virtual credit counter. Coins above max_credits are still counted by bookkeeping but add nothing.
class Credits {
public:
    explicit Credits(const CreditConfig& config = {});

    void configure(const CreditConfig& config);
    const CreditConfig& config() const { return config_; }

    int insertCoin();    // returns credits actually added
    int serviceCredit(); // one credit without a coin; returns credits added
    bool canJoin() const;
    bool payForJoin();   // false when the player cannot afford it
    int credits() const { return credits_; }
    bool freePlay() const { return config_.freePlay; }
    void clear() { credits_ = 0; }

private:
    int add(int amount);

    CreditConfig config_;
    int credits_ = 0;
};

} // namespace spindle::arcade
