#include "gameplay/LevelScript.hpp"

#include <algorithm>
#include <cmath>
#include <format>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include "assets/EmbeddedAssets.hpp"
#include "engine/Log.hpp"
#include "gameplay/Geometry.hpp"
#include "scripting/LuaSandbox.hpp"

namespace spindle::gameplay {
namespace {

constexpr double kDegToRad = geom::kPi / 180.0;
constexpr double kRadToDeg = 180.0 / geom::kPi;

std::optional<double> numberField(lua_State* L, int table, const char* name) {
    lua_getfield(L, table, name);
    std::optional<double> v;
    if (lua_isnumber(L, -1)) v = static_cast<double>(lua_tonumber(L, -1));
    lua_pop(L, 1);
    return v;
}

} // namespace

std::uint64_t levelSeed(std::uint32_t matchSeed, int round) {
    std::uint64_t z = (static_cast<std::uint64_t>(matchSeed) << 32) ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(round));
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

ScriptSource LevelScript::embeddedSource() {
    auto text = assets::embeddedText("scripts/levels.lua");
    return {text ? std::string(*text) : std::string(), "embedded:scripts/levels.lua"};
}

std::unique_ptr<scripting::LuaSandbox> LevelScript::load(const ScriptSource& source, std::string* error) {
    auto sandbox = std::make_unique<scripting::LuaSandbox>("levels");
    if (!sandbox->valid()) {
        if (error) *error = "could not create Lua state";
        return nullptr;
    }
    if (source.code.empty()) {
        if (error) *error = "script is empty";
        return nullptr;
    }
    if (!sandbox->run(source.code, source.origin, error)) return nullptr;
    lua_getglobal(sandbox->state(), "level");
    const bool isFunction = lua_isfunction(sandbox->state(), -1);
    lua_pop(sandbox->state(), 1);
    if (!isFunction) {
        if (error) *error = "script does not define function level(round, limits)";
        return nullptr;
    }
    return sandbox;
}

std::optional<LevelDef> LevelScript::generate(scripting::LuaSandbox& sandbox, int round, std::uint32_t seed,
                                              const LevelLimits& limits, std::string* error) {
    lua_State* L = sandbox.state();
    const int top = lua_gettop(L);
    sandbox.seedRandom(levelSeed(seed, round));
    sandbox.setInstructionBudget(5'000'000);

    lua_getglobal(L, "level");
    lua_pushinteger(L, round);
    lua_createtable(L, 0, 6);
    const int maxTotal = static_cast<int>(std::floor(geom::kTwoPi / (limits.minSeparation * limits.packingMargin)));
    lua_pushnumber(L, limits.minSeparation * kRadToDeg);
    lua_setfield(L, -2, "min_separation_deg");
    lua_pushinteger(L, maxTotal);
    lua_setfield(L, -2, "max_total");
    lua_pushinteger(L, limits.maxLaunch);
    lua_setfield(L, -2, "max_launch");
    lua_pushinteger(L, limits.maxPrepins);
    lua_setfield(L, -2, "max_prepins");
    lua_pushnumber(L, limits.maxSpeed * kRadToDeg);
    lua_setfield(L, -2, "max_speed_deg");
    lua_pushinteger(L, limits.maxSegments);
    lua_setfield(L, -2, "max_segments");
    lua_pushinteger(L, limits.maxPickups);
    lua_setfield(L, -2, "max_pickups");
    lua_createtable(L, static_cast<int>(limits.powerups.size()), 0);
    for (std::size_t i = 0; i < limits.powerups.size(); ++i) {
        const std::string name(powerupName(limits.powerups[i]));
        lua_pushlstring(L, name.data(), name.size());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setfield(L, -2, "powerups");

    if (!sandbox.call(2, 1, error)) {
        lua_settop(L, top);
        return std::nullopt;
    }
    const auto fail = [&](std::string message) -> std::optional<LevelDef> {
        if (error) *error = std::move(message);
        lua_settop(L, top);
        return std::nullopt;
    };
    if (!lua_istable(L, -1)) return fail("level() must return a table");
    const int t = lua_gettop(L);

    LevelDef def;
    def.round = round;
    auto launch = numberField(L, t, "launch");
    if (!launch || !std::isfinite(*launch) || *launch != std::floor(*launch) || *launch < 1 || *launch > limits.maxLaunch)
        return fail(std::format("launch must be a whole number 1..{}", limits.maxLaunch));
    def.launchCount = static_cast<int>(*launch);

    lua_getfield(L, t, "prepins");
    if (!lua_isnil(L, -1)) {
        if (!lua_istable(L, -1)) return fail("prepins must be a list of angles");
        const auto n = static_cast<long long>(lua_rawlen(L, -1));
        if (n > limits.maxPrepins) return fail(std::format("more than {} prepins", limits.maxPrepins));
        for (long long i = 1; i <= n; ++i) {
            lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
            if (!lua_isnumber(L, -1)) return fail(std::format("prepins[{}] is not a number", i));
            const double deg = static_cast<double>(lua_tonumber(L, -1));
            lua_pop(L, 1);
            if (!std::isfinite(deg)) return fail(std::format("prepins[{}] is not finite", i));
            def.prepins.push_back(deg * kDegToRad);
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, t, "rotation");
    if (!lua_istable(L, -1)) return fail("rotation must be a table");
    const int rot = lua_gettop(L);
    def.rotation.initialSpeed = numberField(L, rot, "initial").value_or(0.0) * kDegToRad;
    lua_getfield(L, rot, "segments");
    if (!lua_istable(L, -1)) return fail("rotation.segments must be a list");
    const auto segCount = static_cast<long long>(lua_rawlen(L, -1));
    if (segCount < 1 || segCount > limits.maxSegments) return fail(std::format("rotation needs 1..{} segments", limits.maxSegments));
    for (long long i = 1; i <= segCount; ++i) {
        lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
        if (!lua_istable(L, -1)) return fail(std::format("segment {} is not a table", i));
        const int seg = lua_gettop(L);
        RotationSegment s;
        auto speed = numberField(L, seg, "speed");
        if (!speed) return fail(std::format("segment {} has no speed", i));
        s.targetSpeed = *speed * kDegToRad;
        s.rampTime = numberField(L, seg, "ramp").value_or(0.0);
        s.holdTime = numberField(L, seg, "hold").value_or(0.0);
        def.rotation.segments.push_back(s);
        lua_pop(L, 1);
    }
    lua_pop(L, 2); // segments, rotation

    lua_getfield(L, t, "pickups");
    if (!lua_isnil(L, -1)) {
        if (!lua_istable(L, -1)) return fail("pickups must be a list of { angle = degrees, kind = name }");
        const auto n = static_cast<long long>(lua_rawlen(L, -1));
        if (n > limits.maxPickups) return fail(std::format("more than {} pickups", limits.maxPickups));
        for (long long i = 1; i <= n; ++i) {
            lua_rawgeti(L, -1, static_cast<lua_Integer>(i));
            if (!lua_istable(L, -1)) return fail(std::format("pickups[{}] is not a table", i));
            const int pk = lua_gettop(L);
            auto angle = numberField(L, pk, "angle");
            lua_getfield(L, pk, "kind");
            const char* kindName = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
            const auto kind = kindName ? parsePowerup(kindName) : std::nullopt;
            lua_pop(L, 2);
            if (!angle || !std::isfinite(*angle)) return fail(std::format("pickups[{}] needs a finite angle", i));
            if (!kind) return fail(std::format("pickups[{}] has an unknown kind", i));
            def.pickups.push_back({*angle * kDegToRad, *kind});
        }
    }
    lua_settop(L, top);

    if (auto problem = validateLevel(def, limits)) {
        if (error) *error = *problem;
        return std::nullopt;
    }
    return def;
}

LevelScript::LevelScript(std::optional<ScriptSource> override) : limits_(defaultLimits()) {
    std::string err;
    embedded_ = load(embeddedSource(), &err);
    if (!embedded_) log::error("levels: embedded level script failed to load: {}", err);
    if (override) {
        overrideOrigin_ = override->origin;
        override_ = load(*override, &err);
        if (override_)
            log::info("levels: using operator level script {}", overrideOrigin_);
        else
            log::warn("levels: {} failed to load ({}); using the embedded script", overrideOrigin_, err);
    }
}

LevelScript::~LevelScript() = default;

LevelDef LevelScript::level(int round, std::uint32_t seed, const std::vector<PowerupKind>& powerups) {
    LevelLimits limits = limits_;
    limits.powerups = powerups;
    std::string err;
    if (override_) {
        if (auto def = generate(*override_, round, seed, limits, &err)) return *def;
        log::warn("levels: {} round {} rejected ({}); using the embedded script for this round", overrideOrigin_, round, err);
    }
    if (embedded_) {
        if (auto def = generate(*embedded_, round, seed, limits, &err)) return *def;
        log::error("levels: embedded script round {} rejected: {}", round, err);
    }
    return emergencyLevel(round);
}

LevelDef emergencyLevel(int round) {
    LevelDef def;
    def.round = std::max(1, round);
    def.launchCount = std::clamp(6 + round / 2, 6, 16);
    const double speed = std::min(1.3 + 0.05 * round, 3.5);
    def.rotation.initialSpeed = speed;
    def.rotation.segments.push_back({speed, 0.0, 30.0});
    return def;
}

} // namespace spindle::gameplay
