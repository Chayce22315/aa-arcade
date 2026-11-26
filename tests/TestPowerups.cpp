// Powerups: parsing, the four effects on a station, mend and sudden death in a match, level rings.

#include <array>
#include <cmath>
#include <functional>

#include "Test.hpp"
#include "collision/Collision.hpp"
#include "gameplay/Geometry.hpp"
#include "gameplay/LevelScript.hpp"
#include "gameplay/Match.hpp"
#include "gameplay/Playfield.hpp"
#include "scripting/LuaSandbox.hpp"

using namespace spindle::gameplay;
using spindle::collision::angularDistance;

namespace {

LevelDef constantLevel(int launch, double speed) {
    LevelDef d;
    d.launchCount = launch;
    d.rotation.initialSpeed = speed;
    d.rotation.segments = {{speed, 0.0, 30.0}};
    return d;
}

// Steps a field with a constant rotor; fires on the listed ticks. Same order as Match::tick.
void run(Playfield& f, Rotor& rotor, const std::vector<std::uint64_t>& fireTicks, int ticks) {
    for (std::uint64_t tick = 1; tick <= static_cast<std::uint64_t>(ticks) && f.status() == FieldStatus::Playing; ++tick) {
        const double before = rotor.state().angle;
        rotor.advance(geom::kTickSeconds);
        const double delta = spindle::collision::wrapAngle(rotor.state().angle - before);
        for (std::uint64_t t : fireTicks)
            if (t == tick) f.fire(tick);
        f.step(tick, geom::kTickSeconds, before, delta);
    }
}

// Where a needle fired on tick `fireTick` lands (dry run without rings).
double landingAngle(const LevelDef& level, std::uint64_t fireTick) {
    Rotor rotor(level.rotation);
    Playfield f;
    LevelDef one = level;
    one.pickups.clear();
    one.launchCount = 1;
    f.begin(one, makeFieldConfig(1.0));
    run(f, rotor, {fireTick}, 240);
    return f.attached().empty() ? 0.0 : f.attached().back().angle;
}

LevelDef withRing(LevelDef level, PowerupKind kind, std::uint64_t fireTick) {
    level.pickups = {{landingAngle(level, fireTick), kind}};
    return level;
}

} // namespace

TEST_CASE(powerups_parse_and_format) {
    std::vector<std::string> unknown;
    auto kinds = parsePowerupList(" Mend, shield,shield , bogus", &unknown);
    CHECK_EQ(kinds.size(), std::size_t{2});
    CHECK(kinds[0] == PowerupKind::Shield && kinds[1] == PowerupKind::Mend); // canonical order
    CHECK_EQ(unknown.size(), std::size_t{1});
    CHECK_EQ(formatPowerupList(kinds), std::string("shield, mend"));
    CHECK(parsePowerupList("none").empty());
    CHECK_EQ(formatPowerupList({}), std::string("none"));
    CHECK(parsePowerup("SLIM") == PowerupKind::Slim);
    CHECK(!parsePowerup("speed"));
}

TEST_CASE(powerups_ring_is_taken_only_by_a_needle_landing_on_it) {
    const LevelDef base = constantLevel(3, 1.0);
    const LevelDef level = withRing(base, PowerupKind::Skip, 10);
    {
        Rotor rotor(level.rotation);
        Playfield f;
        f.begin(level, makeFieldConfig(1.0));
        run(f, rotor, {10}, 60);
        CHECK(f.pickups()[0].claimed);
        CHECK(f.lastClaim() == PowerupKind::Skip);
        CHECK_EQ(f.takeClaims().size(), std::size_t{1});
        CHECK(f.takeClaims().empty());
    }
    {
        // A needle landing a full claim window away leaves the ring alone (1 rad/s for 0.25 s = 14 deg).
        Rotor rotor(level.rotation);
        Playfield f;
        f.begin(level, makeFieldConfig(1.0));
        run(f, rotor, {70}, 120);
        CHECK(!f.pickups()[0].claimed);
        CHECK(angularDistance(f.attached()[0].angle, f.pickups()[0].pickup.angle) > f.claimAngle());
    }
}

