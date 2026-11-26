#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "gameplay/LevelDef.hpp"
#include "gameplay/MatchRules.hpp"
#include "scripting/LuaSandbox.hpp"

// The nine game modes (docs/RULES.md "Game modes"). Every mode keeps the shared rotor: all stations
// play the same round in lockstep; a mode changes only how rounds are scored and how a match ends.
namespace spindle::gameplay {

enum class ModeId { Battle, Marathon, Blitz, Race, Crown, Sabotage, Teams, Chaos, Daily };
inline constexpr std::size_t kModeCount = 9;
inline constexpr std::array<ModeId, kModeCount> kAllModes{ModeId::Battle, ModeId::Marathon, ModeId::Blitz, ModeId::Race,  ModeId::Crown,
                                                          ModeId::Sabotage, ModeId::Teams, ModeId::Chaos, ModeId::Daily};

enum class MatchEnd {
    LastStanding, // tries; the last player (or team) in wins, T1-T11
    AllOut,       // tries; play on until everyone is out, best score wins
    Clock,        // no knock-outs; a match clock runs out, best score wins
    FirstTo,      // no knock-outs; first to `target` round wins
    Rounds        // no knock-outs; after `target` rounds, most crown points wins
};

struct ModeRules {
    ModeId id = ModeId::Battle;
    MatchEnd end = MatchEnd::LastStanding;
    bool knockouts = true;       // a failed round costs a try
    double clockSeconds = 0.0;   // Clock: seconds of play (ready and round end screens do not count)
    int target = 0;              // FirstTo: round wins; Rounds: rounds played
    bool teams = false;          // P1+P2 against P3+P4, a shared try pool per team
    bool junk = false;           // sabotage: clears and close calls send junk needles to rivals
    bool mutators = false;       // chaos: every round rolls one mutator
    bool dailySeed = false;      // the levels come from the local date
    bool failPenalty = false;    // blitz: a failed round costs points
    bool todayOnly = false;      // its high score table keeps today's scores only
    bool operator==(const ModeRules&) const = default;
};

ModeRules modeRules(ModeId id);
std::string_view modeKey(ModeId id);   // "battle" ... "daily" (settings.ini, highscores.ini)
std::string_view modeName(ModeId id);  // shown on the cards
std::string_view modeBlurb(ModeId id); // one line on the card / intro
std::optional<ModeId> modeFromKey(std::string_view key);

// Uncalibrated constants.
inline constexpr double kBlitzSeconds = 90.0;
inline constexpr int kRaceTarget = 5;
inline constexpr int kRaceNoClearGuard = 3; // rounds in a row nobody clears before race ends on score
inline constexpr int kCrownRounds = 8;
inline constexpr int kJunkCap = 4;          // junk needles one station can receive for one round
inline constexpr int kCloseCallsPerJunk = 3;
inline constexpr double kTinyFactor = 0.7, kBigFactor = 1.3, kFastFactor = 1.4;
inline constexpr int kExtraNeedles = 2;

// ------------------------------------------------------------------ chaos mutators

enum class Mutator { None, Blind, Tiny, Big, Fast, Extra };
std::string_view mutatorName(Mutator m);
// The round's mutator from the match seed (never in sudden death).
Mutator rollMutator(std::uint32_t seed, int round);

// ------------------------------------------------------------------ sabotage junk

// Adds up to `count` junk needles to the level where it stays valid under `limits` (spacing from every
// needle and ring, the packing rule). Returns how many were added; the angles come from `rng`.
int addJunk(LevelDef& level, int count, const LevelLimits& limits, scripting::Pcg32& rng);

// ------------------------------------------------------------------ daily

// The seed of a day's levels (FNV-1a of the date; never 0). Everyone gets the same levels that day.
std::uint32_t dailySeed(std::string_view isoDate);

// ------------------------------------------------------------------ resolving rounds in the other modes

struct ScoreTotals {
    int id = 0;
    std::int64_t score = 0;
    int placed = 0;
    int roundsCleared = 0;
    int points = 0; // the mode's own count (race wins, crown points); higher is better
};

// The best of the given players: points, then score, then needles placed, then rounds cleared, then
// the lowest station (docs/RULES.md "Score ties").
int bestByTotals(std::span<const ScoreTotals> players);

// AllOut modes: everyone who failed loses a try; the match ends when nobody has a try left, the best
// total wins (no winner for a single participant).
RoundDecision resolveEndless(std::span<const PlayerRecord> players, std::span<const RoundOutcome> outcomes,
                             std::span<const ScoreTotals> totals, int participants);

// Teams: `pools` are the two teams' shared tries before the round, `teamOf` maps an outcome id to its
// team (0 or 1). A failed round costs its team one try; a team at zero is out with every member.
struct TeamDecision {
    RoundDecision decision;
    std::array<int, 2> pools{};
    std::optional<int> winningTeam;
};
TeamDecision resolveTeamRound(std::array<int, 2> pools, std::span<const RoundOutcome> outcomes, std::span<const int> teamOf,
                              std::span<const ScoreTotals> totals);

} // namespace spindle::gameplay
