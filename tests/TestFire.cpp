// Fire meter (combo, catching fire, burning needles), the heat / purge / swift rings, and the PNG check
// for the flame image.

#include <array>
#include <cmath>
#include <functional>
#include <string_view>

#include "Test.hpp"
#include "assets/EmbeddedAssets.hpp"
#include "assets/PngInfo.hpp"
#include "collision/Collision.hpp"
#include "config/Settings.hpp"
#include "gameplay/Geometry.hpp"
#include "gameplay/Match.hpp"
#include "gameplay/Playfield.hpp"

using namespace spindle::gameplay;
using spindle::collision::angularDistance;

namespace {

LevelDef constantLevel(int launch, double speed) {
    LevelDef d;
    d.launchCount = launch;
    d.rotation.initialSpeed = speed;
    d.rotation.segments = {{speed, 0.0, 60.0}};
    return d;
}

FieldConfig fireConfig(int combo = 4, int fireTicks = 120, int chainTicks = 60) {
    FieldConfig c = makeFieldConfig(1.0);
    c.fireEnabled = true;
    c.fireCombo = combo;
    c.fireTicks = fireTicks;
    c.chainWindowTicks = chainTicks;
    return c;
}

// A station and its rotor, stepped like Match::tick does, that can be resumed.
struct Sim {
    Playfield f;
    Rotor rotor;
    std::uint64_t tick = 0;
    std::vector<std::uint64_t> placedAt; // tick of each placement

    Sim(const LevelDef& level, const FieldConfig& cfg) : rotor(level.rotation) { f.begin(level, cfg); }

    void until(std::uint64_t last, const std::vector<std::uint64_t>& fireTicks = {}) {
        while (tick < last && f.status() == FieldStatus::Playing) {
            ++tick;
            const double before = rotor.state().angle;
            rotor.advance(geom::kTickSeconds);
            const double delta = spindle::collision::wrapAngle(rotor.state().angle - before);
            for (std::uint64_t t : fireTicks)
                if (t == tick) f.fire(tick);
            const int placed = f.placed();
            f.step(tick, geom::kTickSeconds, before, delta);
            for (int k = placed; k < f.placed(); ++k) placedAt.push_back(tick);
        }
    }
};

// Where a needle fired on `fireTick` lands on an empty circle.
double landingAngle(const LevelDef& level, std::uint64_t fireTick) {
    LevelDef one = level;
    one.prepins.clear();
    one.pickups.clear();
    one.launchCount = 1;
    Sim s(one, makeFieldConfig(1.0));
    s.until(fireTick + 120, {fireTick});
    return s.f.attached().empty() ? 0.0 : s.f.attached().back().angle;
}

std::uint64_t ticksFor(double radians, double speed) { return static_cast<std::uint64_t>(std::llround(radians / (speed * geom::kTickSeconds))); }

} // namespace

TEST_CASE(fire_meter_is_off_unless_enabled) {
    const MatchConfig c;
    CHECK(!c.fireMeter);
    Sim s(constantLevel(5, 2.0), makeFieldConfig(1.0));
    s.until(200, {10, 40, 70, 100});
    CHECK_EQ(s.f.placed(), 4);
    CHECK_EQ(s.f.combo(), 0);
    CHECK(!s.f.onFire(s.tick));
    CHECK_EQ(s.f.takeIgnitions(), 0);
}

TEST_CASE(fire_combo_chains_quick_needles_and_drops_when_too_slow) {
    const double speed = 2.0;
    const FieldConfig cfg = fireConfig(8, 120, 60);
    // 30 ticks apart at 2 rad/s = 0.25 rad, wider than a close call (1.4 x min separation).
    REQUIRE(0.25 > kCloseCallFactor * cfg.minSeparation);
    Sim s(constantLevel(10, speed), cfg);
    s.until(100, {10, 40, 70});
    CHECK_EQ(s.f.placed(), 3);
    CHECK_EQ(s.f.combo(), 3);
    CHECK(s.f.chainTicksLeft(s.tick) > 0);
    CHECK_NEAR(s.f.comboFill(), 3.0 / 8.0, 1e-12);
    // No needle for longer than the chain window: the combo is gone.
    s.until(s.placedAt.back() + 61);
    CHECK_EQ(s.f.combo(), 0);
    CHECK_EQ(s.f.chainTicksLeft(s.tick), 0);
    // A new needle starts a new chain at 1.
    const std::uint64_t next = s.tick + 5;
    s.until(next + 40, {next});
    CHECK_EQ(s.placedAt.size(), std::size_t{4});
    CHECK_EQ(s.f.combo(), 1);
}

