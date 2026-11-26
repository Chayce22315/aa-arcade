#include "arcade/ArcadeMachine.hpp"

#include <algorithm>

#include "engine/Log.hpp"
#include "gameplay/Geometry.hpp"

namespace spindle::arcade {
namespace {

CreditConfig creditConfigFrom(const config::Settings& s) {
    return {s.arcade.freePlay, s.arcade.creditsPerCoin, s.arcade.creditsPerGame, s.arcade.maxCredits};
}

} // namespace

const char* arcadeStateName(ArcadeState s) {
    switch (s) {
    case ArcadeState::Attract: return "attract";
    case ArcadeState::InsertCoin: return "insert coin";
    case ArcadeState::Join: return "join";
    case ArcadeState::Playing: return "playing";
    case ArcadeState::GameOver: return "game over";
    }
    return "attract";
}

ArcadeMachine::ArcadeMachine(const config::Settings& settings, gameplay::LevelProvider& levels, BotFactory botFactory,
                             Bookkeeping bookkeeping, std::uint64_t entropy)
    : settings_(settings), levels_(levels), botFactory_(std::move(botFactory)), bookkeeping_(std::move(bookkeeping)),
      credits_(creditConfigFrom(settings)), rng_(entropy, 0x5851F42D4C957F2DULL) {
    enter(ArcadeState::Attract);
}

void ArcadeMachine::applySettings(const config::Settings& settings) {
    settings_ = settings;
    credits_.configure(creditConfigFrom(settings));
    updatePause();
}

double ArcadeMachine::joinTimeLeft() const {
    if (state_ != ArcadeState::Join) return 0.0;
    return std::max(0.0, settings_.arcade.joinWindow - stateTime_);
}

double ArcadeMachine::gameOverTimeLeft() const {
    if (state_ != ArcadeState::GameOver) return 0.0;
    return std::max(0.0, settings_.arcade.gameOverTime - stateTime_);
}

bool ArcadeMachine::stationWaitingForReconnect(int station) const {
    if (station < 0 || station >= gameplay::kMaxStations) return false;
    const auto i = static_cast<std::size_t>(station);
    return disconnected_[i] && state_ == ArcadeState::Playing && lineup_.seats[i] == players::SeatKind::Human;
}

bool ArcadeMachine::pausedForDisconnect() const {
    if (settings_.input.disconnectAction != config::DisconnectAction::Pause) return false;
    for (int s = 0; s < gameplay::kMaxStations; ++s)
        if (stationWaitingForReconnect(s)) return true;
    return false;
}

void ArcadeMachine::updatePause() {
    if (match_) match_->setPaused(serviceOpen_ || suspended_ || pausedForDisconnect());
}

void ArcadeMachine::suspend() {
    if (state_ != ArcadeState::Playing || !match_ || match_->over()) return;
    if (!suspended_) log::info("arcade: match suspended (app in the background)");
    suspended_ = true;
    updatePause();
}

void ArcadeMachine::setServiceOpen(bool open) {
    serviceOpen_ = open;
    updatePause();
}

void ArcadeMachine::stationDisconnected(int station) {
    if (station < 0 || station >= gameplay::kMaxStations) return;
    disconnected_[static_cast<std::size_t>(station)] = true;
    if (stationWaitingForReconnect(station)) log::warn("arcade: station {} lost its controller during a match", station + 1);
    updatePause();
}

void ArcadeMachine::stationReconnected(int station) {
    if (station < 0 || station >= gameplay::kMaxStations) return;
    if (disconnected_[static_cast<std::size_t>(station)]) log::info("arcade: station {} input restored", station + 1);
    disconnected_[static_cast<std::size_t>(station)] = false;
    updatePause();
}

void ArcadeMachine::sound(audio::SoundId id, bool demo) {
    if (demo && !settings_.audio.attractSound) return;
    events_.push_back({ArcadeEventType::Sound, id, -1});
}

std::vector<ArcadeEvent> ArcadeMachine::takeEvents() {
    std::vector<ArcadeEvent> out;
    out.swap(events_);
    return out;
}

void ArcadeMachine::enter(ArcadeState s) {
    state_ = s;
    stateTime_ = 0.0;
    if (s != ArcadeState::Playing) suspended_ = false;
    if (s != ArcadeState::GameOver && s != ArcadeState::Playing) {
        match_.reset();
        for (auto& b : bots_) b.reset();
    }
    if (s == ArcadeState::Attract || s == ArcadeState::Join || s == ArcadeState::InsertCoin) {
        joined_.fill(false);
        disconnected_.fill(false);
    }
    if (s == ArcadeState::Attract) enterAttractPage(AttractPage::Title);
    log::debug("arcade: state -> {}", arcadeStateName(s));
}

void ArcadeMachine::enterAttractPage(AttractPage p) {
    attractPage_ = p;
    pageTime_ = 0.0;
    if (p == AttractPage::Demo) startDemo();
    if (p != AttractPage::Demo) {
        match_.reset();
        for (auto& b : bots_) b.reset();
    }
}

gameplay::MatchConfig ArcadeMachine::matchConfig(bool demo) {
    gameplay::MatchConfig c;
    c.tries = demo ? 1 : settings_.gameplay.tries;
    c.roundTimeLimit = settings_.gameplay.roundTimeLimit;
    c.readyDelay = settings_.gameplay.readyDelay;
    c.startRound = demo ? 1 + static_cast<int>(rng_.range(0, 7)) : settings_.gameplay.startRound;
    c.seed = settings_.gameplay.seed != 0 ? settings_.gameplay.seed : (rng_.next() | 1u);
    c.rotationScale = settings_.gameplay.rotationSpeedScale;
    c.needleScale = settings_.gameplay.needleSpeedScale;
    if (settings_.gameplay.powerups) c.powerups = gameplay::parsePowerupList(settings_.gameplay.powerupTypes);
    c.fireMeter = settings_.gameplay.fireMeter;
    c.fireCombo = settings_.gameplay.fireCombo;
    c.fireSeconds = settings_.gameplay.fireSeconds;
    c.fireChainWindow = settings_.gameplay.fireChainWindow;
    return c;
}

void ArcadeMachine::startDemo() {
    lineup_ = players::demoLineup();
    match_ = std::make_unique<gameplay::Match>(matchConfig(true), lineup_.participants(), levels_);
    for (int s = 0; s < gameplay::kMaxStations; ++s)
        bots_[static_cast<std::size_t>(s)] =
            botFactory_ ? botFactory_(settings_.bots.demoDifficulty, (static_cast<std::uint64_t>(rng_.next()) << 8) | static_cast<std::uint64_t>(s)) : nullptr;
    demoOverTime_ = 0.0;
    updatePause();
}

void ArcadeMachine::startMatch() {
    players::BotFill fill{settings_.bots.singleplayerBots, settings_.bots.multiplayerBots};
    lineup_ = players::buildLineup(joined_, fill);
    match_ = std::make_unique<gameplay::Match>(matchConfig(false), lineup_.participants(), levels_);
    for (int s = 0; s < gameplay::kMaxStations; ++s) {
        auto& b = bots_[static_cast<std::size_t>(s)];
        b.reset();
        if (lineup_.seats[static_cast<std::size_t>(s)] == players::SeatKind::Bot && botFactory_) {
            b = botFactory_(settings_.bots.difficulty, (static_cast<std::uint64_t>(rng_.next()) << 8) | static_cast<std::uint64_t>(s));
            if (!b) log::error("arcade: bot for station {} could not be created; it will not fire", s + 1);
        }
    }
    if (lineup_.mode == players::GameMode::Multiplayer)
        ++bookkeeping_.gamesMultiplayer;
    else
        ++bookkeeping_.gamesSingleplayer;
    log::info("arcade: {} match started: {} human(s), {} bot(s), seed {}", players::gameModeName(lineup_.mode), lineup_.humans,
              lineup_.bots, match_->config().seed);
    matchSeconds_ = 0.0;
    lastResult_.reset();
    events_.push_back({ArcadeEventType::MatchStarted, audio::SoundId::RoundStart, -1});
    events_.push_back({ArcadeEventType::SaveBookkeeping, audio::SoundId::Coin, -1});
    state_ = ArcadeState::Playing;
    stateTime_ = 0.0;
    updatePause();
}

void ArcadeMachine::endMatch() {
    MatchResult r;
    r.mode = lineup_.mode;
    r.lineup = lineup_;
    r.winner = match_->winnerStation();
    r.standings = match_->standings();
    r.finalRound = match_->round();
    for (int s = 0; s < gameplay::kMaxStations; ++s)
        if (const auto& p = match_->participant(s)) r.roundsCleared[static_cast<std::size_t>(s)] = p->roundsCleared;
    if (r.winner >= 0) {
        const bool bot = lineup_.seats[static_cast<std::size_t>(r.winner)] == players::SeatKind::Bot;
        bookkeeping_.addWinner({r.winner, bot, r.finalRound});
        if (bot)
            ++bookkeeping_.botWins;
        else
            ++bookkeeping_.humanWins;
    }
    bookkeeping_.playSeconds += static_cast<std::uint64_t>(matchSeconds_);
    log::info("arcade: match over after round {}: {}", r.finalRound,
              r.winner >= 0 ? "winner P" + std::to_string(r.winner + 1) : std::string("no winner (single participant)"));
    lastResult_ = std::move(r);
    events_.push_back({ArcadeEventType::MatchEnded, audio::SoundId::Winner, -1});
    events_.push_back({ArcadeEventType::SaveBookkeeping, audio::SoundId::Coin, -1});
    state_ = ArcadeState::GameOver;
    stateTime_ = 0.0;
}

void ArcadeMachine::handleCoins(const ArcadeInput& input) {
    bool any = false;
    for (int i = 0; i < input.coins; ++i) {
        credits_.insertCoin();
        ++bookkeeping_.coins;
        sound(audio::SoundId::Coin);
        any = true;
    }
    for (int i = 0; i < input.serviceCredits; ++i) {
        credits_.serviceCredit();
        ++bookkeeping_.serviceCredits;
        sound(audio::SoundId::Coin);
        any = true;
    }
    if (!any) return;
    events_.push_back({ArcadeEventType::SaveBookkeeping, audio::SoundId::Coin, -1});
    if (state_ == ArcadeState::Attract) enter(ArcadeState::InsertCoin);
    if (state_ == ArcadeState::InsertCoin) stateTime_ = 0.0;
}

void ArcadeMachine::tryJoin(int station) {
    const auto i = static_cast<std::size_t>(station);
    if (state_ == ArcadeState::Join && joined_[i]) return;
    if (!credits_.canJoin()) {
        needCoin_[i] = 1.5;
        events_.push_back({ArcadeEventType::JoinRefused, audio::SoundId::MenuMove, station});
        return;
    }
    if (state_ != ArcadeState::Join) enter(ArcadeState::Join);
    credits_.payForJoin();
    joined_[i] = true;
    sound(audio::SoundId::Join);
    log::info("arcade: station {} joined", station + 1);
}

void ArcadeMachine::tickMatch(const ArcadeInput& input, bool demo) {
    if (!match_) return;
    std::array<int, gameplay::kMaxStations> fire{};
    if (match_->phase() == gameplay::MatchPhase::Playing && !match_->paused()) {
        for (int s = 0; s < gameplay::kMaxStations; ++s) {
            const auto i = static_cast<std::size_t>(s);
            if (!demo && lineup_.seats[i] == players::SeatKind::Human) fire[i] = input.stations[i].firePresses;
            if (bots_[i] && match_->stationInRound(s))
                fire[i] += bots_[i]->think(match_->rotor().state(), match_->field(s), match_->roundTime(), match_->roundTimeLeft(),
                                           match_->tickCount() + 1);
        }
    }
    match_->tick(fire);
    if (!match_->paused() && !demo) matchSeconds_ += gameplay::geom::kTickSeconds;
    for (const gameplay::MatchEvent& e : match_->takeEvents()) {
        switch (e.type) {
        case gameplay::MatchEventType::RoundStarted:
            for (auto& b : bots_)
                if (b) b->beginRound();
            sound(audio::SoundId::RoundStart, demo);
            break;
        case gameplay::MatchEventType::NeedlePlaced: sound(audio::SoundId::NeedleHit, demo); break;
        case gameplay::MatchEventType::StationFailed: sound(audio::SoundId::Fail, demo); break;
        case gameplay::MatchEventType::StationCleared: sound(audio::SoundId::Clear, demo); break;
        case gameplay::MatchEventType::Winner: sound(audio::SoundId::Winner, demo); break;
        case gameplay::MatchEventType::NoWinner: sound(audio::SoundId::Fail, demo); break;
        case gameplay::MatchEventType::PowerupClaimed: sound(audio::SoundId::Powerup, demo); break;
        case gameplay::MatchEventType::ShieldBroke: sound(audio::SoundId::Shield, demo); break;
        case gameplay::MatchEventType::Ignited: sound(audio::SoundId::Ignite, demo); break;
        case gameplay::MatchEventType::Burned: sound(audio::SoundId::Burn, demo); break;
        default: break;
        }
    }
}

void ArcadeMachine::tick(const ArcadeInput& input) {
    if (serviceOpen_) return;
    constexpr double dt = gameplay::geom::kTickSeconds;
    stateTime_ += dt;
    pageTime_ += dt;
    for (double& t : needCoin_) t = std::max(0.0, t - dt);
    handleCoins(input);

    switch (state_) {
    case ArcadeState::Attract: {
        switch (attractPage_) {
        case AttractPage::Title:
            if (pageTime_ >= kTitleTime) enterAttractPage(AttractPage::HowTo);
            break;
        case AttractPage::HowTo:
            if (pageTime_ >= kHowToTime) enterAttractPage(AttractPage::Demo);
            break;
        case AttractPage::Demo:
            tickMatch(input, true);
            if (match_ && match_->over()) demoOverTime_ += dt;
            if (demoOverTime_ >= kDemoEndHold || pageTime_ >= kDemoMaxTime)
                enterAttractPage(bookkeeping_.recentWinners.empty() ? AttractPage::Title : AttractPage::Winners);
            break;
        case AttractPage::Winners:
            if (pageTime_ >= kWinnersTime) enterAttractPage(AttractPage::Title);
            break;
        }
        for (int s = 0; s < gameplay::kMaxStations && state_ == ArcadeState::Attract; ++s)
            if (input.stations[static_cast<std::size_t>(s)].startPressed) tryJoin(s);
        // A start that opened the join window may be followed by others in the same tick.
        if (state_ == ArcadeState::Join)
            for (int s = 0; s < gameplay::kMaxStations; ++s)
                if (input.stations[static_cast<std::size_t>(s)].startPressed && !joined_[static_cast<std::size_t>(s)]) tryJoin(s);
        break;
    }
    case ArcadeState::InsertCoin:
    case ArcadeState::GameOver: {
        for (int s = 0; s < gameplay::kMaxStations; ++s) {
            if (!input.stations[static_cast<std::size_t>(s)].startPressed) continue;
            if (state_ == ArcadeState::Join && joined_[static_cast<std::size_t>(s)]) continue;
            tryJoin(s);
        }
        if (state_ == ArcadeState::InsertCoin && stateTime_ >= kInsertCoinIdle) enter(ArcadeState::Attract);
        if (state_ == ArcadeState::GameOver && stateTime_ >= settings_.arcade.gameOverTime) {
            const bool canPlay = credits_.freePlay() || credits_.credits() > 0;
            enter(canPlay ? ArcadeState::InsertCoin : ArcadeState::Attract);
        }
        break;
    }
    case ArcadeState::Join: {
        for (int s = 0; s < gameplay::kMaxStations && state_ == ArcadeState::Join; ++s) {
            if (!input.stations[static_cast<std::size_t>(s)].startPressed) continue;
            if (joined_[static_cast<std::size_t>(s)])
                startMatch();
            else
                tryJoin(s);
        }
        if (state_ == ArcadeState::Join) {
            const bool full = std::all_of(joined_.begin(), joined_.end(), [](bool j) { return j; });
            if (full || stateTime_ >= settings_.arcade.joinWindow) startMatch();
        }
        break;
    }
    case ArcadeState::Playing: {
        if (suspended_) {
            // Any human start or fire resumes; that press is used up waking the match.
            bool wake = false;
            for (int s = 0; s < gameplay::kMaxStations; ++s) {
                const auto& st = input.stations[static_cast<std::size_t>(s)];
                if (lineup_.seats[static_cast<std::size_t>(s)] == players::SeatKind::Human && (st.startPressed || st.firePresses > 0)) wake = true;
            }
            if (!wake) break;
            suspended_ = false;
            log::info("arcade: match resumed");
            updatePause();
            break;
        }
        for (int s = 0; s < gameplay::kMaxStations; ++s) {
            // Pressing start at a station whose controller vanished hands it to that station's keys.
            if (stationWaitingForReconnect(s) && input.stations[static_cast<std::size_t>(s)].startPressed) stationReconnected(s);
        }
        tickMatch(input, false);
        if (match_ && match_->over()) endMatch();
        break;
    }
    }
}

} // namespace spindle::arcade
