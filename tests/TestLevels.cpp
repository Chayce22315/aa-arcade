#include <numbers>

#include "Test.hpp"
#include "gameplay/LevelScript.hpp"
#include "scripting/LuaSandbox.hpp"

extern "C" {
#include "lua.h"
}

using namespace spindle::gameplay;

TEST_CASE(levels_embedded_script_is_feasible_for_rounds_1_to_500) {
    std::string err;
    auto sandbox = LevelScript::load(LevelScript::embeddedSource(), &err);
    REQUIRE(sandbox != nullptr);
    const LevelLimits limits = defaultLimits();
    for (std::uint32_t seed : {1u, 77u, 4242u}) {
        for (int round = 1; round <= 500; ++round) {
            auto def = LevelScript::generate(*sandbox, round, seed, limits, &err);
            if (!def) {
                test::fail(__FILE__, __LINE__, std::format("round {} seed {}: {}", round, seed, err));
                return;
            }
            CHECK_EQ(def->round, round);
        }
    }
}

TEST_CASE(levels_same_seed_same_round_identical) {
    LevelScript a, b;
    LevelDef x = a.level(14, 99);
    LevelDef y = b.level(14, 99);
    CHECK_EQ(x.launchCount, y.launchCount);
    CHECK_EQ(x.prepins.size(), y.prepins.size());
    for (std::size_t i = 0; i < x.prepins.size() && i < y.prepins.size(); ++i) CHECK_EQ(x.prepins[i], y.prepins[i]);
    CHECK_EQ(x.rotation.segments.size(), y.rotation.segments.size());
    LevelDef z = a.level(14, 100);
    CHECK(z.prepins != x.prepins || z.rotation.segments.size() != x.rotation.segments.size() ||
          z.rotation.initialSpeed != x.rotation.initialSpeed);
}

TEST_CASE(levels_difficulty_grows) {
    LevelScript s;
    LevelDef early = s.level(1, 5);
    LevelDef late = s.level(30, 5);
    CHECK(late.launchCount > early.launchCount);
    CHECK(late.prepins.size() > early.prepins.size());
}

TEST_CASE(levels_validation_rejects_bad_levels) {
    const LevelLimits limits = defaultLimits();
    LevelDef ok;
    ok.launchCount = 8;
    ok.rotation.initialSpeed = 1.5;
    ok.rotation.segments = {{1.5, 0.0, 10.0}};
    CHECK(!validateLevel(ok, limits).has_value());

    LevelDef crowded = ok;
    crowded.launchCount = 40;
    CHECK(validateLevel(crowded, limits).has_value());

    LevelDef close = ok;
    close.prepins = {0.0, limits.minSeparation};
    CHECK(validateLevel(close, limits).has_value());

    LevelDef still = ok;
    still.rotation.initialSpeed = 0.0;
    still.rotation.segments = {{0.0, 0.0, 5.0}};
    CHECK(validateLevel(still, limits).has_value());

    LevelDef nan = ok;
    nan.rotation.segments = {{std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0}};
    CHECK(validateLevel(nan, limits).has_value());

    LevelDef blink = ok;
    blink.rotation.segments = {{1.0, 0.0, 0.01}};
    CHECK(validateLevel(blink, limits).has_value());
}

TEST_CASE(levels_broken_override_falls_back_to_embedded) {
    LevelScript bad(ScriptSource{"function level() return { launch = 99 } end", "test:bad.lua"});
    CHECK(bad.usingOverride());
    LevelDef def = bad.level(3, 1);
    CHECK(!validateLevel(def, bad.limits()).has_value());
    LevelScript syntax(ScriptSource{"this is not lua", "test:syntax.lua"});
    CHECK(!syntax.usingOverride());
    CHECK(!validateLevel(syntax.level(3, 1), syntax.limits()).has_value());
}

TEST_CASE(lua_sandbox_has_no_io_and_enforces_budget_and_memory) {
    spindle::scripting::LuaSandbox sb("test", 1024 * 1024);
    std::string err;
    CHECK(sb.run("assert(io == nil and os == nil and package == nil and debug == nil)", "t1", &err));
    CHECK(sb.run("assert(load == nil and dofile == nil and loadfile == nil and require == nil)", "t2", &err));
    CHECK(sb.run("assert(string.dump == nil and math.random(1, 6) >= 1)", "t3", &err));
    sb.setInstructionBudget(100000);
    CHECK(!sb.run("while true do end", "spin", &err));
    CHECK(err.find("budget") != std::string::npos);
    sb.setInstructionBudget(50'000'000);
    CHECK(!sb.run("local t = {} for i = 1, 1e7 do t[i] = string.rep('x', 100) .. i end", "hog", &err));
    CHECK(sb.run("x = 1", "after", &err)); // state still usable
}

TEST_CASE(lua_sandbox_random_is_seeded_and_deterministic) {
    spindle::scripting::LuaSandbox a("a"), b("b");
    a.seedRandom(123);
    b.seedRandom(123);
    std::string err;
    CHECK(a.run("v = math.random(1, 1000000) + math.random()", "a", &err));
    CHECK(b.run("v = math.random(1, 1000000) + math.random()", "b", &err));
    lua_getglobal(a.state(), "v");
    lua_getglobal(b.state(), "v");
    CHECK(lua_isnumber(a.state(), -1));
    CHECK_EQ(static_cast<double>(lua_tonumber(a.state(), -1)), static_cast<double>(lua_tonumber(b.state(), -1)));
    lua_pop(a.state(), 1);
    lua_pop(b.state(), 1);
    spindle::scripting::Pcg32 r1(5), r2(5);
    for (int i = 0; i < 100; ++i) CHECK_EQ(r1.next(), r2.next());
    for (int i = 0; i < 1000; ++i) {
        const auto v = r1.range(-3, 3);
        CHECK(v >= -3 && v <= 3);
    }
}
