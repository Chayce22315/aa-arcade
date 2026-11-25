#include "gameplay/Rotor.hpp"

#include <algorithm>
#include <cmath>

#include "collision/Collision.hpp"

namespace spindle::gameplay {

Rotor::Rotor(RotationProgram program) : program_(std::move(program)) {
    state_.speed = program_.initialSpeed;
    segmentStartSpeed_ = program_.initialSpeed;
}

void Rotor::advance(double dt) {
    if (!(dt > 0.0) || !std::isfinite(dt)) return;
    if (program_.segments.empty()) {
        state_.accel = 0.0;
        state_.angle = collision::wrapPositive(state_.angle + state_.speed * dt);
        return;
    }
    double remaining = dt;
    // Bounded: a validated program has a positive cycle length, so each pass consumes time or advances
    // a segment; the guard stops a degenerate program (all zero-length segments) from spinning.
    int guard = static_cast<int>(program_.segments.size()) * 2 + 8;
    while (remaining > 0.0 && guard-- > 0) {
        const RotationSegment& seg = program_.segments[segment_];
        const double ramp = std::max(0.0, seg.rampTime);
        const double total = ramp + std::max(0.0, seg.holdTime);
        if (segmentTime_ < ramp) {
            const double accel = (seg.targetSpeed - segmentStartSpeed_) / ramp;
            const double step = std::min(remaining, ramp - segmentTime_);
            state_.angle += state_.speed * step + 0.5 * accel * step * step;
            state_.speed += accel * step;
            state_.accel = accel;
            segmentTime_ += step;
            remaining -= step;
            if (segmentTime_ >= ramp) state_.speed = seg.targetSpeed; // remove drift at the ramp end
            continue;
        }
        if (segmentTime_ < total) {
            state_.speed = seg.targetSpeed;
            state_.accel = 0.0;
            const double step = std::min(remaining, total - segmentTime_);
            state_.angle += state_.speed * step;
            segmentTime_ += step;
            remaining -= step;
            continue;
        }
        state_.speed = seg.targetSpeed;
        segmentStartSpeed_ = state_.speed;
        segment_ = (segment_ + 1) % program_.segments.size();
        segmentTime_ = 0.0;
    }
    if (remaining > 0.0) state_.angle += state_.speed * remaining;
    state_.angle = collision::wrapPositive(state_.angle);
}

RotationProgram scaled(RotationProgram program, double scale) {
    program.initialSpeed *= scale;
    for (RotationSegment& s : program.segments) s.targetSpeed *= scale;
    return program;
}

} // namespace spindle::gameplay