TEST_CASE(fire_close_calls_count_double) {
    const double speed = 2.0;
    const FieldConfig cfg = fireConfig(8, 120, 60);
    // Second needle lands 1.2 x the minimum separation from the first: legal, and a close call.
    const std::uint64_t gap = ticksFor(1.2 * cfg.minSeparation, speed);
    const double angle = static_cast<double>(gap) * geom::kTickSeconds * speed;
    REQUIRE(angle > cfg.minSeparation && angle < kCloseCallFactor * cfg.minSeparation);
    Sim s(constantLevel(5, speed), cfg);
    s.until(80, {10, 10 + gap});
    CHECK(s.f.status() == FieldStatus::Playing);
    CHECK_EQ(s.f.placed(), 2);
    CHECK_EQ(s.f.combo(), 3); // 1 + 2
}

TEST_CASE(fire_full_meter_sets_the_station_on_fire_for_the_configured_time) {
    const FieldConfig cfg = fireConfig(4, 120, 60);
    Sim s(constantLevel(10, 2.0), cfg);
    s.until(130, {10, 40, 70, 100});
    REQUIRE((s.placedAt.size()) == std::size_t{4});
    CHECK_EQ(s.f.takeIgnitions(), 1);
    CHECK(s.f.onFire(s.tick));
    CHECK_EQ(s.f.fireStartTick(), s.placedAt.back());
    CHECK_EQ(s.f.fireUntilTick() - s.f.fireStartTick(), std::uint64_t{120});
    CHECK_EQ(s.f.combo(), 0); // the meter empties when it catches fire
    // Needles placed while on fire do not fill the meter again.
    s.until(s.f.fireStartTick() + 60, {s.tick + 2});
    CHECK_EQ(s.f.combo(), 0);
    s.until(s.f.fireUntilTick());
    CHECK(!s.f.onFire(s.tick));
    CHECK_EQ(s.f.takeIgnitions(), 0);
}

TEST_CASE(fire_needles_burn_through_and_normal_needles_still_collide) {
    const double speed = 1.0;
    LevelDef level = constantLevel(3, speed);
    level.pickups = {{landingAngle(level, 10), PowerupKind::Heat}};
    level.prepins = {landingAngle(level, 60)};
    const FieldConfig cfg = fireConfig(8, 120, 240);
    Sim s(level, cfg);
    s.until(40, {10});
    REQUIRE(s.f.onFire(s.tick)); // heat ring
    s.until(61, {60});
    REQUIRE((s.f.flying().size()) == std::size_t{1});
    CHECK(s.f.flying()[0].fire);
    s.until(120);
    CHECK(s.f.status() == FieldStatus::Playing); // landed right on the pre-placed needle
    CHECK_EQ(s.f.takeBurns(), 1);
    CHECK_EQ(s.f.attached().size(), std::size_t{2});
    for (const AttachedNeedle& a : s.f.attached()) CHECK(!a.preplaced);
    CHECK_EQ(s.f.burns().size(), std::size_t{1});
    // A full turn later, with the fire long out, a normal needle into the same spot fails.
    const std::uint64_t turn = ticksFor(geom::kTwoPi, speed);
    s.until(60 + turn + 60, {60 + turn});
    CHECK(s.f.status() == FieldStatus::Failed);
    CHECK(s.f.failReason() == FailReason::Collision);
}

TEST_CASE(fire_needle_launched_on_the_last_fire_tick_still_burns) {
    const double speed = 1.0;
    LevelDef level = constantLevel(3, speed);
    level.pickups = {{landingAngle(level, 10), PowerupKind::Heat}};
    Sim probe(level, fireConfig(8, 60, 240));
    probe.until(40, {10});
    REQUIRE(probe.f.onFire(probe.tick));
    const std::uint64_t last = probe.f.fireUntilTick() - 1;
    level.prepins = {landingAngle(level, last)};
    Sim s(level, fireConfig(8, 60, 240));
    s.until(last + 60, {10, last});
    CHECK(!s.f.onFire(s.tick));
    CHECK(s.f.status() == FieldStatus::Playing);
    CHECK_EQ(s.f.takeBurns(), 1);
}

