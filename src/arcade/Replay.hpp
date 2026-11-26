#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gameplay/Match.hpp"
#include "gameplay/Modes.hpp"

// A recorded match (docs/RULES.md "Replays"): the match's exact configuration, its line-up and every
// fire press of every station, CPUs included, per simulation tick. The match is deterministic, so
// feeding the presses back into a new Match with the same configuration and levels plays the same game
// without any bots. Kept for each mode's best score and shown on the attract loop.
namespace spindle::arcade {

struct ReplayPress {
    std::uint64_t tick = 0; // the match tick the presses were fed into (Match::tickCount() before it)
    int station = 0;
    int count = 0;
    bool operator==(const ReplayPress&) const = default;
};

struct Replay {
    std::string build;                 // who recorded it: version, commit, level script hash
    gameplay::ModeId mode = gameplay::ModeId::Battle;
    std::string name;                  // the record holder's initials
    int station = -1;                  // their station
    std::int64_t score = 0;
    gameplay::MatchConfig config;      // config.mode is rebuilt from `mode`
    std::vector<gameplay::ParticipantSpec> participants;
    std::array<std::int64_t, gameplay::kMaxStations> finalScores{};
    std::uint64_t endTick = 0;
    std::vector<ReplayPress> presses;

    std::string serialize() const;
    static std::optional<Replay> parse(std::string_view text, std::string* error = nullptr);
    bool operator==(const Replay&) const = default;
};

// Builds a replay while a match runs.
class ReplayRecorder {
public:
    void begin(const gameplay::MatchConfig& config, const std::vector<gameplay::ParticipantSpec>& participants, gameplay::ModeId mode,
               std::string build);
    // Call with the presses about to be fed into match.tick() (only when the match is not paused).
    void record(std::uint64_t tick, const std::array<int, gameplay::kMaxStations>& fire);
    void finish(const gameplay::Match& match);
    bool active() const { return active_; }
    bool finished() const { return finished_; }
    const Replay& replay() const { return replay_; }
    void clear() { *this = ReplayRecorder{}; }

    static constexpr std::size_t kMaxPresses = 200'000; // a runaway match stops recording (and is not kept)

private:
    Replay replay_;
    bool active_ = false;
    bool finished_ = false;
    bool overflow_ = false;
};

// Plays a replay back: the presses for the next tick of the match it drives.
class ReplayPlayer {
public:
    explicit ReplayPlayer(const Replay& replay) : replay_(&replay) {}
    std::array<int, gameplay::kMaxStations> presses(std::uint64_t tick);
    bool done(std::uint64_t tick) const { return tick >= replay_->endTick; }

private:
    const Replay* replay_;
    std::size_t next_ = 0;
};

// Runs a replay headless to its end; returns the final scores (the tests check determinism with it).
std::array<std::int64_t, gameplay::kMaxStations> runReplay(const Replay& replay, gameplay::LevelProvider& levels);

} // namespace spindle::arcade
