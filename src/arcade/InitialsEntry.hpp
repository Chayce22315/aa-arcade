#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "arcade/HoldRepeat.hpp"
#include "arcade/HighScores.hpp"

// One player shooting their initials into a high score table (docs/RULES.md "High scores"):
// left / right turn the letter wheel, fire (or start) shoots the letter shown into the circle.
namespace spindle::arcade {

struct StationButtons;

class InitialsEntry {
public:
    static constexpr std::string_view kLetters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static constexpr int kDelete = static_cast<int>(kLetters.size()); // "<": removes the last letter
    static constexpr int kEnd = kDelete + 1;                          // "end": done
    static constexpr int kSymbols = kEnd + 1;

    InitialsEntry(int station, std::uint32_t token, double seconds, std::string fallbackName);

    void tick(const StationButtons& buttons, double dt);

    int station() const { return station_; }
    std::uint32_t token() const { return token_; }
    bool done() const { return done_; }
    int cursor() const { return cursor_; }       // symbol on the next ammo ball
    const std::string& typed() const { return typed_; }
    std::string name() const { return typed_.empty() ? fallback_ : typed_; } // what gets saved
    double timeLeft() const { return timeLeft_; }
    double seconds() const { return seconds_; }
    double sinceShot() const { return sinceShot_; } // seconds since the last letter was shot (animation)
    int shots() const { return shots_; }            // letters and deletes shot so far (sounds)
    int lastShot() const { return lastShot_; }      // symbol of the last shot, -1 before the first
    bool timedOut() const { return timedOut_; }

    static std::string symbolLabel(int symbol); // "A", "7", "<", "end"

private:
    void shoot();

    int station_ = 0;
    std::uint32_t token_ = 0;
    double seconds_ = 15.0;
    double timeLeft_ = 15.0;
    std::string fallback_;
    std::string typed_;
    int cursor_ = 0;
    bool done_ = false;
    bool timedOut_ = false;
    double sinceShot_ = 99.0;
    int shots_ = 0;
    int lastShot_ = -1;
    HoldRepeat left_, right_;
};

} // namespace spindle::arcade