TEST_CASE(fire_shield_touch_breaks_the_chain) {
    LevelDef level = constantLevel(4, 0.5);
    level.pickups = {{landingAngle(level, 10), PowerupKind::Shield}};
    const FieldConfig cfg = fireConfig(8, 120, 240);
    const auto gap = static_cast<std::uint64_t>(cfg.minFireIntervalTicks);
    Sim s(level, cfg);
    // Shot 1 takes the shield (combo 1); shot 2 right behind it touches it and bounces off.
    s.until(90, {10, 10 + gap});
    REQUIRE(s.placedAt.size() == std::size_t{1});
    CHECK_EQ(s.f.takeShieldBreaks(), 1);
    CHECK(s.f.status() == FieldStatus::Playing);
    CHECK_EQ(s.f.combo(), 0);
}

TEST_CASE(powerups_purge_burns_the_two_nearest_preplaced_needles) {
    LevelDef level = constantLevel(3, 1.0);
    const double ring = landingAngle(level, 10);
    level.pickups = {{ring, PowerupKind::Purge}};
    level.prepins = {ring + 0.4, ring - 0.5, ring + 2.0, ring + 3.0};
    Sim s(level, makeFieldConfig(1.0)); // purge works without the fire meter
    s.until(60, {10});
    REQUIRE(s.f.pickups()[0].claimed);
    CHECK_EQ(s.f.takeBurns(), 2);
    REQUIRE((s.f.attached().size()) == std::size_t{3});
    int pre = 0;
    for (const AttachedNeedle& a : s.f.attached()) {
        if (!a.preplaced) continue;
        ++pre;
        CHECK(angularDistance(a.angle, ring) > 1.5); // the far ones stay
    }
    CHECK_EQ(pre, 2);
}

TEST_CASE(powerups_purge_never_burns_the_players_own_needles) {
    LevelDef level = constantLevel(3, 1.0);
    level.pickups = {{landingAngle(level, 60), PowerupKind::Purge}};
    Sim s(level, makeFieldConfig(1.0));
    s.until(120, {10, 60});
    REQUIRE(s.f.pickups()[0].claimed);
    CHECK_EQ(s.f.takeBurns(), 0);
    CHECK_EQ(s.f.attached().size(), std::size_t{2});
}

TEST_CASE(powerups_swift_needles_fly_faster_for_the_rest_of_the_round) {
    LevelDef level = constantLevel(3, 1.0);
    level.pickups = {{landingAngle(level, 10), PowerupKind::Swift}};
    const FieldConfig cfg = makeFieldConfig(1.0);
    Sim s(level, cfg);
    s.until(200, {10, 100});
    REQUIRE((s.placedAt.size()) == std::size_t{2});
    CHECK(s.f.swift());
    CHECK_NEAR(s.f.needleSpeed(), cfg.needleSpeed * powerup::kSwiftFactor, 1e-9);
    const double normal = (cfg.launchDistance - cfg.needleLength) / cfg.needleSpeed * geom::kTickRate;
    const auto first = static_cast<double>(s.placedAt[0] - 10);
    const auto second = static_cast<double>(s.placedAt[1] - 100);
    CHECK(first >= normal - 1.0);                                 // flew at normal speed
    CHECK(second <= normal / powerup::kSwiftFactor + 1.0);        // flew swift
}

TEST_CASE(powerups_heat_sets_the_station_on_fire_only_with_the_meter) {
    LevelDef level = constantLevel(3, 1.0);
    level.pickups = {{landingAngle(level, 10), PowerupKind::Heat}};
    {
        Sim s(level, fireConfig(8, 120, 240));
        s.until(40, {10});
        CHECK(s.f.onFire(s.tick));
        CHECK_EQ(s.f.takeIgnitions(), 1);
    }
    {
        Sim s(level, makeFieldConfig(1.0));
        s.until(40, {10});
        CHECK(!s.f.onFire(s.tick));
        CHECK_EQ(s.f.takeIgnitions(), 0);
    }
}

