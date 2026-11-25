#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "arcade/Bookkeeping.hpp"
#include "arcade/Credits.hpp"
#include "audio/SoundId.hpp"
#include "bots/BotController.hpp"
#include "config/Settings.hpp"
#include "gameplay/Match.hpp"
#include "players/PlayerManager.hpp"
#include "scripting/LuaSandbox.hpp"

namespace spindle::arcade {

enum class ArcadeState { Attract, InsertCoin, Join, Playing, GameOver };
enum class AttractPage { Title, HowTo, Demo, Winners };

struct StationButtons {
    int firePresses = 0;
    bool startPressed = false;
};

// One simulation tick worth of cabinet input (edges, not levels).
struct ArcadeInput {
    std::array<StationButtons, gameplay::kMaxStations> stations{};
    int coins = 0;
    int serviceCredits = 0;
};

enum class ArcadeEventType { Sound, SaveBookkeeping, MatchStarted, MatchEnded, JoinRefused };

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
    double joinTimeLeft() const;
    double gameOverTimeLeft() const;
    double needCoinFlash(int station) const { return needCoin_[static_cast<std::size_t>(station)]; }
    bool stationWaitingForReconnect(int station) const;
    bool pausedForDisconnect() const;
    const std::optional<MatchResult>& lastResult() const { return lastResult_; }
    const bots::BotController* bot(int station) const { return bots_[static_cast<std::size_t>(station)].get(); }

    std::vector<ArcadeEvent> takeEvents();

    // Durations (seconds), exposed for screens and tests.
    static constexpr double kTitleTime = 7.0;
    static constexpr double kHowToTime = 8.0;
    static constexpr double kDemoMaxTime = 50.0;
    static constexpr double kDemoEndHold = 3.0;
    static constexpr double kWinnersTime = 6.0;
    static constexpr double kInsertCoinIdle = 30.0;

private:
    void enter(ArcadeState s);
    void enterAttractPage(AttractPage p);
    void startDemo();
    void startMatch();
    void endMatch();
    void tickMatch(const ArcadeInput& input, bool demo);
    void handleCoins(const ArcadeInput& input);
    void tryJoin(int station);
    void updatePause();
    void sound(audio::SoundId id, bool demo = false);
    gameplay::MatchConfig matchConfig(bool demo);

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
    bool serviceOpen_ = false;
    std::vector<ArcadeEvent> events_;
};

const char* arcadeStateName(ArcadeState s);

} // namespace spindle::arcade
