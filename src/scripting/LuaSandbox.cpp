#include "scripting/LuaSandbox.hpp"

#include <cstdlib>
#include <format>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

#include "engine/Log.hpp"

namespace spindle::scripting {

// ------------------------------------------------------------------ Pcg32

Pcg32::Pcg32(std::uint64_t seed, std::uint64_t stream) {
    inc_ = (stream << 1u) | 1u;
    state_ = 0;
    next();
    state_ += seed;
    next();
}

std::uint32_t Pcg32::next() {
    const std::uint64_t old = state_;
    state_ = old * 6364136223846793005ULL + inc_;
    const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
    const auto rot = static_cast<std::uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
}

double Pcg32::nextDouble() {
    const std::uint64_t hi = next() >> 5;  // 27 bits
    const std::uint64_t lo = next() >> 6;  // 26 bits
    return static_cast<double>((hi << 26) | lo) * (1.0 / 9007199254740992.0);
}

std::int64_t Pcg32::range(std::int64_t lo, std::int64_t hi) {
    if (hi <= lo) return lo;
    const auto span = static_cast<std::uint64_t>(hi - lo) + 1u;
    // Rejection sampling over 64 bits avoids modulo bias.
    const std::uint64_t limit = span == 0 ? 0 : (~std::uint64_t{0} - (~std::uint64_t{0} % span));
    while (true) {
        const std::uint64_t v = (static_cast<std::uint64_t>(next()) << 32) | next();
        if (span == 0) return static_cast<std::int64_t>(v);
        if (v < limit) return lo + static_cast<std::int64_t>(v % span);
    }
}

// ------------------------------------------------------------------ LuaSandbox

LuaSandbox::LuaSandbox(std::string name, std::size_t memoryLimitBytes) : name_(std::move(name)), limit_(memoryLimitBytes) {
    state_ = lua_newstate(&LuaSandbox::allocate, this);
    if (!state_) {
        log::error("lua[{}]: could not create state", name_);
        return;
    }
    lua_State* L = state_;
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);

    for (const char* banned : {"dofile", "loadfile", "load", "require", "collectgarbage"}) {
        lua_pushnil(L);
        lua_setglobal(L, banned);
    }
    lua_getglobal(L, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pop(L, 1);

    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &LuaSandbox::luaPrint, 1);
    lua_setglobal(L, "print");

    lua_getglobal(L, LUA_MATHLIBNAME);
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &LuaSandbox::luaRandom, 1);
    lua_setfield(L, -2, "random");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &LuaSandbox::luaRandomSeed, 1);
    lua_setfield(L, -2, "randomseed");
    lua_pop(L, 1);

    lua_sethook(L, &LuaSandbox::countHook, LUA_MASKCOUNT, 1000);
}

LuaSandbox::~LuaSandbox() {
    if (state_) lua_close(state_);
}

void* LuaSandbox::allocate(void* ud, void* ptr, std::size_t osize, std::size_t nsize) {
    auto* self = static_cast<LuaSandbox*>(ud);
    const std::size_t oldSize = ptr ? osize : 0; // with ptr == NULL, osize encodes the object type
    if (nsize == 0) {
        std::free(ptr);
        self->used_ -= oldSize;
        return nullptr;
    }
    if (nsize > oldSize && self->used_ - oldSize + nsize > self->limit_) return nullptr;
    void* p = std::realloc(ptr, nsize);
    if (!p) return nullptr;
    self->used_ = self->used_ - oldSize + nsize;
    return p;
}

void LuaSandbox::countHook(lua_State* L, lua_Debug*) {
    void* ud = nullptr;
    lua_getallocf(L, &ud);
    auto* self = static_cast<LuaSandbox*>(ud);
    self->executed_ += 1000;
    if (self->executed_ > self->budget_) luaL_error(L, "instruction budget of %d exceeded", static_cast<int>(self->budget_));
}

int LuaSandbox::luaRandom(lua_State* L) {
    auto* self = static_cast<LuaSandbox*>(lua_touserdata(L, lua_upvalueindex(1)));
    const int n = lua_gettop(L);
    if (n == 0) {
        lua_pushnumber(L, self->rng_.nextDouble());
        return 1;
    }
    lua_Integer lo = 1;
    lua_Integer hi = 0;
    if (n == 1) {
        hi = luaL_checkinteger(L, 1);
    } else {
        lo = luaL_checkinteger(L, 1);
        hi = luaL_checkinteger(L, 2);
    }
    luaL_argcheck(L, lo <= hi, n == 1 ? 1 : 2, "interval is empty");
    lua_pushinteger(L, static_cast<lua_Integer>(self->rng_.range(lo, hi)));
    return 1;
}

int LuaSandbox::luaRandomSeed(lua_State* L) {
    auto* self = static_cast<LuaSandbox*>(lua_touserdata(L, lua_upvalueindex(1)));
    const lua_Integer seed = luaL_checkinteger(L, 1);
    self->seedRandom(static_cast<std::uint64_t>(seed));
    return 0;
}

int LuaSandbox::luaPrint(lua_State* L) {
    auto* self = static_cast<LuaSandbox*>(lua_touserdata(L, lua_upvalueindex(1)));
    std::string line;
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        std::size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) line += '\t';
        line.append(s, len);
        lua_pop(L, 1);
    }
    log::info("lua[{}]: {}", self->name_, line);
    return 0;
}

bool LuaSandbox::run(std::string_view code, std::string_view chunkName, std::string* error) {
    if (!state_) {
        if (error) *error = "no Lua state";
        return false;
    }
    const std::string chunk = std::format("={}", chunkName);
    if (luaL_loadbufferx(state_, code.data(), code.size(), chunk.c_str(), "t") != LUA_OK) {
        if (error) *error = lua_tostring(state_, -1) ? lua_tostring(state_, -1) : "load failed";
        lua_pop(state_, 1);
        return false;
    }
    return call(0, 0, error);
}

bool LuaSandbox::call(int nargs, int nresults, std::string* error) {
    if (!state_) {
        if (error) *error = "no Lua state";
        return false;
    }
    executed_ = 0;
    const int status = lua_pcall(state_, nargs, nresults, 0);
    if (status != LUA_OK) {
        const char* msg = lua_tostring(state_, -1);
        if (error) *error = msg ? msg : "error object is not a string";
        lua_pop(state_, 1);
        return false;
    }
    return true;
}

void LuaSandbox::seedRandom(std::uint64_t seed) { rng_ = Pcg32(seed, 0x2545F4914F6CDD1DULL); }

} // namespace spindle::scripting
