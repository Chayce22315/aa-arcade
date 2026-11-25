#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "bots/BotRuntime.hpp"
#include "gameplay/Playfield.hpp"
#include "gameplay/Rotor.hpp"

namespace spindle::bots {

// Feeds one bot its delayed view of its own station and turns its decisions into fire presses.
// A bot has no other output: the press goes into the same per-station queue as a human's.
class BotController {
public:
    explicit BotController(std::unique_ptr<BotBrain> brain);

    void beginRound();
    // Call once per Playing tick, before the match tick. Returns the number of fire presses (0 or 1).
    int think(const gameplay::RotorState& rotor, const gameplay::Playfield& field, double roundTime, double timeLeft,
              std::uint64_t tick);

    const BotBrain& brain() const { return *brain_; }
    int delayTicks() const { return delayTicks_; }

private:
    struct Snapshot {
        double angle = 0.0;
        double speed = 0.0;
        double accel = 0.0;
        std::vector<double> attached; // burns remove needles, so each moment keeps its own copy
        std::uint32_t revision = 0;
        int needlesLeft = 0;
        int inFlight = 0;
        bool onFire = false;
        double fireLeft = 0.0;
        int combo = 0;
        double chainLeft = 0.0;
    };

    std::unique_ptr<BotBrain> brain_;
    int delayTicks_ = 0;
    std::vector<Snapshot> history_; // ring buffer of delayTicks_ + 1 entries
    std::size_t head_ = 0;
    std::size_t filled_ = 0;
    std::vector<double> pickupScratch_;
};

} // namespace spindle::bots
