#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Pure round-resolution rules (docs/RULES.md). No timing, no rendering, no input: given who played a
// round and how it went, decide tries, eliminations, the winner, or a sudden-death round.
namespace spindle::gameplay {

struct PlayerRecord {
    int id = 0;
    int tries = 0; // before this round
};

struct RoundOutcome {
    int id = 0;
    bool cleared = false;
    std::uint64_t failTick = 0; // valid when !cleared
    int placed = 0;             // needles this player placed in the round
    bool timedOut = false;      // failed because the round time limit ran out
};

enum class DecisionKind {
    Continue,   // two or more players still have tries: play the next round
    Winner,     // exactly one winner decided
    NoWinner,   // single-participant match ended (assumption A1)
    SuddenDeath // the listed players play a sudden-death round, one try each
};

// T1-T11: battle's tie-break table. The other modes decide by Score (best total), Race (first to the
// target), Crown (most crown points), Clock (blitz time up, best total) and Team (last team in).
enum class RuleCase { None, T1, T2, T3, T4, T5, T6, T7, T8, T9, T10, T11, Score, Race, Crown, Clock, Team };
std::string_view ruleCaseName(RuleCase c);

struct RoundDecision {
    DecisionKind kind = DecisionKind::Continue;
    int winner = -1;
    std::vector<int> lostTry;     // ids that lost a try this round
    std::vector<int> eliminated;  // ids whose tries reached zero this round
    std::vector<int> suddenDeath; // ids for the next sudden-death round
    RuleCase rule = RuleCase::None;     // the case from docs/RULES.md that decided this round
    RuleCase subRule = RuleCase::None;  // for T7/T8: the T2/T3/T4 step that decided it
};

// Normal round: players are everyone who started the round with tries > 0 (all of them play), and
// outcomes has exactly one entry per player. participants is the match's starting player count.
RoundDecision resolveRound(std::span<const PlayerRecord> players, std::span<const RoundOutcome> outcomes, int participants);

// Sudden-death round between tied players (each with one try). Never changes tries.
RoundDecision resolveSuddenDeath(std::span<const RoundOutcome> outcomes);

// The simultaneous-elimination tie-break (T2 -> T3 -> T4) over failed outcomes.
RoundDecision tieBreak(std::span<const RoundOutcome> failed);

// T11, the abandoned-match guard: after kMaxTiedSuddenDeaths consecutive sudden-death rounds that end
// in a complete tie (typically everyone idle until the timer runs out), the winner is the player with
// the most needles placed over the whole match, then the most rounds cleared, then the lowest station.
inline constexpr int kMaxTiedSuddenDeaths = 3;
struct MatchTotals {
    int id = 0;
    int totalPlaced = 0;
    int roundsCleared = 0;
};
RoundDecision matchTotalsTieBreak(std::span<const MatchTotals> players);

} // namespace spindle::gameplay
