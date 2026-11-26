#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "gameplay/LevelDef.hpp"
#include "gameplay/MatchRules.hpp"
#include "gameplay/Playfield.hpp"
#include "gameplay/Rotor.hpp"
#include "gameplay/Scoring.hpp"

namespace spindle::gameplay {

inline constexpr int kMaxStations = 4;

enum class ParticipantKind { Human, Bot };

struct ParticipantSpec {
    int station = 0;
    ParticipantKind kind = ParticipantKind::Human;
};

struct Participant {
    int station = 0;
    ParticipantKind kind = ParticipantKind::Human;
    int tries = 0;
    bool eliminated = false;
    int eliminatedRound = 0;
    std::uint64_t eliminatedTick = 0;
    int roundsCleared = 0;
    int totalPlaced = 0;
    int powerupsClaimed = 0;
    int ignitions = 0;    // times this player caught fire
    int burnt = 0;        // needles their fire needles and purges burnt away
    int carriedCombo = 0; // fire-meter combo banked by clearing the previous round
    bool inRound = false; // plays the current (or upcoming) round

    // Score (docs/RULES.md "Score").
    std::int64_t score = 0;       // total over the match
    std::int64_t roundPoints = 0; // earned while playing the current round (shown at its end)
    std::int64_t roundBonus = 0;  // added at the last round end
    std::int64_t scoreBefore = 0; // total before the last round end
    int closeCalls = 0;
    int bestCombo = 0;
    int crownsWon = 0; // rounds won outright (most round points)
};

struct MatchConfig {
    int tries = 3;
    double roundTimeLimit = 45.0;
    double readyDelay = 1.5;
    double resolveDelay = 1.75;
    int startRound = 1;
    std::uint32_t seed = 1;
    double rotationScale = 1.0;
    double needleScale = 1.0;
    std::vector<PowerupKind> powerups; // ring kinds rounds may contain; empty = powerups off

    // Fire meter (docs/RULES.md): a combo of quick placements; full = on fire for fireSeconds.
    bool fireMeter = false;
    int fireCombo = 8;
    double fireSeconds = 1.5;
    double fireChainWindow = 1.0;

    // The round end screen (tally): how long it plays. Off = a plain resolveDelay pause.
    scoring::TallyStyle tally = scoring::TallyStyle::Full;
};

// Tally: the round end screen (scores typed in, the crown moves) before the next round or the end.
enum class MatchPhase { Ready, Playing, Tally, Over };

enum class MatchEventType {
    RoundReady,     // value = round number
    RoundStarted,   // value = round number
    NeedleLaunched, // station
    NeedlePlaced,   // station
    StationCleared, // station
    StationFailed,  // station, value = 1 if timeout
    TryLost,        // station, value = tries left
    Eliminated,     // station
    SuddenDeath,    // value = number of players in it
    Winner,         // station
    NoWinner,
    PowerupClaimed, // station, value = PowerupKind
    TryGained,      // station, value = tries now (mend)
    ShieldBroke,    // station
    Ignited,        // station: the station is on fire
    Burned,         // station, value = needles burnt away this tick
    RoundScored,    // station, value = round points (one per player in the round, at its end)
    CrownTaken,     // station, value = previous holder (-1 = nobody)
    CrownLost       // station: the holder was knocked out
};

struct MatchEvent {
    MatchEventType type;
    int station = -1;
    int value = 0;
};

// Runs a match: shared rotor, one playfield per station, rounds, tries, elimination and the
// tie-break rules. Fixed timestep; one call to tick() per simulation tick.
class Match {
public:
    Match(const MatchConfig& config, const std::vector<ParticipantSpec>& participants, LevelProvider& levels);

    // fireRequests[s] = launch presses for station s during this tick (humans and bots alike).
    void tick(const std::array<int, kMaxStations>& fireRequests);

    void setPaused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }

    MatchPhase phase() const { return phase_; }
    bool over() const { return phase_ == MatchPhase::Over; }
    int round() const { return round_; }
    bool suddenDeath() const { return suddenDeath_; }
    std::uint64_t tickCount() const { return tick_; }
    double phaseTime() const { return phaseTime_; }
    double roundTime() const;
    double roundTimeLeft() const;
    const MatchConfig& config() const { return config_; }

    const Rotor& rotor() const { return rotor_; }
    const Playfield& field(int station) const { return fields_[static_cast<std::size_t>(station)]; }
    const std::optional<Participant>& participant(int station) const { return participants_[static_cast<std::size_t>(station)]; }
    int participantCount() const { return participantCount_; }
    int aliveCount() const;
    bool stationInRound(int station) const;

    int winnerStation() const { return winner_; }
    // Stations ordered best first (winner, then by later elimination, then needles placed).
    std::vector<int> standings() const;
    const RoundDecision& lastDecision() const { return lastDecision_; }

    // Crown: the station that won a round outright most recently and still holds it (-1 = nobody).
    int crownHolder() const { return crown_; }
    int crownBefore() const { return crownBefore_; } // holder before the last round end (for the crown's flight)
    // The round end screen of the last round: its timeline and who is in it.
    const scoring::TallyTiming& tallyTiming() const { return tally_; }
    bool tallied(int station) const { return station >= 0 && station < kMaxStations && tallied_[static_cast<std::size_t>(station)]; }

    std::vector<MatchEvent> takeEvents();

private:
    void prepareRound();
    void startRound();
    void resolveRound();
    void scoreRound(const std::vector<RoundOutcome>& outcomes);
    void updateCrown();
    void emit(MatchEventType type, int station = -1, int value = 0) { events_.push_back({type, station, value}); }

    MatchConfig config_;
    LevelProvider& levels_;
    FieldConfig fieldConfig_;
    std::array<std::optional<Participant>, kMaxStations> participants_{};
    std::array<Playfield, kMaxStations> fields_{};
    int participantCount_ = 0;
    Rotor rotor_;
    LevelDef level_;
    MatchPhase phase_ = MatchPhase::Ready;
    int round_ = 1;
    bool suddenDeath_ = false;
    std::vector<int> suddenDeathSet_;
    int tiedSuddenDeaths_ = 0;
    bool overAfterResolve_ = false;
    int winner_ = -1;
    bool paused_ = false;
    std::uint64_t tick_ = 0;
    std::uint64_t roundStartTick_ = 0;
    double phaseTime_ = 0.0;
    RoundDecision lastDecision_;
    int crown_ = -1;
    int crownBefore_ = -1;
    scoring::TallyTiming tally_{};
    std::array<bool, kMaxStations> tallied_{};
    std::vector<MatchEvent> events_;
};

} // namespace spindle::gameplay
