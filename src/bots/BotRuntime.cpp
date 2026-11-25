#include "bots/BotRuntime.hpp"

#include <format>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include "assets/EmbeddedAssets.hpp"
#include "engine/Log.hpp"
#include "scripting/LuaSandbox.hpp"

namespace spindle::bots {
namespace {

constexpr int kMaxErrors = 20;
constexpr long long kDecideBudget = 400'000;

void setNumber(lua_State* L, int table, const char* key, double v) {
    lua_pushnumber(L, v);
    lua_setfield(L, table, key);
}

void setInteger(lua_State* L, int table, const char* key, long long v) {
    lua_pushinteger(L, static_cast<lua_Integer>(v));
    lua_setfield(L, table, key);
}

} // namespace

gameplay::ScriptSource BotBrain::embeddedSource() {
    auto text = assets::embeddedText("scripts/bots/bot.lua");
    return {text ? std::string(*text) : std::string(), "embedded:scripts/bots/bot.lua"};
}

std::unique_ptr<BotBrain> BotBrain::create(const gameplay::ScriptSource& source, config::Difficulty difficulty,
                                           std::uint64_t seed, std::string* error) {
    std::unique_ptr<BotBrain> brain(new BotBrain());
    brain->sandbox_ = std::make_unique<scripting::LuaSandbox>("bot", 4u * 1024u * 1024u);
    scripting::LuaSandbox& sb = *brain->sandbox_;
    if (!sb.valid()) {
        if (error) *error = "could not create Lua state";
        return nullptr;
    }
    sb.seedRandom(seed);
    if (!sb.run(source.code, source.origin, error)) return nullptr;
    lua_State* L = sb.state();

    lua_getglobal(L, "decide");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        if (error) *error = "bot script does not define decide(obs, profile, memory)";
        return nullptr;
    }
    brain->decideRef_ = luaL_ref(L, LUA_REGISTRYINDEX);

    const std::string name(config::difficultyName(difficulty));
    lua_getglobal(L, "profiles");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        if (error) *error = "bot script does not define a profiles table";
        return nullptr;
    }
    lua_getfield(L, -1, name.c_str());
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        if (error) *error = std::format("bot script has no '{}' profile", name);
        return nullptr;
    }
    lua_getfield(L, -1, "reaction");
    const double reaction = lua_isnumber(L, -1) ? static_cast<double>(lua_tonumber(L, -1)) : -1.0;
    lua_pop(L, 1);
    if (!(reaction >= 0.0 && reaction <= 0.5)) {
        lua_pop(L, 2);
        if (error) *error = std::format("profile '{}' needs reaction in 0..0.5 seconds", name);
        return nullptr;
    }
    brain->profile_ = {name, reaction};
    brain->profileRef_ = luaL_ref(L, LUA_REGISTRYINDEX); // pops the profile
    lua_pop(L, 1);                                       // profiles

    lua_createtable(L, 64, 0);
    brain->attachedRef_ = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_createtable(L, 4, 0);
    brain->pickupsRef_ = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_createtable(L, 0, 16);
    brain->obsRef_ = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_createtable(L, 0, 8);
    brain->memRef_ = luaL_ref(L, LUA_REGISTRYINDEX);
    return brain;
}

BotBrain::~BotBrain() = default;

void BotBrain::resetMemory() {
    if (!sandbox_ || !sandbox_->valid()) return;
    lua_State* L = sandbox_->state();
    luaL_unref(L, LUA_REGISTRYINDEX, memRef_);
    lua_createtable(L, 0, 8);
    memRef_ = luaL_ref(L, LUA_REGISTRYINDEX);
}

