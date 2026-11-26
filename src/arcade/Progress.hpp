#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/IniDocument.hpp"
#include "gameplay/Modes.hpp"

// The cabinet's lifetime stats, achievements and the looks they unlock (docs/RULES.md "Achievements").
// Cabinet-wide: spindle has no player accounts, so whoever earns one earns it for the machine. Humans
// only, never in the attract demo. Persisted to progress.ini.
namespace spindle::arcade {

enum class Achievement {
    FirstPin,        // place a needle
    Round10,         // reach round 10 in a match
    Round25,         // reach round 25 in a match
    CloseShaves,     // 5 close calls in one match
    HotHands,        // catch fire 3 times in one match
    Crowned,         // take the crown
    Royalty,         // take the crown 5 times in one match
    RingCollector,   // take 5 rings in one match
    TenThousand,     // score 10,000 in a match
    SixFigures,      // score 100,000 in a match
    OnTheBoard,      // shoot your initials into a high score table
    Flawless,        // win a match against someone without losing a try
    Tourist,         // finish a match in every game mode
    ThousandNeedles, // 1,000 needles placed on this cabinet
    Count
};
inline constexpr std::size_t kAchievementCount = static_cast<std::size_t>(Achievement::Count);

enum class Look {
    BlueFlames, // fire burns blue
    RoyalCrown, // the crown gets a velvet band and jewels
    Trails,     // needles in flight leave a comet trail
    Count
};
inline constexpr std::size_t kLookCount = static_cast<std::size_t>(Look::Count);

struct AchievementInfo {
    std::string_view key;   // progress.ini name
    std::string_view name;  // shown in the pop-up and lists
    std::string_view blurb; // how to earn it
    std::optional<Look> unlocks;
};
AchievementInfo achievementInfo(Achievement a);
std::string_view lookKey(Look l);
std::string_view lookName(Look l);

// One human station's match so far (what the conditions read).
struct StationProgress {
    gameplay::ModeId mode = gameplay::ModeId::Battle;
    int round = 0; // the round being played (or the round they were knocked out in)
    int needles = 0;
    int closeCalls = 0;
    int ignitions = 0;
    int crowns = 0;
    int rings = 0;
    std::int64_t score = 0;
    bool finished = false; // the match is over
    bool won = false;      // ...and this station won it outright
    bool flawless = false; // ...without losing a try, against at least one other player
};

struct LifetimeStats {
    std::uint64_t matches = 0;  // matches with at least one human
    std::uint64_t needles = 0;  // placed by humans
    std::uint64_t roundsCleared = 0;
    std::uint64_t closeCalls = 0;
    std::uint64_t fires = 0;
    std::uint64_t crowns = 0;
    std::uint64_t rings = 0;
    std::uint64_t highScores = 0; // initials shot into a table
    std::int64_t bestScore = 0;
    int bestRound = 0;
    std::uint32_t modesFinished = 0; // bit per gameplay::ModeId
    bool operator==(const LifetimeStats&) const = default;
};

class Progress {
public:
    // Newly earned achievements for this station's match so far (each is earned once per cabinet).
    std::vector<Achievement> update(const StationProgress& s);
    // A human's finished match goes into the lifetime stats; returns achievements that completes.
    std::vector<Achievement> addMatch(const StationProgress& s, int roundsCleared);
    void countMatch() { ++stats_.matches; } // once per match, however many humans played
    std::vector<Achievement> addHighScore();

    bool earned(Achievement a) const { return earned_[static_cast<std::size_t>(a)]; }
    int earnedCount() const;
    bool unlocked(Look l) const;
    bool lookOn(Look l) const { return unlocked(l) && looks_[static_cast<std::size_t>(l)]; }
    // Turns a look on or off; a look that is still locked stays off.
    void setLook(Look l, bool on);
    const LifetimeStats& stats() const { return stats_; }
    void reset() { *this = Progress{}; }

    std::string serialize() const;
    static Progress parse(const config::IniDocument& doc);
    bool operator==(const Progress&) const = default;

private:
    bool award(Achievement a, std::vector<Achievement>& out);

    std::array<bool, kAchievementCount> earned_{};
    std::array<bool, kLookCount> looks_{}; // chosen (only shown when unlocked)
    LifetimeStats stats_;
};

} // namespace spindle::arcade