TEST_CASE(powerups_shield_bounces_one_touch_and_requeues_the_needle) {
    const LevelDef level = withRing(constantLevel(4, 0.5), PowerupKind::Shield, 10);
    Rotor rotor(level.rotation);
    Playfield f;
    const FieldConfig cfg = makeFieldConfig(1.0);
    f.begin(level, cfg);
    const auto gap = static_cast<std::uint64_t>(cfg.minFireIntervalTicks);
    // Shot 1 takes the shield; shot 2 right behind it would touch it.
    run(f, rotor, {10, 10 + gap}, 10 + static_cast<int>(gap) + 60);
    CHECK(f.status() == FieldStatus::Playing);
    CHECK(!f.shieldArmed());
    CHECK_EQ(f.takeShieldBreaks(), 1);
    CHECK_EQ(f.placed(), 1);
    CHECK_EQ(f.needlesLeft(), 3); // 4 - 2 launched + 1 bounced back
    // Without the shield the same touch fails the round.
    const LevelDef plain = constantLevel(4, 0.5);
    Rotor rotor2(plain.rotation);
    Playfield g;
    g.begin(plain, cfg);
    run(g, rotor2, {10, 10 + gap}, 10 + static_cast<int>(gap) + 60);
    CHECK(g.status() == FieldStatus::Failed);
}

TEST_CASE(powerups_skip_removes_a_needle_and_can_clear_the_round) {
    const LevelDef level = withRing(constantLevel(2, 1.0), PowerupKind::Skip, 10);
    Rotor rotor(level.rotation);
    Playfield f;
    f.begin(level, makeFieldConfig(1.0));
    run(f, rotor, {10}, 120);
    CHECK(f.status() == FieldStatus::Cleared); // 2 needles: one placed, one skipped
    CHECK_EQ(f.placed(), 1);
    CHECK_EQ(f.needlesLeft(), 0);
}

TEST_CASE(powerups_slim_shrinks_balls_so_tighter_gaps_are_legal) {
    const double speed = 1.0;
    const LevelDef level = withRing(constantLevel(3, speed), PowerupKind::Slim, 10);
    const FieldConfig cfg = makeFieldConfig(1.0);
    const double slimSep = spindle::collision::minSeparation(cfg.needleLength, cfg.ballRadius * powerup::kSlimFactor);
    CHECK(slimSep < cfg.minSeparation);
    // Second shot lands halfway between the slim and the normal separation from the first.
    const double gapAngle = 0.5 * (slimSep + cfg.minSeparation);
    const auto later = 10 + static_cast<std::uint64_t>(std::llround(gapAngle / (speed * geom::kTickSeconds)));
    {
        Rotor rotor(level.rotation);
        Playfield f;
        f.begin(level, cfg);
        run(f, rotor, {10, later}, static_cast<int>(later) + 60);
        CHECK(f.slim());
        CHECK_NEAR(f.ballRadius(), cfg.ballRadius * powerup::kSlimFactor, 1e-12);
        CHECK(f.status() == FieldStatus::Playing);
        CHECK_EQ(f.placed(), 2);
    }
    {
        const LevelDef plain = constantLevel(3, speed);
        Rotor rotor(plain.rotation);
        Playfield f;
        f.begin(plain, cfg);
        run(f, rotor, {10, later}, static_cast<int>(later) + 60);
        CHECK(f.status() == FieldStatus::Failed);
    }
}

namespace {

class RingLevels final : public LevelProvider {
public:
    explicit RingLevels(LevelDef level) : level_(std::move(level)) {}
    LevelDef level(int round, std::uint32_t, const std::vector<PowerupKind>& powerups) override {
        LevelDef d = level_;
        d.round = round;
        std::erase_if(d.pickups, [&](const Pickup& p) { return !contains(powerups, p.kind); });
        ++calls;
        return d;
    }
    int calls = 0;

private:
    LevelDef level_;
};

MatchConfig ringConfig(int tries) {
    MatchConfig c;
    c.tries = tries;
    c.readyDelay = 0.5;
    c.resolveDelay = 0.5;
    c.tally = scoring::TallyStyle::Off;
    c.roundTimeLimit = 20.0;
    c.seed = 3;
    c.powerups.assign(kAllPowerups.begin(), kAllPowerups.end());
    return c;
}

// Drives a match: plan(station, ticksIntoRound) says whether to press fire.
void drive(Match& m, const std::function<bool(int, int)>& plan, int maxTicks, const std::function<bool()>& until) {
    int intoRound = 0;
    for (int i = 0; i < maxTicks && !m.over() && !until(); ++i) {
        std::array<int, 4> fire{};
        if (m.phase() == MatchPhase::Playing) {
            ++intoRound;
            for (int s = 0; s < 4; ++s)
                if (m.stationInRound(s) && plan(s, intoRound)) fire[static_cast<std::size_t>(s)] = 1;
        } else {
            intoRound = 0;
        }
        m.tick(fire);
    }
}

} // namespace

