// Replays: the format round-trips, a recorded match plays back to the same scores without bots, damaged
// files are refused, and the arcade keeps a mode's record game and shows it on the attract loop.

#include <algorithm>
#include <memory>

#include "Test.hpp"
#include "arcade/ArcadeMachine.hpp"
#include "arcade/Replay.hpp"
#include "bots/BotController.hpp"
#include "gameplay/Geometry.hpp"
#include "gameplay/LevelScript.hpp"

using namespace spindle;

namespace {

std::unique_ptr<bots::BotController> makeBot(config::Difficulty d, std::uint64_t seed) {
    std::string err;
    auto brain = bots::BotBrain::create(bots::BotBrain::embeddedSource(), d, seed, &err);
    return brain ? std::make_unique<bots::BotController>(std::move(brain)) : nullptr;
}

// Four CPUs play a sabotage match (junk and close calls exercise the most rules) while it is recorded.
arcade::Replay recordBotMatch(gameplay::LevelProvider& levels, std::uint32_t seed) {
    gameplay::MatchConfig c;
    c.mode = gameplay::modeRules(gameplay::ModeId::Sabotage);
    c.seed = seed;
    c.tries = 2;
    c.fireMeter = true;
    c.powerups.assign(gameplay::kAllPowerups.begin(), gameplay::kAllPowerups.end());
    c.tally = gameplay::scoring::TallyStyle::Short;
    std::vector<gameplay::ParticipantSpec> specs;
    std::array<std::unique_ptr<bots::BotController>, gameplay::kMaxStations> bots;
    for (int s = 0; s < gameplay::kMaxStations; ++s) {
        specs.push_back({s, gameplay::ParticipantKind::Bot});
        bots[static_cast<std::size_t>(s)] = makeBot(config::Difficulty::Normal, 1000u + static_cast<std::uint64_t>(s));
    }
    gameplay::Match m(c, specs, levels);
    arcade::ReplayRecorder rec;
    rec.begin(m.config(), specs, gameplay::ModeId::Sabotage, "test-build");
    for (int i = 0; i < 400 * gameplay::geom::kTickRate && !m.over(); ++i) {
        std::array<int, gameplay::kMaxStations> fire{};
        if (m.phase() == gameplay::MatchPhase::Playing) {
            for (int s = 0; s < gameplay::kMaxStations; ++s)
                if (m.stationInRound(s))
                    fire[static_cast<std::size_t>(s)] = bots[static_cast<std::size_t>(s)]->think(m.rotor().state(), m.field(s), m.roundTime(),
                                                                                                m.roundTimeLeft(), m.tickCount() + 1);
            for (const auto& e : m.takeEvents())
                if (e.type == gameplay::MatchEventType::RoundStarted)
                    for (auto& b : bots) b->beginRound(false);
        } else {
            for (const auto& e : m.takeEvents())
                if (e.type == gameplay::MatchEventType::RoundStarted)
                    for (auto& b : bots) b->beginRound(false);
        }
        rec.record(m.tickCount(), fire);
        m.tick(fire);
    }
    rec.finish(m);
    REQUIRE(rec.finished());
    return rec.replay();
}

} // namespace

TEST_CASE(replay_plays_a_recorded_match_back_to_the_same_scores_without_bots) {
    gameplay::LevelScript levels;
    const arcade::Replay r = recordBotMatch(levels, 4242);
    CHECK(r.presses.size() > 20);
    CHECK(r.endTick > 0);
    const std::int64_t total = r.finalScores[0] + r.finalScores[1] + r.finalScores[2] + r.finalScores[3];
    CHECK(total > 0);
    gameplay::LevelScript fresh; // a new level script: same seed, same rounds
    CHECK(arcade::runReplay(r, fresh) == r.finalScores);
    // Through the text format too.
    const auto back = arcade::Replay::parse(r.serialize());
    REQUIRE(back.has_value());
    CHECK(*back == r);
    gameplay::LevelScript again;
    CHECK(arcade::runReplay(*back, again) == r.finalScores);
    // One press moved by a tick is a different game (the check means something).
    arcade::Replay changed = r;
    changed.presses.front().tick += 1;
    gameplay::LevelScript other;
    CHECK(arcade::runReplay(changed, other) != r.finalScores);
}

TEST_CASE(replay_refuses_damaged_files) {
    gameplay::LevelScript levels;
    const std::string good = recordBotMatch(levels, 7).serialize();
    CHECK(arcade::Replay::parse(good).has_value());
    std::string err;
    std::string cut = good.substr(0, good.size() - 12); // a press line lost
    CHECK(!arcade::Replay::parse(cut, &err).has_value());
    CHECK(!err.empty());
    std::string bad = good;
    bad.replace(bad.find("mode = sabotage"), 15, "mode = bowling");
    CHECK(!arcade::Replay::parse(bad).has_value());
    CHECK(!arcade::Replay::parse("").has_value());
    CHECK(!arcade::Replay::parse("presses = 1\n5 9 1\n").has_value());
}