namespace {

class FixedLevel final : public LevelProvider {
public:
    explicit FixedLevel(LevelDef level) : level_(std::move(level)) {}
    LevelDef level(int round, std::uint32_t, const std::vector<PowerupKind>& powerups) override {
        LevelDef d = level_;
        d.round = round;
        std::erase_if(d.pickups, [&](const Pickup& p) { return !contains(powerups, p.kind); });
        return d;
    }

private:
    LevelDef level_;
};

MatchConfig fireMatch(int tries) {
    MatchConfig c;
    c.tries = tries;
    c.readyDelay = 0.5;
    c.resolveDelay = 0.5;
    c.tally = scoring::TallyStyle::Off;
    c.roundTimeLimit = 20.0;
    c.seed = 5;
    c.powerups.assign(kAllPowerups.begin(), kAllPowerups.end());
    c.fireMeter = true;
    c.fireCombo = 8;
    c.fireSeconds = 0.5;
    c.fireChainWindow = 1.0;
    return c;
}

void drive(Match& m, const std::function<bool(int, int)>& plan, int maxTicks, const std::function<bool()>& until,
           std::vector<MatchEvent>* events = nullptr) {
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
        for (const MatchEvent& e : m.takeEvents())
            if (events) events->push_back(e);
    }
}

} // namespace

TEST_CASE(fire_match_heat_ring_needs_the_fire_meter) {
    LevelDef level = constantLevel(3, 1.0);
    level.pickups = {{1.0, PowerupKind::Heat}, {3.0, PowerupKind::Skip}};
    FixedLevel levels(level);
    MatchConfig off = fireMatch(3);
    off.fireMeter = false;
    Match a(off, {{0, ParticipantKind::Human}, {1, ParticipantKind::Human}}, levels);
    REQUIRE((a.field(0).pickups().size()) == std::size_t{1});
    CHECK(a.field(0).pickups()[0].pickup.kind == PowerupKind::Skip);
    CHECK(!contains(a.config().powerups, PowerupKind::Heat));
    CHECK(!a.field(0).config().fireEnabled);
    Match b(fireMatch(3), {{0, ParticipantKind::Human}, {1, ParticipantKind::Human}}, levels);
    CHECK_EQ(b.field(0).pickups().size(), std::size_t{2});
    CHECK(b.field(0).config().fireEnabled);
    CHECK_EQ(b.field(0).config().fireTicks, 120);
    CHECK_EQ(b.field(0).config().chainWindowTicks, 240);
}

TEST_CASE(fire_match_reports_ignition_and_burns) {
    // P1 takes a heat ring with its first needle and fires the second straight into it: it burns.
    LevelDef level = constantLevel(3, 1.0);
    level.pickups = {{landingAngle(level, 1), PowerupKind::Heat}};
    FixedLevel levels(level);
    Match m(fireMatch(3), {{0, ParticipantKind::Human}, {1, ParticipantKind::Human}}, levels);
    std::vector<MatchEvent> events;
    const auto gap = m.field(0).config().minFireIntervalTicks;
    drive(m, [&](int s, int t) { return s == 0 && (t == 1 || t == 40 || t == 40 + gap); }, 10 * 240,
          [&] { return m.phase() == MatchPhase::Playing && m.roundTime() > 2.0; }, &events);
    int ignited = 0, burnt = 0;
    for (const MatchEvent& e : events) {
        if (e.type == MatchEventType::Ignited && e.station == 0) ++ignited;
        if (e.type == MatchEventType::Burned && e.station == 0) burnt += e.value;
    }
    CHECK_EQ(ignited, 1);
    CHECK(burnt >= 1);
    CHECK(m.field(0).status() != FieldStatus::Failed);
    CHECK_EQ(m.participant(0)->ignitions, 1);
    CHECK_EQ(m.participant(0)->burnt, burnt);
}

TEST_CASE(fire_match_banks_the_combo_on_clear_and_drops_it_on_fail) {
    FixedLevel levels(constantLevel(3, 2.0));
    Match m(fireMatch(3), {{0, ParticipantKind::Human}, {1, ParticipantKind::Human}}, levels);
    const int gap = m.field(1).config().minFireIntervalTicks;
    // P1: three needles 30 ticks apart (combo 3, clear). P2: second needle right behind its first (fail).
    drive(m, [&](int s, int t) { return s == 0 ? (t == 1 || t == 31 || t == 61) : (t == 1 || t == 1 + gap); }, 20 * 240,
          [&] { return m.round() == 2; });
    REQUIRE((m.round()) == 2);
    CHECK_EQ(m.participant(0)->carriedCombo, 3);
    CHECK_EQ(m.participant(1)->carriedCombo, 0);
    CHECK_EQ(m.field(0).combo(), 3);
    CHECK_EQ(m.field(1).combo(), 0);
    // The banked chain still has to be kept: the first needle of round 2 must come within the window.
    drive(m, [](int, int) { return false; }, 10 * 240, [&] { return m.phase() == MatchPhase::Playing && m.roundTime() > 1.1; });
    CHECK_EQ(m.field(0).combo(), 0);
}

