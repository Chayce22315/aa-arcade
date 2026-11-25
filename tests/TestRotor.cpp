#include <cmath>

#include "Test.hpp"
#include "collision/Collision.hpp"
#include "gameplay/Geometry.hpp"
#include "gameplay/Rotor.hpp"

using namespace spindle::gameplay;

TEST_CASE(rotor_constant_speed_integrates_exactly) {
    RotationProgram p;
    p.initialSpeed = 1.0;
    p.segments.push_back({1.0, 0.0, 30.0});
    Rotor r(p);
    for (int i = 0; i < geom::kTickRate; ++i) r.advance(geom::kTickSeconds);
    CHECK_NEAR(r.state().angle, 1.0, 1e-9);
    CHECK_NEAR(r.state().speed, 1.0, 1e-12);
}

TEST_CASE(rotor_ramp_reverses_with_exact_kinematics) {
    RotationProgram p;
    p.initialSpeed = 2.0;
    p.segments.push_back({-2.0, 1.0, 1.0}); // ramp 2 -> -2 over 1 s, hold 1 s
    Rotor r(p);
    for (int i = 0; i < geom::kTickRate; ++i) r.advance(geom::kTickSeconds);
    // angle = v0 t + a t^2 / 2 = 2 - 2 = 0 after the ramp
    CHECK_NEAR(spindle::collision::wrapAngle(r.state().angle), 0.0, 1e-9);
    CHECK_NEAR(r.state().speed, -2.0, 1e-12);
    for (int i = 0; i < geom::kTickRate; ++i) r.advance(geom::kTickSeconds);
    CHECK_NEAR(spindle::collision::wrapAngle(r.state().angle), -2.0, 1e-9);
}

TEST_CASE(rotor_is_deterministic_and_shared_reads_agree) {
    RotationProgram p;
    p.initialSpeed = 0.0;
    p.segments = {{3.0, 0.4, 0.7}, {0.0, 0.25, 0.3}, {-2.5, 0.3, 1.1}};
    Rotor a(p), b(p);
    for (int i = 0; i < 20 * geom::kTickRate; ++i) {
        a.advance(geom::kTickSeconds);
        b.advance(geom::kTickSeconds);
        if (a.state().angle != b.state().angle || a.state().speed != b.state().speed) {
            CHECK(false);
            break;
        }
    }
    CHECK(a.state().angle >= 0.0 && a.state().angle < geom::kTwoPi);
}

TEST_CASE(rotor_scaled_program_scales_every_speed) {
    RotationProgram p;
    p.initialSpeed = 1.0;
    p.segments = {{2.0, 0.5, 0.5}};
    auto s = scaled(p, 1.5);
    CHECK_NEAR(s.initialSpeed, 1.5, 1e-12);
    CHECK_NEAR(s.segments[0].targetSpeed, 3.0, 1e-12);
}

TEST_CASE(rotor_degenerate_program_does_not_hang) {
    RotationProgram p;
    p.initialSpeed = 1.0;
    p.segments = {{1.0, 0.0, 0.0}, {2.0, 0.0, 0.0}};
    Rotor r(p);
    r.advance(geom::kTickSeconds); // must return
    CHECK(std::isfinite(r.state().angle));
}