TEST_CASE(replay_the_record_game_is_kept_and_shown_before_its_table) {
    gameplay::LevelScript levels;
    config::Settings s = config::Settings::defaults();
    s.gameplay.seed = 99;
    s.gameplay.tries = 1;
    s.arcade.freePlay = true;
    s.arcade.voteSeconds = 10;
    s.arcade.initialsSeconds = 10;
    s.gameplay.roundEndScreen = config::RoundEndScreen::Short;
    s.bots.singleplayerBots = 1;
    s.bots.difficulty = config::Difficulty::Easy;
    arcade::ArcadeMachine m(s, levels, makeBot, {}, 3);
    m.setReplayBuild("build-a");
    m.setToday("2026-11-26");
    constexpr int kSecond = gameplay::geom::kTickRate;
    arcade::ArcadeInput start;
    start.stations[0].startPressed = true;
    m.tick(start);
    m.tick(start); // votes for battle
    for (int i = 0; i < 20 * kSecond && m.state() == arcade::ArcadeState::Vote; ++i) m.tick({});
    REQUIRE(m.state() == arcade::ArcadeState::Playing);
    // P1 fires steadily (a few needles score) until the match ends.
    bool saveEvent = false;
    for (int i = 0; i < 300 * kSecond && m.state() == arcade::ArcadeState::Playing; ++i) {
        arcade::ArcadeInput in;
        const gameplay::Match* match = m.match();
        if (match && match->phase() == gameplay::MatchPhase::Playing && match->stationInRound(0) && i % (kSecond / 2) == 0)
            in.stations[0].firePresses = 1;
        m.tick(in);
        for (const auto& e : m.takeEvents()) saveEvent |= e.type == arcade::ArcadeEventType::SaveReplay;
    }
    REQUIRE(m.state() == arcade::ArcadeState::Initials);
    // "end" right away: the fallback name P1.
    arcade::ArcadeInput left;
    left.stations[0].leftPresses = 1;
    m.tick(left);
    arcade::ArcadeInput fire;
    fire.stations[0].firePresses = 1;
    m.tick(fire);
    for (const auto& e : m.takeEvents()) saveEvent |= e.type == arcade::ArcadeEventType::SaveReplay;
    const auto& rows = m.highScores().tables("battle").allTime.rows();
    REQUIRE(!rows.empty());
    const auto& kept = m.replay(gameplay::ModeId::Battle);
    REQUIRE(kept.has_value());
    CHECK(saveEvent);
    CHECK_EQ(kept->name, rows.front().name);
    CHECK_EQ(kept->score, rows.front().score);
    CHECK_EQ(kept->build, std::string("build-a"));
    gameplay::LevelScript fresh;
    CHECK(arcade::runReplay(*kept, fresh) == kept->finalScores); // the arcade's own recording replays exactly

    // Another build's replay is refused; the arcade's own is accepted.
    arcade::Replay foreign = *kept;
    foreign.build = "build-b";
    CHECK(!m.setReplay(foreign));
    CHECK(m.setReplay(*kept));

    // Back on the attract loop: after the demo, the record game plays before the battle table.
    bool sawReplay = false, sawTable = false;
    for (int i = 0; i < 400 * kSecond && !sawTable; ++i) {
        m.tick({});
        if (m.state() != arcade::ArcadeState::Attract) continue;
        if (m.attractPage() == arcade::AttractPage::Replay) {
            sawReplay = true;
            CHECK(m.showingReplay() != nullptr);
        }
        if (m.attractPage() == arcade::AttractPage::HighScores) sawTable = true;
    }
    CHECK(sawReplay);
    CHECK(sawTable);

    // A player starting while the record game plays gets a match of their own: their presses count and
    // the replay is gone (it once kept feeding its presses into the new match).
    for (int i = 0; i < 400 * kSecond && !(m.state() == arcade::ArcadeState::Attract && m.attractPage() == arcade::AttractPage::Replay); ++i)
        m.tick({});
    REQUIRE(m.attractPage() == arcade::AttractPage::Replay);
    for (int i = 0; i < 2 * kSecond; ++i) m.tick({}); // well into the record game
    m.tick(start);
    m.tick(start);
    CHECK(m.showingReplay() == nullptr);
    for (int i = 0; i < 20 * kSecond && m.state() == arcade::ArcadeState::Vote; ++i) m.tick({});
    REQUIRE(m.state() == arcade::ArcadeState::Playing);
    for (int i = 0; i < 10 * kSecond && m.match()->phase() != gameplay::MatchPhase::Playing; ++i) m.tick({});
    REQUIRE(m.match()->phase() == gameplay::MatchPhase::Playing);
    const int before = m.match()->field(0).needlesLeft();
    arcade::ArcadeInput shoot;
    shoot.stations[0].firePresses = 1;
    m.tick(shoot);
    m.tick({});
    CHECK(m.match()->field(0).needlesLeft() < before);
}
