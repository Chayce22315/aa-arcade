#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "arcade/Bookkeeping.hpp"
#include "arcade/Credits.hpp"
#include "arcade/HighScores.hpp"
#include "arcade/InitialsEntry.hpp"
#include "arcade/VoteBox.hpp"
#include "audio/SoundId.hpp"
#include "bots/BotController.hpp"
#include "config/Settings.hpp"
#include "gameplay/Match.hpp"
#include "players/PlayerManager.hpp"
#include "scripting/LuaSandbox.hpp"

namespace spindle::arcade {

// Vote: players join and vote for a game mode. Initials: after the last round end screen, players who
// made a high score table shoot their initials.
enum class ArcadeState { Attract, InsertCoin, Vote, Playing, Initials, GameOver };
// Inside Vote: voting (joining still open), the tie-break wheel, then the chosen mode's card.
enum class VotePhase { Voting, Spin, Intro };
enum class AttractPage { Title, HowTo, Demo, HighScores };

struct StationButtons {
    int firePresses = 0;
    bool startPressed = false;
    int leftPresses = 0;
    int rightPresses = 0;
    bool leftHeld = false; // held at the end of the tick (for hold-to-repeat)
    bool rightHeld = false;
};

// One simulation tick worth of cabinet input (edges, not levels).
struct ArcadeInput {
    std::array<StationButtons, gameplay::kMaxStations> stations{};
    int coins = 0;
    int serviceCredits = 0;
};

enum class ArcadeEventType { Sound, SaveBookkeeping, SaveHighScores, MatchStarted, MatchEnded, JoinRefused };

struct ArcadeEvent {
    ArcadeEventType type = ArcadeEventType::Sound;
    audio::SoundId sound = audio::SoundId::Coin;
    int station = -1;
};

struct MatchResult {
    players::GameMode mode = players::GameMode::Singleplayer;
    players::Lineup lineup;
    int winner = -1;
    std::vector<int> standings;
    int finalRound = 0;
    std::array<int, gameplay::kMaxStations> roundsCleared{};
    std::array<std::int64_t, gameplay::kMaxStations> scores{};
    gameplay::ModeId gameMode = gameplay::ModeId::Battle;
    std::array<int, gameplay::kMaxStations> points{}; // race wins / crown points
    std::optional<int> winningTeam;                   // teams
};

using BotFactory = std::function<std::unique_ptr<bots::BotController>(config::Difficulty, std::uint64_t seed)>;

// The cabinet: attract, coins and credits, joining, the match, game over, back to attract.
// Pure logic on a fixed 240 Hz tick; the platform layer feeds input and draws the state.
class ArcadeMachine {
public:
    ArcadeMachine(const config::Settings& settings, gameplay::LevelProvider& levels, BotFactory botFactory,
                  Bookkeeping bookkeeping, std::uint64_t entropy);

    void applySettings(const config::Settings& settings);
    void tick(const ArcadeInput& input);

    void setServiceOpen(bool open);
    bool serviceOpen() const { return serviceOpen_; }
    void stationDisconnected(int station);
    void stationReconnected(int station);
    // The app went to the background (phone call, home button): a running match pauses until a
    // player presses start or fire, so nobody comes back to a round already in motion.
    void suspend();
    bool suspended() const { return suspended_; }

    ArcadeState state() const { return state_; }
    AttractPage attractPage() const { return attractPage_; }
    double stateTime() const { return stateTime_; }
    double pageTime() const { return pageTime_; }

    const Credits& credits() const { return credits_; }
    Credits& credits() { return credits_; }
    const Bookkeeping& bookkeeping() const { return bookkeeping_; }
    Bookkeeping& bookkeeping() { return bookkeeping_; }
    const config::Settings& settings() const { return settings_; }

    const gameplay::Match* match() const { return match_.get(); }
    const players::Lineup& lineup() const { return lineup_; }
    const std::array<bool, gameplay::kMaxStations>& joined() const { return joined_; }
    // The vote screen.
    double voteTimeLeft() const;
    const VoteBox& vote() const { return vote_; }
    VotePhase votePhase() const { return votePhase_; }
    double votePhaseTime() const { return votePhaseTime_; }
    const std::vector<int>& wheel() const { return wheel_; } // card indices on the tie-break wheel
    int wheelResult() const { return wheelResult_; }          // the slice it stops on
    gameplay::ModeId chosenMode() const { return chosenMode_; }
    gameplay::ModeId currentMode() const { return currentMode_; } // the match (or demo) being played
    std::vector<gameplay::ModeId> enabledModes() const;
    double gameOverTimeLeft() const;
    double needCoinFlash(int station) const { return needCoin_[static_cast<std::size_t>(station)]; }
    bool stationWaitingForReconnect(int station) const;
    bool pausedForDisconnect() const;
    const std::optional<MatchResult>& lastResult() const { return lastResult_; }
    const bots::BotController* bot(int station) const { return bots_[static_cast<std::size_t>(station)].get(); }

