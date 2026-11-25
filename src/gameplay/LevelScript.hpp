#pragma once

#include <memory>
#include <optional>
#include <string>

#include "gameplay/LevelDef.hpp"

namespace spindle::scripting {
class LuaSandbox;
}

namespace spindle::gameplay {

struct ScriptSource {
    std::string code;
    std::string origin; // file path or "embedded:scripts/levels.lua"
};

// Runs data/scripts/levels.lua. An operator override (loose file) is tried first; if it fails to load
// or produces an invalid round, the embedded original is used for that round.
class LevelScript final : public LevelProvider {
public:
    explicit LevelScript(std::optional<ScriptSource> override = std::nullopt);
    ~LevelScript() override;

    LevelDef level(int round, std::uint32_t seed) override;

    bool embeddedHealthy() const { return embedded_ != nullptr; }
    bool usingOverride() const { return override_ != nullptr; }
    const LevelLimits& limits() const { return limits_; }

    // Raw generation without fallback, for tests and tools.
    static std::optional<LevelDef> generate(scripting::LuaSandbox& sandbox, int round, std::uint32_t seed,
                                            const LevelLimits& limits, std::string* error);
    static std::unique_ptr<scripting::LuaSandbox> load(const ScriptSource& source, std::string* error);
    static ScriptSource embeddedSource();

private:
    LevelLimits limits_;
    std::unique_ptr<scripting::LuaSandbox> override_;
    std::unique_ptr<scripting::LuaSandbox> embedded_;
    std::string overrideOrigin_;
};

// Last-resort round used only if even the embedded script fails (logged as an error).
LevelDef emergencyLevel(int round);

std::uint64_t levelSeed(std::uint32_t matchSeed, int round);

} // namespace spindle::gameplay
