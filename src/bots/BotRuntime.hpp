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
    double minSeparation = 0.0; // radians (effective: slim makes it smaller)
    double attachAngle = 0.0;   // radians
    bool fireReady = false;     // the fire interval has elapsed (current, the bot's own button)
    std::span<const double> pickups; // relative angles of powerup rings not yet taken
    double claimAngle = 0.0;    // a needle within this angle of a ring's centre takes it
    std::uint32_t revision = 0; // changes whenever the attached set changes (placements and burns)
    // Fire meter, as of the delayed moment.
    bool fireEnabled = false;
    bool onFire = false;
    double fireLeft = 0.0;      // seconds of fire left
    int combo = 0;
    int comboTarget = 0;        // combo that sets the station on fire
    double chainLeft = 0.0;     // seconds before the combo drops (0 = no chain)
    double closeCall = 0.0;     // a landing nearer than this to a needle counts double (radians)
    double roundTime = 0.0;
    double timeLeft = 0.0;      // seconds until the round timer runs out (shown on screen)
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
    int pickupsRef_ = -1;
    int memRef_ = -1;
    int lastAttachedLen_ = 0;
    int lastPickupsLen_ = 0;
    int errors_ = 0;
    bool disabled_ = false;
};

} // namespace spindle::bots