TEST_CASE(powerups_mend_gives_back_a_lost_try_but_never_more) {
    // Two needles per round; a mend ring sits exactly where a needle fired on the first playing tick lands.
    RingLevels levels(withRing(constantLevel(2, 1.0), PowerupKind::Mend, 1));
    Match m(ringConfig(3), {{0, ParticipantKind::Human}, {1, ParticipantKind::Human}}, levels);
    int gained = 0;
    // Round 1: P1 takes the mend at full tries (no effect), then fires straight into its own needle.
    // P2 takes it too and then places its second needle safely.
    drive(m, [](int s, int t) { return t == 1 || (s == 0 ? t == 8 : t == 120); }, 20 * 240, [&] { return m.round() == 2; });
    for (const auto& e : m.takeEvents()) gained += e.type == MatchEventType::TryGained;
    CHECK_EQ(gained, 0);
    CHECK_EQ(m.participant(0)->tries, 2);
    CHECK_EQ(m.participant(1)->tries, 3);
    CHECK_EQ(m.participant(0)->powerupsClaimed, 1);
    // Round 2: both take the mend again; only P1 is missing a try.
    drive(m, [](int, int t) { return t == 1 || t == 120; }, 20 * 240, [&] { return m.round() == 3; });
    for (const auto& e : m.takeEvents()) gained += e.type == MatchEventType::TryGained;
    CHECK_EQ(gained, 1);
    CHECK_EQ(m.participant(0)->tries, 3);
    CHECK_EQ(m.participant(1)->tries, 3);
}

TEST_CASE(powerups_no_rings_in_sudden_death_or_when_disabled) {
    RingLevels levels(withRing(constantLevel(2, 1.0), PowerupKind::Shield, 1));
    {
        MatchConfig off = ringConfig(1);
        off.powerups.clear();
        Match m(off, {{0, ParticipantKind::Human}, {1, ParticipantKind::Human}}, levels);
        CHECK(m.field(0).pickups().empty());
    }
    {
        MatchConfig onlySkip = ringConfig(1);
        onlySkip.powerups = {PowerupKind::Skip};
        Match m(onlySkip, {{0, ParticipantKind::Human}, {1, ParticipantKind::Human}}, levels);
        CHECK(m.field(0).pickups().empty()); // the level's shield ring is not enabled
    }
    // Identical play by both: shield bounces the first touch for both, then both fail together -> T4.
    Match m(ringConfig(1), {{0, ParticipantKind::Human}, {1, ParticipantKind::Human}}, levels);
    CHECK_EQ(m.field(0).pickups().size(), std::size_t{1});
    drive(m, [](int, int t) { return t % 6 == 1; }, 30 * 240, [&] { return m.suddenDeath() && m.phase() == MatchPhase::Ready; });
    REQUIRE(m.suddenDeath());
    CHECK(m.field(0).pickups().empty());
    CHECK(m.field(1).pickups().empty());
}

TEST_CASE(powerups_level_script_places_valid_rings) {
    std::string err;
    auto sandbox = LevelScript::load(LevelScript::embeddedSource(), &err);
    REQUIRE(sandbox != nullptr);
    LevelLimits limits = defaultLimits();
    limits.powerups.assign(kAllPowerups.begin(), kAllPowerups.end());
    int rings = 0;
    for (std::uint32_t seed : {2u, 9u}) {
        for (int round = 1; round <= 300; ++round) {
            auto def = LevelScript::generate(*sandbox, round, seed, limits, &err);
            if (!def) {
                test::fail(__FILE__, __LINE__, std::format("round {} seed {}: {}", round, seed, err));
                return;
            }
            if (round < 3) CHECK(def->pickups.empty());
            int mends = 0, heats = 0;
            for (const auto& p : def->pickups) {
                mends += p.kind == PowerupKind::Mend;
                heats += p.kind == PowerupKind::Heat;
            }
            CHECK(mends <= 1);
            CHECK(heats <= 1);
            rings += static_cast<int>(def->pickups.size());
        }
    }
    CHECK(rings > 400);
    limits.powerups = {PowerupKind::Slim};
    for (int round = 3; round <= 40; ++round) {
        auto def = LevelScript::generate(*sandbox, round, 5, limits, &err);
        REQUIRE(def.has_value());
        for (const auto& p : def->pickups) CHECK(p.kind == PowerupKind::Slim);
    }
    limits.powerups.clear();
    auto none = LevelScript::generate(*sandbox, 20, 5, limits, &err);
    REQUIRE(none.has_value());
    CHECK(none->pickups.empty());
}

