#include "arcade/ArcadeMachine.hpp"

#include <algorithm>
#include <format>

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
    case ArcadeState::Vote: return "vote";
    case ArcadeState::Playing: return "playing";
    case ArcadeState::Initials: return "initials";
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

double ArcadeMachine::voteTimeLeft() const {
    if (state_ != ArcadeState::Vote || votePhase_ != VotePhase::Voting) return 0.0;
    return vote_.timeLeft();
}

std::vector<gameplay::ModeId> ArcadeMachine::enabledModes() const {
    std::vector<gameplay::ModeId> out;
    for (std::size_t m = 0; m < gameplay::kModeCount; ++m)
        if (settings_.modes.enabled[m]) out.push_back(gameplay::kAllModes[m]);
    if (out.empty()) out.push_back(gameplay::ModeId::Battle); // every mode off: battle stays on
    return out;
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
    if (s != ArcadeState::GameOver && s != ArcadeState::Playing && s != ArcadeState::Initials) {
        match_.reset();
        for (auto& b : bots_) b.reset();
    }
    if (s == ArcadeState::Attract || s == ArcadeState::InsertCoin || s == ArcadeState::Vote) {
        for (auto& e : entries_) e.reset();
        entryPending_.fill(false);
        entryChecked_.fill(false);
    }
    if (s == ArcadeState::Attract || s == ArcadeState::Vote || s == ArcadeState::InsertCoin) {
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

gameplay::MatchConfig ArcadeMachine::matchConfig(bool demo, gameplay::ModeId mode) {
    gameplay::MatchConfig c;
    c.mode = gameplay::modeRules(mode);
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
    switch (settings_.gameplay.roundEndScreen) {
    case config::RoundEndScreen::Full: c.tally = gameplay::scoring::TallyStyle::Full; break;
    case config::RoundEndScreen::Short: c.tally = gameplay::scoring::TallyStyle::Short; break;
    case config::RoundEndScreen::Off: c.tally = gameplay::scoring::TallyStyle::Off; break;
    }
    // Daily: everyone gets the same levels today, whatever the operator's seed.
    if (c.mode.dailySeed && !demo) c.seed = gameplay::dailySeed(highScores_.today());
    return c;
}

void ArcadeMachine::startDemo() {
    // Each demo shows off one of the enabled modes.
    const auto modes = enabledModes();
    currentMode_ = modes[static_cast<std::size_t>(rng_.range(0, static_cast<std::int64_t>(modes.size()) - 1))];
    lineup_ = players::demoLineup();
    match_ = std::make_unique<gameplay::Match>(matchConfig(true, currentMode_), lineup_.participants(), levels_);
    for (int s = 0; s < gameplay::kMaxStations; ++s)
        bots_[static_cast<std::size_t>(s)] =
            botFactory_ ? botFactory_(settings_.bots.demoDifficulty, (static_cast<std::uint64_t>(rng_.next()) << 8) | static_cast<std::uint64_t>(s)) : nullptr;
    demoOverTime_ = 0.0;
    updatePause();
}

void ArcadeMachine::startMatch(gameplay::ModeId mode) {
    currentMode_ = mode;
    players::BotFill fill{settings_.bots.singleplayerBots, settings_.bots.multiplayerBots};
    fill.fillAll = gameplay::modeRules(mode).teams; // teams always seats four
    lineup_ = players::buildLineup(joined_, fill);
    match_ = std::make_unique<gameplay::Match>(matchConfig(false, mode), lineup_.participants(), levels_);
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
    log::info("arcade: {} {} match started: {} human(s), {} bot(s), seed {}", gameplay::modeKey(mode), players::gameModeName(lineup_.mode),
              lineup_.humans, lineup_.bots, match_->config().seed);
    matchSeconds_ = 0.0;
    lastResult_.reset();
    for (auto& e : entries_) e.reset();
    entryPending_.fill(false);
    entryChecked_.fill(false);
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
        if (const auto& p = match_->participant(s)) {
            r.roundsCleared[static_cast<std::size_t>(s)] = p->roundsCleared;
            r.scores[static_cast<std::size_t>(s)] = p->score;
            r.points[static_cast<std::size_t>(s)] = match_->modePoints(s);
        }
    r.gameMode = currentMode_;
    r.winningTeam = match_->winningTeam();
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
    // Everyone still to be offered a table (survivors, the winner, the last round's knock-outs) gets
    // their chance now; the results wait while anyone is shooting initials.
    for (int s = 0; s < gameplay::kMaxStations; ++s) startEntry(s);
    const bool anyEntering = std::any_of(entries_.begin(), entries_.end(), [](const auto& e) { return e && !e->done(); });
    state_ = anyEntering ? ArcadeState::Initials : ArcadeState::GameOver;
    stateTime_ = 0.0;
    entriesDoneTime_ = 0.0;
}

bool ArcadeMachine::entering(int station) const {
    if (station < 0 || station >= gameplay::kMaxStations) return false;
    const auto& e = entries_[static_cast<std::size_t>(station)];
    return e && !e->done();
}

void ArcadeMachine::startEntry(int station) {
    const auto i = static_cast<std::size_t>(station);
    entryPending_[i] = false;
    if (entryChecked_[i] || !match_ || lineup_.seats[i] != players::SeatKind::Human) return;
    const auto& p = match_->participant(station);
    if (!p) return;
    entryChecked_[i] = true;
    const int round = p->eliminated ? p->eliminatedRound : match_->round();
    const std::string fallback = std::format("P{}", station + 1);
    const std::uint32_t token = highScores_.reserve(scoreTable(), p->score, round, fallback, gameplay::modeRules(currentMode_).todayOnly);
    if (token == 0) return;
    entries_[i].emplace(station, token, static_cast<double>(settings_.arcade.initialsSeconds), fallback);
    events_.push_back({ArcadeEventType::SaveHighScores, audio::SoundId::Coin, station}); // the row is held under the fallback name
    log::info("arcade: station {} made the {} table with {} - shooting initials", station + 1, scoreTable(), p->score);
}

void ArcadeMachine::tickEntries(const ArcadeInput& input, double dt) {
    for (int s = 0; s < gameplay::kMaxStations; ++s) {
        auto& e = entries_[static_cast<std::size_t>(s)];
        if (!e || e->done()) continue;
        const int shotsBefore = e->shots();
        e->tick(input.stations[static_cast<std::size_t>(s)], dt);
        if (e->shots() != shotsBefore && !e->done()) sound(audio::SoundId::LetterShot);
        if (e->done()) {
            highScores_.finish(e->token(), e->name());
            sound(audio::SoundId::InitialsDone);
            events_.push_back({ArcadeEventType::SaveHighScores, audio::SoundId::Coin, s});
            log::info("arcade: station {} saved '{}'{}", s + 1, e->name(), e->timedOut() ? " (time ran out)" : "");
        }
    }
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
    if (state_ == ArcadeState::Vote && (joined_[i] || votePhase_ != VotePhase::Voting)) return;
    if (!credits_.canJoin()) {
        needCoin_[i] = 1.5;
        events_.push_back({ArcadeEventType::JoinRefused, audio::SoundId::MenuMove, station});
        return;
    }
    if (state_ != ArcadeState::Vote) enterVote();
    credits_.payForJoin();
    joined_[i] = true;
    vote_.join(station);
    sound(audio::SoundId::Join);
    log::info("arcade: station {} joined", station + 1);
}

void ArcadeMachine::enterVote() {
    enter(ArcadeState::Vote);
    vote_ = VoteBox(enabledModes(), static_cast<double>(settings_.arcade.voteSeconds));
    votePhase_ = VotePhase::Voting;
    votePhaseTime_ = 0.0;
    wheel_.clear();
    for (auto& h : voteLeft_) h.reset();
    for (auto& h : voteRight_) h.reset();
}

void ArcadeMachine::startIntro() {
    votePhase_ = VotePhase::Intro;
    votePhaseTime_ = 0.0;
    sound(audio::SoundId::ModeIntro);
    log::info("arcade: game mode {}", gameplay::modeKey(chosenMode_));
}

void ArcadeMachine::finishVote() {
    const std::vector<int> leaders = vote_.leaders();
    if (leaders.size() == 1) {
        chosenMode_ = vote_.modes()[static_cast<std::size_t>(leaders.front())];
        startIntro();
        return;
    }
    // A tie (or nobody voted): the tied modes go on the wheel as equal slices.
    wheel_ = leaders;
    wheelResult_ = static_cast<int>(rng_.range(0, static_cast<std::int64_t>(wheel_.size()) - 1));
    wheelSlice_ = wheelSliceAt(0.0, static_cast<int>(wheel_.size()));
    votePhase_ = VotePhase::Spin;
    votePhaseTime_ = 0.0;
    log::info("arcade: vote tied between {} modes - spinning the wheel", wheel_.size());
}

void ArcadeMachine::tickVote(const ArcadeInput& input, double dt) {
    switch (votePhase_) {
    case VotePhase::Voting:
        for (int s = 0; s < gameplay::kMaxStations && votePhase_ == VotePhase::Voting; ++s) {
            const auto i = static_cast<std::size_t>(s);
            const StationButtons& st = input.stations[i];
            if (!vote_.joined(s)) {
                if (st.startPressed) tryJoin(s);
                continue;
            }
            const int steps = voteRight_[i].update(st.rightPresses, st.rightHeld, dt) - voteLeft_[i].update(st.leftPresses, st.leftHeld, dt);
            if (steps != 0 && vote_.modes().size() > 1) {
                vote_.move(s, steps);
                sound(audio::SoundId::VoteTick);
            }
            if (st.firePresses > 0 || st.startPressed) {
                if (vote_.modes().size() == 1) { // one mode on: pressing again simply starts
                    finishVote();
                    break;
                }
                vote_.cast(s);
                sound(audio::SoundId::VoteCast);
            }
        }
        if (votePhase_ != VotePhase::Voting) break;
        vote_.tick(dt);
        if (vote_.expired()) finishVote();
        break;
    case VotePhase::Spin: {
        votePhaseTime_ += dt;
        const int n = static_cast<int>(wheel_.size());
        const int slice = wheelSliceAt(wheelAngle(votePhaseTime_, n, wheelResult_), n);
        if (slice != wheelSlice_) {
            wheelSlice_ = slice;
            sound(audio::SoundId::WheelTick);
        }
        if (votePhaseTime_ >= VoteBox::kSpinSeconds) {
            sound(audio::SoundId::WheelStop);
            chosenMode_ = vote_.modes()[static_cast<std::size_t>(wheel_[static_cast<std::size_t>(wheelResult_)])];
            startIntro();
        }
        break;
    }
    case VotePhase::Intro:
        votePhaseTime_ += dt;
        if (votePhaseTime_ >= VoteBox::kIntroSeconds) startMatch(chosenMode_);
        break;
    }
}

bool ArcadeMachine::nextAttractTable() {
    for (std::size_t k = 1; k <= gameplay::kModeCount; ++k) {
        const std::size_t m = (static_cast<std::size_t>(attractTable_) + k) % gameplay::kModeCount;
        const auto& t = highScores_.tables(kScoreTables[m]);
        if (!t.allTime.empty() || !t.today.empty()) {
            attractTable_ = static_cast<int>(m);
            return true;
        }
    }
    return false;
}

// The round end screen's sounds, on the core's timeline: a rising tick per digit, a chord when the
// number is in, the crown fanfare when it changes hands.
void ArcadeMachine::tallySounds(bool demo) {
    if (match_->phase() != gameplay::MatchPhase::Tally) {
        tallyTicks_ = 0;
        tallyDone_ = false;
        tallyCrown_ = false;
        return;
    }
    const auto& t = match_->tallyTiming();
    const double e = match_->phaseTime();
    if (t.perDigit > 0.0) {
        int landed = 0;
        for (int k = 0; k < t.digits; ++k)
            if (e >= t.digitLand(k)) ++landed;
        constexpr audio::SoundId kTicks[] = {audio::SoundId::TallyTick1, audio::SoundId::TallyTick2, audio::SoundId::TallyTick3,
                                             audio::SoundId::TallyTick4};
        for (; tallyTicks_ < landed; ++tallyTicks_) sound(kTicks[std::min(tallyTicks_, 3)], demo);
        if (!tallyDone_ && e >= t.breakdownStart()) {
            tallyDone_ = true;
            sound(audio::SoundId::TallyDone, demo);
        }
    }
    const int holder = match_->crownHolder();
    if (!tallyCrown_ && e >= t.crownStart() && holder >= 0 && holder != match_->crownBefore()) {
        tallyCrown_ = true;
        sound(audio::SoundId::Crown, demo);
    }
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
    tallySounds(demo);
    for (const gameplay::MatchEvent& e : match_->takeEvents()) {
        switch (e.type) {
        case gameplay::MatchEventType::RoundStarted:
            for (auto& b : bots_)
                if (b) b->beginRound(match_->mutator() == gameplay::Mutator::Blind);
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
        case gameplay::MatchEventType::Eliminated:
            // A knocked-out player may have a high score: they shoot their initials once this round's
            // end screen is over, in their own column, while the others play on.
            if (!demo && e.station >= 0 && lineup_.seats[static_cast<std::size_t>(e.station)] == players::SeatKind::Human)
                entryPending_[static_cast<std::size_t>(e.station)] = true;
            break;
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
                enterAttractPage(nextAttractTable() ? AttractPage::HighScores : AttractPage::Title);
            break;
        case AttractPage::HighScores:
            if (pageTime_ >= kHighScoresTime) enterAttractPage(AttractPage::Title);
            break;
        }
        for (int s = 0; s < gameplay::kMaxStations && state_ == ArcadeState::Attract; ++s)
            if (input.stations[static_cast<std::size_t>(s)].startPressed) tryJoin(s);
        // A start that opened the vote screen may be followed by others in the same tick.
        if (state_ == ArcadeState::Vote)
            for (int s = 0; s < gameplay::kMaxStations; ++s)
                if (input.stations[static_cast<std::size_t>(s)].startPressed && !joined_[static_cast<std::size_t>(s)]) tryJoin(s);
        break;
    }
    case ArcadeState::InsertCoin:
    case ArcadeState::GameOver: {
        for (int s = 0; s < gameplay::kMaxStations; ++s) {
            if (!input.stations[static_cast<std::size_t>(s)].startPressed) continue;
            if (state_ == ArcadeState::Vote && joined_[static_cast<std::size_t>(s)]) continue;
            tryJoin(s);
        }
        if (state_ == ArcadeState::InsertCoin && stateTime_ >= kInsertCoinIdle) enter(ArcadeState::Attract);
        if (state_ == ArcadeState::GameOver && stateTime_ >= settings_.arcade.gameOverTime) {
            const bool canPlay = credits_.freePlay() || credits_.credits() > 0;
            enter(canPlay ? ArcadeState::InsertCoin : ArcadeState::Attract);
        }
        break;
    }
    case ArcadeState::Vote: tickVote(input, dt); break;
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
        tickEntries(input, dt);
        tickMatch(input, false);
        if (match_ && match_->phase() != gameplay::MatchPhase::Tally)
            for (int s = 0; s < gameplay::kMaxStations; ++s)
                if (entryPending_[static_cast<std::size_t>(s)]) startEntry(s);
        if (match_ && match_->over()) endMatch();
        break;
    }
    case ArcadeState::Initials: {
        tickEntries(input, dt);
        const bool anyEntering = std::any_of(entries_.begin(), entries_.end(), [](const auto& e) { return e && !e->done(); });
        if (!anyEntering) entriesDoneTime_ += dt;
        if (entriesDoneTime_ >= kInitialsHold) {
            state_ = ArcadeState::GameOver;
            stateTime_ = 0.0;
        }
        break;
    }
    }
}

} // namespace spindle::arcade
