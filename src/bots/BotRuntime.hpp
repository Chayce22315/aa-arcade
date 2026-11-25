#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "config/Settings.hpp"
#include "gameplay/LevelScript.hpp"

namespace spindle::scripting {
class LuaSandbox;
}

namespace spindle::bots {

// What a bot is allowed to know, already delayed by its reaction time (except its own fire button).
struct BotView {
    double angle = 0.0;      // rotor angle, delayed
    double speed = 0.0;      // rad/s, delayed
    double accel = 0.0;      // rad/s^2, delayed
    std::span<const double> attached; // relative needle angles, as of the delayed moment
    int needlesLeft = 0;
    int inFlight = 0;
    double flightTime = 0.0;    // seconds from launch to attachment
    double minSeparation = 0.0; // radians
    double attachAngle = 0.0;   // radians
    bool fireReady = false;     // the fire interval has elapsed (current, the bot's own button)
    double roundTime = 0.0;
    double delay = 0.0;         // seconds of delay applied to this view
    double dt = 0.0;
};

struct BotProfile {
    std::string name;
    double reaction = 0.2; // seconds; enforced by the C++ observation delay
};

// One sandboxed Lua state running data/scripts/bots/bot.lua for one bot.
class BotBrain {
public:
    static std::unique_ptr<BotBrain> create(const gameplay::ScriptSource& source, config::Difficulty difficulty,
                                            std::uint64_t seed, std::string* error);
    ~BotBrain();

    // Returns true to press fire this tick. Script errors count; after too many the bot stops firing.
    bool decide(const BotView& view);
    void resetMemory();

    const BotProfile& profile() const { return profile_; }
    bool disabled() const { return disabled_; }
    int errorCount() const { return errors_; }

    static gameplay::ScriptSource embeddedSource();

private:
    BotBrain() = default;

    std::unique_ptr<scripting::LuaSandbox> sandbox_;
    BotProfile profile_;
    int decideRef_ = -1;
    int profileRef_ = -1;
    int obsRef_ = -1;
    int attachedRef_ = -1;
    int memRef_ = -1;
    int lastAttachedLen_ = 0;
    int errors_ = 0;
    bool disabled_ = false;
};

} // namespace spindle::bots
