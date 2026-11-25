#include <memory>

#include "Test.hpp"
#include "bots/BotController.hpp"
#include "gameplay/LevelScript.hpp"
#include "gameplay/Geometry.hpp"
#include "gameplay/Match.hpp"

using namespace spindle;

namespace {

std::unique_ptr<bots::BotController> makeBot(config::Difficulty d, std::uint64_t seed) {
    std::string err;
    auto brain = bots::BotBrain::create(bots::BotBrain::embeddedSource(), d, seed, &err);
    if (!brain) {
        test::fail(__FILE__, __LINE__, "bot create failed: " + err);
        return nullptr;
    }
    return std::make_unique<bots::BotController>(std::move(brain));
}

struct BotMatchResult {
    int winner = -1;
    int rounds = 0;
};

BotMatchResult playBots(const std::array<config::Difficulty, 4>& diff, std::uint32_t seed, int seats = 4) {
    gameplay::LevelScript levels;
    gameplay::MatchConfig c;
    c.seed = seed;
    c.tries = 3;
    std::vector<gameplay::ParticipantSpec> specs;
    std::array<std::unique_ptr<bots::BotController>, 4> bots;
    for (int s = 0; s < seats; ++s) {
        specs.push_back({s, gameplay::ParticipantKind::Bot});
        bots[static_cast<std::size_t>(s)] = makeBot(diff[static_cast<std::size_t>(s)], seed * 31u + static_cast<std::uint32_t>(s));
    }
    gameplay::Match m(c, specs, levels);
    for (int i = 0; i < 1200 * gameplay::geom::kTickRate && !m.over(); ++i) {
        std::array<int, 4> fire{};
        if (m.phase() == gameplay::MatchPhase::Playing)
            for (int s = 0; s < seats; ++s)
                if (bots[static_cast<std::size_t>(s)] && m.stationInRound(s))
                    fire[static_cast<std::size_t>(s)] =
                        bots[static_cast<std::size_t>(s)]->think(m.rotor().state(), m.field(s), m.roundTime(), m.roundTimeLeft(), m.tickCount() + 1);
        m.tick(fire);
        for (const auto& e : m.takeEvents())
            if (e.type == gameplay::MatchEventType::RoundStarted)
                for (auto& b : bots)
                    if (b) b->beginRound();
    }
    return {m.over() ? m.winnerStation() : -2, m.round()};
}

} // namespace

TEST_CASE(bots_load_every_profile_with_enforced_reaction) {
    for (auto d : {config::Difficulty::Easy, config::Difficulty::Normal, config::Difficulty::Hard, config::Difficulty::Expert}) {
        auto bot = makeBot(d, 1);
        REQUIRE(bot != nullptr);
        CHECK(bot->brain().profile().reaction > 0.0);
        CHECK(bot->delayTicks() > 0);
        CHECK(!bot->brain().disabled());
    }
    auto easy = makeBot(config::Difficulty::Easy, 1);
    auto expert = makeBot(config::Difficulty::Expert, 1);
    CHECK(easy->delayTicks() > expert->delayTicks());
}

TEST_CASE(bots_matches_end_with_exactly_one_winner_at_every_difficulty) {
    using D = config::Difficulty;
    for (D d : {D::Easy, D::Normal, D::Hard, D::Expert}) {
        const BotMatchResult r = playBots({d, d, d, d}, 11u + static_cast<std::uint32_t>(d));
        CHECK(r.winner >= 0 && r.winner < 4);
        CHECK(r.rounds >= 1);
    }
}

TEST_CASE(bots_better_profiles_survive_longer_on_average) {
    using D = config::Difficulty;
    int expertWins = 0;
    for (std::uint32_t seed = 1; seed <= 6; ++seed) {
        const BotMatchResult r = playBots({D::Easy, D::Expert, D::Easy, D::Easy}, seed);
        if (r.winner == 1) ++expertWins;
    }
    CHECK(expertWins >= 4);
}

TEST_CASE(bots_broken_script_is_rejected_or_disabled) {
    std::string err;
    CHECK(bots::BotBrain::create({"x = ", "test:syntax"}, config::Difficulty::Normal, 1, &err) == nullptr);
    CHECK(bots::BotBrain::create({"function decide() return true end", "test:noprofiles"}, config::Difficulty::Normal, 1, &err) == nullptr);
    CHECK(bots::BotBrain::create({"profiles = { normal = { reaction = 9 } } function decide() return true end", "test:reaction"},
                                 config::Difficulty::Normal, 1, &err) == nullptr);
    auto brain = bots::BotBrain::create({"profiles = { normal = { reaction = 0.1 } } function decide() while true do end end", "test:spin"},
                                        config::Difficulty::Normal, 1, &err);
    REQUIRE(brain != nullptr);
    bots::BotView v;
    for (int i = 0; i < 25; ++i) CHECK(!brain->decide(v));
    CHECK(brain->disabled());
}