TEST_CASE(fire_no_fire_in_sudden_death) {
    FixedLevel levels(constantLevel(2, 1.0));
    Match m(fireMatch(1), {{0, ParticipantKind::Human}, {1, ParticipantKind::Human}}, levels);
    CHECK(m.field(0).config().fireEnabled);
    // Identical play: both fire into their own first needle on the same tick -> T4 sudden death.
    const int gap = m.field(0).config().minFireIntervalTicks;
    drive(m, [&](int, int t) { return t == 1 || t == 1 + gap; }, 20 * 240, [&] { return m.suddenDeath() && m.phase() == MatchPhase::Ready; });
    REQUIRE(m.suddenDeath());
    CHECK(!m.field(0).config().fireEnabled);
    CHECK(!m.field(1).config().fireEnabled);
    CHECK_EQ(m.field(0).combo(), 0);
}

TEST_CASE(png_header_of_the_embedded_flame) {
    const std::string_view digits = "123456789";
    CHECK_EQ(spindle::assets::crc32(std::span(reinterpret_cast<const unsigned char*>(digits.data()), digits.size())),
             std::uint32_t{0xCBF43926u});
    const auto png = spindle::assets::findEmbedded("art/flame.png");
    REQUIRE(png.has_value());
    const auto info = spindle::assets::readPngInfo(*png);
    REQUIRE(info.has_value());
    CHECK_EQ(info->width, std::uint32_t{128});
    CHECK_EQ(info->height, std::uint32_t{256});
    CHECK_EQ(static_cast<int>(info->bitDepth), 8);
    CHECK_EQ(static_cast<int>(info->colorType), 6);
    std::vector<unsigned char> bad(png->begin(), png->end());
    bad[20] ^= 0x01; // height byte: the IHDR CRC no longer matches
    CHECK(!spindle::assets::readPngInfo(bad));
    CHECK(!spindle::assets::readPngInfo(png->first(20)));
    std::vector<unsigned char> notPng(png->begin(), png->end());
    notPng[1] = 'X';
    CHECK(!spindle::assets::readPngInfo(notPng));
}

TEST_CASE(settings_fire_meter_keys) {
    using namespace spindle::config;
    const Settings d = Settings::defaults();
    CHECK(d.gameplay.fireMeter);
    CHECK_EQ(d.gameplay.fireCombo, 8);
    CHECK_NEAR(d.gameplay.fireSeconds, 1.5, 1e-12);
    CHECK_NEAR(d.gameplay.fireChainWindow, 1.0, 1e-12);
    CHECK_EQ(d.gameplay.powerupTypes, std::string("shield, skip, slim, mend, heat, purge, swift"));
    auto ok = loadSettings(IniDocument::parse("[Gameplay]\nfire_meter = 0\nfire_combo = 12\nfire_seconds = 2.5\nfire_chain_window = 0.75\n"));
    CHECK(!ok.settings.gameplay.fireMeter);
    CHECK_EQ(ok.settings.gameplay.fireCombo, 12);
    CHECK_NEAR(ok.settings.gameplay.fireSeconds, 2.5, 1e-12);
    CHECK_NEAR(ok.settings.gameplay.fireChainWindow, 0.75, 1e-12);
    CHECK_EQ(ok.invalidCount(), std::size_t{0});
    auto bad = loadSettings(IniDocument::parse("[Gameplay]\nfire_combo = 99\nfire_seconds = 0\npowerup_types = heat, lava\n"));
    CHECK_EQ(bad.settings.gameplay.fireCombo, 8);
    CHECK_NEAR(bad.settings.gameplay.fireSeconds, 1.5, 1e-12);
    CHECK_EQ(bad.invalidCount(), std::size_t{3});
}
