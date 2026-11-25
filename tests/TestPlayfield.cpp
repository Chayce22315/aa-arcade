#include <cmath>

#include "Test.hpp"
#include "collision/Collision.hpp"
#include "gameplay/Geometry.hpp"
#include "gameplay/Playfield.hpp"
#include "gameplay/Rotor.hpp"

using namespace spindle::gameplay;

namespace {

LevelDef simpleLevel(int launch, double speed, std::vector<double> prepins = {}) {
    LevelDef d;
    d.launchCount = launch;
    d.prepins = std::move(prepins);
    d.rotation.initialSpeed = speed;
    d.rotation.segments = {{speed, 0.0, 30.0}};
    return d;
}

// Runs one field with a shared rotor; fires when fireAt(tick) says so. Returns the tick count used.
template <class F>
int run(Playfield& f, Rotor& rotor, F fireAt, int maxTicks = 20 * geom::kTickRate) {
    std::uint64_t tick = 0;
    for (int i = 0; i < maxTicks && f.status() == FieldStatus::Playing; ++i) {
        ++tick;
        const double before = rotor.state().angle;
        rotor.advance(geom::kTickSeconds);
        const double delta = spindle::collision::wrapAngle(rotor.state().angle - before);
        if (fireAt(tick)) f.fire(tick);
        f.step(tick, geom::kTickSeconds, before, delta);
    }
    return static_cast<int>(tick);
}

} // namespace

TEST_CASE(playfield_spaced_shots_clear_the_round) {
    const LevelDef level = simpleLevel(6, 2.0);
    Rotor rotor(level.rotation);
    Playfield f;
    f.begin(level, makeFieldConfig(1.0));
    // 2 rad/s, one shot every 0.5 s -> needles 1 rad apart.
    run(f, rotor, [](std::uint64_t t) { return t % 120 == 1; });
    CHECK(f.status() == FieldStatus::Cleared);
    CHECK_EQ(f.placed(), 6);
    CHECK_EQ(f.needlesLeft(), 0);
    CHECK_EQ(f.attached().size(), std::size_t{6});
}

TEST_CASE(playfield_rapid_fire_collides) {
    const LevelDef level = simpleLevel(6, 0.5);
    Rotor rotor(level.rotation);
    Playfield f;
    const FieldConfig cfg = makeFieldConfig(1.0);
    f.begin(level, cfg);
    run(f, rotor, [](std::uint64_t) { return true; });
    CHECK(f.status() == FieldStatus::Failed);
    CHECK(f.failReason() == FailReason::Collision);
    CHECK(f.conflictAttached().has_value());
    CHECK(f.placed() >= 1);
}

TEST_CASE(playfield_fire_interval_enforced) {
    const LevelDef level = simpleLevel(10, 2.0);
    Playfield f;
    const FieldConfig cfg = makeFieldConfig(1.0);
    f.begin(level, cfg);
    CHECK(f.fire(1));
    CHECK(!f.fire(1));
    CHECK(!f.fire(static_cast<std::uint64_t>(cfg.minFireIntervalTicks)));
    CHECK(f.fire(1 + static_cast<std::uint64_t>(cfg.minFireIntervalTicks)));
    CHECK_EQ(f.inFlight(), 2);
    // Balls in flight never overlap: launch spacing >= one ball diameter.
    CHECK(cfg.minFireIntervalTicks * geom::kTickSeconds * cfg.needleSpeed >= 2 * cfg.ballRadius - 1e-9);
}

TEST_CASE(playfield_hits_preplaced_needle_at_bottom) {
    // A pre-placed needle sitting at the attach point, circle stopped: the first shot must fail.
    LevelDef level = simpleLevel(3, 0.0, {geom::kAttachAngle});
    level.rotation.segments = {{0.0, 0.0, 30.0}};
    Rotor rotor(level.rotation);
    Playfield f;
    f.begin(level, makeFieldConfig(1.0));
    run(f, rotor, [](std::uint64_t t) { return t == 1; }, geom::kTickRate);
    CHECK(f.status() == FieldStatus::Failed);
    CHECK_EQ(f.placed(), 0);
}

TEST_CASE(playfield_timeout_and_idle) {
    Playfield f;
    CHECK(f.status() == FieldStatus::Idle);
    CHECK(!f.fire(1));
    f.begin(simpleLevel(3, 1.0), makeFieldConfig(1.0));
    f.failTimeout(500);
    CHECK(f.status() == FieldStatus::Failed);
    CHECK(f.failReason() == FailReason::Timeout);
    CHECK_EQ(f.failTick(), std::uint64_t{500});
}

TEST_CASE(playfield_stations_are_independent_with_shared_rotor) {
    // Four fields, one rotor. Station 2 fires too fast and fails; the others are unaffected.
    const LevelDef level = simpleLevel(5, 2.0);
    Rotor rotor(level.rotation);
    std::array<Playfield, 4> fields;
    for (auto& f : fields) f.begin(level, makeFieldConfig(1.0));
    std::uint64_t tick = 0;
    for (int i = 0; i < 10 * geom::kTickRate; ++i) {
        ++tick;
        const double before = rotor.state().angle;
        rotor.advance(geom::kTickSeconds);
        const double delta = spindle::collision::wrapAngle(rotor.state().angle - before);
        for (std::size_t s = 0; s < 4; ++s) {
            const bool fire = (s == 2) ? true : (tick % 120 == 1 + s * 7);
            if (fire) fields[s].fire(tick);
            fields[s].step(tick, geom::kTickSeconds, before, delta);
        }
    }
    CHECK(fields[2].status() == FieldStatus::Failed);
    CHECK(fields[0].status() == FieldStatus::Cleared);
    CHECK(fields[1].status() == FieldStatus::Cleared);
    CHECK(fields[3].status() == FieldStatus::Cleared);
    // Same rotor: station 1 fires 7 ticks after station 0, so its first needle sits 7 ticks of rotation
    // (2 rad/s) further back, within one tick of quantization.
    const double diff = spindle::collision::wrapAngle(fields[0].attached()[0].angle - fields[1].attached()[0].angle);
    CHECK_NEAR(diff, 2.0 * 7.0 / geom::kTickRate, 2.0 / geom::kTickRate + 1e-9);
}

TEST_CASE(playfield_landing_angle_matches_rotation_during_flight) {
    const double speed = 1.0;
    const LevelDef level = simpleLevel(1, speed);
    Rotor rotor(level.rotation);
    Playfield f;
    const FieldConfig cfg = makeFieldConfig(1.0);
    f.begin(level, cfg);
    run(f, rotor, [](std::uint64_t t) { return t == 1; });
    REQUIRE(f.status() == FieldStatus::Cleared);
    // Launched at tick 1, travels (launch - L) at needleSpeed; attaches where the bottom is at that moment.
    const double flight = (cfg.launchDistance - cfg.needleLength) / cfg.needleSpeed;
    const double expectedRotor = speed * (geom::kTickSeconds + flight);
    const double expected = spindle::collision::wrapAngle(geom::kAttachAngle - expectedRotor);
    CHECK_NEAR(spindle::collision::angularDistance(f.attached()[0].angle, expected), 0.0, speed * geom::kTickSeconds + 1e-6);
}