bool BotBrain::decide(const BotView& v) {
    if (disabled_ || !sandbox_) return false;
    lua_State* L = sandbox_->state();
    const int top = lua_gettop(L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, attachedRef_);
    const int n = static_cast<int>(v.attached.size());
    for (int i = 0; i < n; ++i) {
        lua_pushnumber(L, v.attached[static_cast<std::size_t>(i)]);
        lua_rawseti(L, -2, i + 1);
    }
    for (int i = n; i < lastAttachedLen_; ++i) {
        lua_pushnil(L);
        lua_rawseti(L, -2, i + 1);
    }
    lastAttachedLen_ = n;
    lua_pop(L, 1);

    lua_rawgeti(L, LUA_REGISTRYINDEX, pickupsRef_);
    const int np = static_cast<int>(v.pickups.size());
    for (int i = 0; i < np; ++i) {
        lua_pushnumber(L, v.pickups[static_cast<std::size_t>(i)]);
        lua_rawseti(L, -2, i + 1);
    }
    for (int i = np; i < lastPickupsLen_; ++i) {
        lua_pushnil(L);
        lua_rawseti(L, -2, i + 1);
    }
    lastPickupsLen_ = np;
    lua_pop(L, 1);

    lua_rawgeti(L, LUA_REGISTRYINDEX, decideRef_);
    lua_rawgeti(L, LUA_REGISTRYINDEX, obsRef_);
    const int obs = lua_gettop(L);
    setNumber(L, obs, "angle", v.angle);
    setNumber(L, obs, "speed", v.speed);
    setNumber(L, obs, "accel", v.accel);
    lua_rawgeti(L, LUA_REGISTRYINDEX, attachedRef_);
    lua_setfield(L, obs, "attached");
    setInteger(L, obs, "attached_count", n);
    setInteger(L, obs, "needles_left", v.needlesLeft);
    setInteger(L, obs, "in_flight", v.inFlight);
    setNumber(L, obs, "flight_time", v.flightTime);
    setNumber(L, obs, "min_separation", v.minSeparation);
    setNumber(L, obs, "attach_angle", v.attachAngle);
    lua_pushboolean(L, v.fireReady ? 1 : 0);
    lua_setfield(L, obs, "fire_ready");
    lua_rawgeti(L, LUA_REGISTRYINDEX, pickupsRef_);
    lua_setfield(L, obs, "pickups");
    setInteger(L, obs, "pickup_count", np);
    setNumber(L, obs, "claim_angle", v.claimAngle);
    setInteger(L, obs, "revision", v.revision);
    lua_pushboolean(L, v.fireEnabled ? 1 : 0);
    lua_setfield(L, obs, "fire_enabled");
    lua_pushboolean(L, v.onFire ? 1 : 0);
    lua_setfield(L, obs, "on_fire");
    setNumber(L, obs, "fire_left", v.fireLeft);
    setInteger(L, obs, "combo", v.combo);
    setInteger(L, obs, "combo_target", v.comboTarget);
    setNumber(L, obs, "chain_left", v.chainLeft);
    setNumber(L, obs, "close_call", v.closeCall);
    setNumber(L, obs, "round_time", v.roundTime);
    setNumber(L, obs, "time_left", v.timeLeft);
    setNumber(L, obs, "delay", v.delay);
    setNumber(L, obs, "dt", v.dt);
    lua_rawgeti(L, LUA_REGISTRYINDEX, profileRef_);
    lua_rawgeti(L, LUA_REGISTRYINDEX, memRef_);

    sandbox_->setInstructionBudget(kDecideBudget);
    std::string err;
    if (!sandbox_->call(3, 1, &err)) {
        ++errors_;
        if (errors_ <= 3) log::warn("bot[{}]: decide() failed: {}", profile_.name, err);
        if (errors_ >= kMaxErrors) {
            disabled_ = true;
            log::error("bot[{}]: disabled after {} script errors; it will no longer fire", profile_.name, errors_);
        }
        lua_settop(L, top);
        return false;
    }
    const bool fire = lua_toboolean(L, -1) != 0;
    lua_settop(L, top);
    return fire;
}

} // namespace spindle::bots