    // High scores. The platform loads the tables at boot and tells the machine today's local date.
    void setHighScores(HighScores h) { highScores_ = std::move(h); }
    const HighScores& highScores() const { return highScores_; }
    HighScores& highScores() { return highScores_; }
    void setToday(std::string_view isoDate) { highScores_.setToday(isoDate); }
    // A player shooting their initials (knocked out mid-match, or after the last round), if any.
    const std::optional<InitialsEntry>& entry(int station) const { return entries_[static_cast<std::size_t>(station)]; }
    bool entering(int station) const;
    // Table the current match's scores go into (one per game mode), and the one the attract page shows.
    std::string_view scoreTable() const { return gameplay::modeKey(currentMode_); }
    std::string_view attractTable() const { return gameplay::modeKey(gameplay::kAllModes[static_cast<std::size_t>(attractTable_)]); }
    // Every table highscores.ini may hold (the mode keys, in gameplay::kAllModes order).
    static constexpr std::array<std::string_view, gameplay::kModeCount> kScoreTables{"battle", "marathon", "blitz", "race",  "crown",
                                                                                     "sabotage", "teams", "chaos", "daily"};

    std::vector<ArcadeEvent> takeEvents();

    // Durations (seconds), exposed for screens and tests.
    static constexpr double kTitleTime = 7.0;
    static constexpr double kHowToTime = 8.0;
    static constexpr double kDemoMaxTime = 50.0;
    static constexpr double kDemoEndHold = 3.0;
    static constexpr double kHighScoresTime = 9.0;
    static constexpr double kInsertCoinIdle = 30.0;
    static constexpr double kInitialsHold = 1.2; // the saved names stay up this long before the results

private:
    void enter(ArcadeState s);
    void enterAttractPage(AttractPage p);
    void startDemo();
    void startMatch(gameplay::ModeId mode);
    void enterVote();
    void tickVote(const ArcadeInput& input, double dt);
    void finishVote();
    void startIntro();
    bool nextAttractTable();
    void endMatch();
    void tickMatch(const ArcadeInput& input, bool demo);
    void handleCoins(const ArcadeInput& input);
    void tryJoin(int station);
    void startEntry(int station);
    void tickEntries(const ArcadeInput& input, double dt);
    void tallySounds(bool demo);
    void updatePause();
    void sound(audio::SoundId id, bool demo = false);
    gameplay::MatchConfig matchConfig(bool demo, gameplay::ModeId mode);

    config::Settings settings_;
    gameplay::LevelProvider& levels_;
    BotFactory botFactory_;
    Bookkeeping bookkeeping_;
    Credits credits_;
    scripting::Pcg32 rng_;

    ArcadeState state_ = ArcadeState::Attract;
    AttractPage attractPage_ = AttractPage::Title;
    double stateTime_ = 0.0;
    double pageTime_ = 0.0;
    double demoOverTime_ = 0.0;
    double matchSeconds_ = 0.0;

    std::array<bool, gameplay::kMaxStations> joined_{};
    std::array<double, gameplay::kMaxStations> needCoin_{};
    std::array<bool, gameplay::kMaxStations> disconnected_{};
    players::Lineup lineup_;
    std::unique_ptr<gameplay::Match> match_;
    std::array<std::unique_ptr<bots::BotController>, gameplay::kMaxStations> bots_{};
    std::optional<MatchResult> lastResult_;
    HighScores highScores_;
    std::array<std::optional<InitialsEntry>, gameplay::kMaxStations> entries_{};
    std::array<bool, gameplay::kMaxStations> entryPending_{}; // knocked out: entry starts when the round end screen is over
    std::array<bool, gameplay::kMaxStations> entryChecked_{}; // this match's score was already offered to the tables
    double entriesDoneTime_ = 0.0;
    VoteBox vote_;
    VotePhase votePhase_ = VotePhase::Voting;
    double votePhaseTime_ = 0.0;
    std::vector<int> wheel_;
    int wheelResult_ = 0;
    int wheelSlice_ = 0;
    std::array<HoldRepeat, gameplay::kMaxStations> voteLeft_{}, voteRight_{};
    gameplay::ModeId chosenMode_ = gameplay::ModeId::Battle;
    gameplay::ModeId currentMode_ = gameplay::ModeId::Battle;
    int attractTable_ = 0;
    int tallyTicks_ = 0; // round end screen sounds already played this tally
    bool tallyDone_ = false;
    bool tallyCrown_ = false;
    bool serviceOpen_ = false;
    bool suspended_ = false;
    std::vector<ArcadeEvent> events_;
};

const char* arcadeStateName(ArcadeState s);

} // namespace spindle::arcade