namespace {

struct StallReport {
    int timeouts = 0;
    int rounds = 0;
    int claimed = 0;
    int ignitions = 0;
    int burnt = 0;
};

// Four bots of one difficulty play rounds [startRound, startRound + rounds) with plenty of tries.
StallReport playRounds(config::Difficulty d, int startRound, int rounds, std::uint32_t seed, bool powerups, bool fireMeter = false) {
    gameplay::LevelScript levels;
    gameplay::MatchConfig c;
    c.seed = seed * 7919u;
    c.tries = 9;
    c.startRound = startRound;
    if (powerups) c.powerups.assign(gameplay::kAllPowerups.begin(), gameplay::kAllPowerups.end());
    c.fireMeter = fireMeter;
    std::vector<gameplay::ParticipantSpec> specs;
    std::array<std::unique_ptr<bots::BotController>, 4> bots;
    for (int s = 0; s < 4; ++s) {
        specs.push_back({s, gameplay::ParticipantKind::Bot});
        bots[static_cast<std::size_t>(s)] = makeBot(d, seed * 31u + static_cast<std::uint32_t>(s));
    }
    gameplay::Match m(c, specs, levels);
    StallReport r;
    while (!m.over() && m.round() < startRound + rounds) {
        std::array<int, 4> fire{};
        if (m.phase() == gameplay::MatchPhase::Playing)
            for (int s = 0; s < 4; ++s)
                if (m.stationInRound(s))
                    fire[static_cast<std::size_t>(s)] = bots[static_cast<std::size_t>(s)]->think(
                        m.rotor().state(), m.field(s), m.roundTime(), m.roundTimeLeft(), m.tickCount() + 1);
        m.tick(fire);
        for (const auto& e : m.takeEvents()) {
            if (e.type == gameplay::MatchEventType::RoundStarted)
                for (auto& b : bots) b->beginRound();
            if (e.type == gameplay::MatchEventType::StationFailed && e.value == 1) ++r.timeouts;
            if (e.type == gameplay::MatchEventType::RoundStarted) ++r.rounds;
            if (e.type == gameplay::MatchEventType::PowerupClaimed) ++r.claimed;
            if (e.type == gameplay::MatchEventType::Ignited) ++r.ignitions;
            if (e.type == gameplay::MatchEventType::Burned) r.burnt += e.value;
        }
    }
    return r;
}

} // namespace

// Regression: on swinging rounds (8-12) the circle reverses before the biggest gap ever reaches the
// launcher. Bots used to wait for that gap until the round timer ran out; these seeds stalled before.
TEST_CASE(bots_never_stall_on_swinging_rounds) {
    using D = config::Difficulty;
    for (D d : {D::Easy, D::Normal, D::Hard, D::Expert}) {
        for (std::uint32_t seed = 1; seed <= 6; ++seed) {
            const StallReport r = playRounds(d, 8, 5, seed, false);
            if (r.timeouts != 0)
                test::fail(__FILE__, __LINE__, std::format("{} seed {}: {} station(s) timed out", config::difficultyName(d), seed, r.timeouts));
        }
    }
}

TEST_CASE(bots_take_powerup_rings) {
    int claimed = 0;
    for (std::uint32_t seed = 1; seed <= 3; ++seed) claimed += playRounds(config::Difficulty::Expert, 3, 6, seed, true).claimed;
    CHECK(claimed >= 3);
}

// With the fire meter on, good bots chain needles until they catch fire, mash while burning, and
// still never sit out a round.
TEST_CASE(bots_catch_fire_and_never_stall_with_the_fire_meter) {
    using D = config::Difficulty;
    int expertIgnitions = 0, expertBurnt = 0;
    for (D d : {D::Normal, D::Expert}) {
        for (std::uint32_t seed = 1; seed <= 3; ++seed) {
            const StallReport r = playRounds(d, 1, 12, seed, true, true);
            if (r.timeouts != 0)
                test::fail(__FILE__, __LINE__, std::format("{} seed {}: {} station(s) timed out", config::difficultyName(d), seed, r.timeouts));
            if (d == D::Expert) {
                expertIgnitions += r.ignitions;
                expertBurnt += r.burnt;
            }
        }
    }
    CHECK(expertIgnitions >= 3);
    CHECK(expertBurnt >= 1);
}
