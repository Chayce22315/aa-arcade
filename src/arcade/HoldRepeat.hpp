#pragma once

// Hold-to-repeat for a digital direction (letter wheel, vote cursor). Station keys never use the
// operating system's key repeat (fire must not auto-fire), so repeating is done here on the tick.
namespace spindle::arcade {

class HoldRepeat {
public:
    static constexpr double kDelay = 0.35; // seconds held before the first repeat
    static constexpr double kRate = 0.09;  // seconds between repeats after that

    // presses: new presses this tick; held: still down at the end of the tick. Returns the steps to move.
    int update(int presses, bool held, double dt) {
        int steps = presses > 0 ? presses : 0;
        if (!held) {
            held_ = 0.0;
            return steps;
        }
        if (presses > 0) {
            held_ = 0.0; // a fresh press restarts the delay
            next_ = kDelay;
            return steps;
        }
        held_ += dt;
        while (held_ >= next_) {
            ++steps;
            next_ += kRate;
        }
        return steps;
    }

    void reset() {
        held_ = 0.0;
        next_ = kDelay;
    }

private:
    double held_ = 0.0;
    double next_ = kDelay;
};

} // namespace spindle::arcade