TEST_CASE(powerups_level_validation_rejects_bad_rings) {
    LevelLimits limits = defaultLimits();
    limits.powerups = {PowerupKind::Shield};
    LevelDef d = constantLevel(6, 1.5);
    d.prepins = {0.0};
    d.pickups = {{2.0, PowerupKind::Shield}};
    CHECK(!validateLevel(d, limits).has_value());
    LevelDef disabled = d;
    disabled.pickups = {{2.0, PowerupKind::Mend}};
    CHECK(validateLevel(disabled, limits).has_value());
    LevelDef onPin = d;
    onPin.pickups = {{0.05, PowerupKind::Shield}};
    CHECK(validateLevel(onPin, limits).has_value());
    LevelDef crowded = d;
    crowded.pickups = {{2.0, PowerupKind::Shield}, {2.05, PowerupKind::Shield}};
    CHECK(validateLevel(crowded, limits).has_value());
    LevelDef many = d;
    many.pickups = {{1.0, PowerupKind::Shield}, {2.0, PowerupKind::Shield}, {3.0, PowerupKind::Shield}, {4.0, PowerupKind::Shield}};
    CHECK(validateLevel(many, limits).has_value());
}

TEST_CASE(powerups_gold_doubles_the_needles_after_it_for_a_few_seconds) {
    const LevelDef level = withRing(constantLevel(3, 1.0), PowerupKind::Gold, 10);
    Rotor rotor(level.rotation);
    Playfield f;
    f.begin(level, makeFieldConfig(1.0));
    run(f, rotor, {10, 250}, 400); // the second needle lands about a second after the first
    REQUIRE(f.status() == FieldStatus::Playing);
    const auto placements = f.takePlacements();
    REQUIRE(placements.size() == std::size_t{2});
    CHECK(!placements[0].gold); // the needle that takes the ring is scored before it takes it
    CHECK(placements[1].gold);
    const std::uint64_t until = f.goldUntilTick();
    CHECK(f.gold(until - 1) && !f.gold(until));
    scoring::Placement plain;
    scoring::Placement golden;
    golden.gold = true;
    CHECK_EQ(scoring::placementPoints(golden, 3), 2 * scoring::placementPoints(plain, 3));
    golden.onFire = true;
    plain.onFire = true;
    CHECK_EQ(scoring::placementPoints(golden, 3), 2 * scoring::placementPoints(plain, 3)); // stacks with fire
}

TEST_CASE(powerups_ghost_needle_sticks_through_a_touch_then_is_ordinary) {
    const double speed = 1.0;
    const LevelDef level = withRing(constantLevel(3, speed), PowerupKind::Ghost, 10);
    const auto rev = static_cast<std::uint64_t>(std::llround(2.0 * geom::kPi / (speed * geom::kTickSeconds)));
    const FieldConfig cfg = makeFieldConfig(1.0);
    {
        // Shot 2 comes round to the very spot of shot 1: as a ghost it sticks there anyway.
        Rotor rotor(level.rotation);
        Playfield f;
        f.begin(level, cfg);
        run(f, rotor, {10, 10 + rev}, static_cast<int>(10 + rev + 240));
        CHECK(f.status() == FieldStatus::Playing);
        CHECK_EQ(f.placed(), 2);
        CHECK(!f.ghostArmed());
        REQUIRE(f.attached().size() == std::size_t{2});
        CHECK(angularDistance(f.attached()[0].angle, f.attached()[1].angle) < cfg.minSeparation);
        const auto placements = f.takePlacements();
        REQUIRE(placements.size() == std::size_t{2});
        CHECK(!placements[1].closeCall); // overlapping is not a close call
    }
    {
        // Shot 3 at the same spot is an ordinary needle again: it touches.
        Rotor rotor(level.rotation);
        Playfield f;
        f.begin(level, cfg);
        run(f, rotor, {10, 10 + rev, 10 + 2 * rev}, static_cast<int>(10 + 2 * rev + 240));
        CHECK(f.status() == FieldStatus::Failed);
        CHECK_EQ(f.placed(), 2);
    }
    {
        // Without the ring, shot 2 already touches.
        const LevelDef plain = constantLevel(3, speed);
        Rotor rotor(plain.rotation);
        Playfield f;
        f.begin(plain, cfg);
        run(f, rotor, {10, 10 + rev}, static_cast<int>(10 + rev + 240));
        CHECK(f.status() == FieldStatus::Failed);
    }
}
