#pragma once

#include <cstddef>
#include <vector>

namespace spindle::gameplay {

// One leg of a rotation program: ramp linearly from the current speed to targetSpeed over rampTime,
// then hold targetSpeed for holdTime. Speeds are rad/s, positive = clockwise on screen.
struct RotationSegment {
    double targetSpeed = 0.0;
    double rampTime = 0.0;
    double holdTime = 0.0;
};

// Segments loop forever. Reversal = target of the opposite sign; pause = target 0.
struct RotationProgram {
    double initialSpeed = 0.0;
    std::vector<RotationSegment> segments;
};

struct RotorState {
    double angle = 0.0; // [0, 2pi)
    double speed = 0.0; // rad/s
    double accel = 0.0; // rad/s^2
};

// The single rotation source of a round. Every station reads the same instance, so all circles have
// the same angle, speed and direction on every tick.
class Rotor {
public:
    Rotor() = default;
    explicit Rotor(RotationProgram program);

    // Exact integration (piecewise constant acceleration), deterministic for a fixed dt.
    void advance(double dt);
    const RotorState& state() const { return state_; }
    const RotationProgram& program() const { return program_; }

private:
    RotationProgram program_;
    RotorState state_;
    std::size_t segment_ = 0;
    double segmentTime_ = 0.0;
    double segmentStartSpeed_ = 0.0;
};

// Multiplies all speeds (and the initial speed) by scale.
RotationProgram scaled(RotationProgram program, double scale);

} // namespace spindle::gameplay
