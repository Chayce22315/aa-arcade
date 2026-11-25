#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

struct lua_State;
struct lua_Debug;

namespace spindle::scripting {

// Small deterministic PRNG (PCG32) used for every script random number, so a given seed yields the
// same levels and bot jitter on every platform.
class Pcg32 {
public:
    explicit Pcg32(std::uint64_t seed = 0x853c49e6748fea9bULL, std::uint64_t stream = 0xda3e39cb94b95bdbULL);
    std::uint32_t next();
    double nextDouble(); // [0, 1)
    // Uniform integer in [lo, hi]; requires lo <= hi.
    std::int64_t range(std::int64_t lo, std::int64_t hi);

private:
    std::uint64_t state_ = 0;
    std::uint64_t inc_ = 0;
};

// One isolated Lua 5.4 state with only base, math, string and table. No io, os, package, debug,
// load/dofile/loadfile/require/collectgarbage. Memory is capped and every protected call has an
// instruction budget.
class LuaSandbox {
public:
    LuaSandbox(std::string name, std::size_t memoryLimitBytes = 8u * 1024u * 1024u);
    ~LuaSandbox();
    LuaSandbox(const LuaSandbox&) = delete;
    LuaSandbox& operator=(const LuaSandbox&) = delete;

    bool valid() const { return state_ != nullptr; }
    lua_State* state() const { return state_; }
    const std::string& name() const { return name_; }

    // Compiles and runs a chunk (text only, binary chunks are refused).
    bool run(std::string_view code, std::string_view chunkName, std::string* error);

    // lua_pcall with the instruction budget armed. Stack contract identical to lua_pcall.
    bool call(int nargs, int nresults, std::string* error);

    void setInstructionBudget(long long instructions) { budget_ = instructions; }
    void seedRandom(std::uint64_t seed);
    Pcg32& rng() { return rng_; }
    std::size_t memoryUsed() const { return used_; }

private:
    static void* allocate(void* ud, void* ptr, std::size_t osize, std::size_t nsize);
    static void countHook(lua_State* L, ::lua_Debug* ar);
    static int luaRandom(lua_State* L);
    static int luaRandomSeed(lua_State* L);
    static int luaPrint(lua_State* L);

    std::string name_;
    lua_State* state_ = nullptr;
    std::size_t limit_ = 0;
    std::size_t used_ = 0;
    long long budget_ = 2'000'000;
    long long executed_ = 0;
    Pcg32 rng_;
};

} // namespace spindle::scripting
