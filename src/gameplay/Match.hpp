#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "gameplay/LevelDef.hpp"
#include "gameplay/MatchRules.hpp"
#include "gameplay/Playfield.hpp"
#include "gameplay/Rotor.hpp"

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
    bool inRound = false; // plays the current (or upcoming) round
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
};

enum class MatchPhase { Ready, Playing, Resolving, Over };

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
    ShieldBroke     // station
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

    std::vector<MatchEvent> takeEvents();

private:
    void prepareRound();
    void startRound();
    void resolveRound();
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
    std::vector<MatchEvent> events_;
};

} // namespace spindle::gameplay
