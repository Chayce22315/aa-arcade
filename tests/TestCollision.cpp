#include <numbers>
#include <vector>

#include "Test.hpp"
#include "collision/Collision.hpp"
#include "gameplay/Geometry.hpp"

using namespace spindle::collision;
namespace geom = spindle::gameplay::geom;

TEST_CASE(collision_wrap_and_distance) {
    constexpr double pi = std::numbers::pi;
    CHECK_NEAR(wrapAngle(3 * pi), pi, 1e-12);
    CHECK_NEAR(wrapAngle(-3 * pi), pi, 1e-12);
    CHECK_NEAR(wrapAngle(0.5), 0.5, 1e-12);
    CHECK_NEAR(wrapPositive(-0.25), 2 * pi - 0.25, 1e-12);
    CHECK_NEAR(angularDistance(0.1, 2 * pi - 0.1), 0.2, 1e-12);
    CHECK_NEAR(angularDistance(0.0, pi), pi, 1e-12);
    CHECK_EQ(wrapAngle(std::numeric_limits<double>::quiet_NaN()), 0.0);
}

TEST_CASE(collision_min_separation_matches_chord) {
    const double d = minSeparation(geom::kNeedleLength, geom::kBallRadius);
    // Two balls exactly d apart touch: chord equals 2r.
    const double chord = 2.0 * geom::kNeedleLength * std::sin(d / 2.0);
    CHECK_NEAR(chord, 2.0 * geom::kBallRadius, 1e-9);
    CHECK_EQ(minSeparation(10.0, 20.0), std::numbers::pi); // degenerate input
}

TEST_CASE(collision_attach_conflict_threshold_is_strict) {
    const double d = minSeparation(geom::kNeedleLength, geom::kBallRadius);
    const std::vector<double> attached{0.0, 2.0};
    CHECK(!findAttachConflict(d + 1e-9, attached, d).has_value());
    CHECK(findAttachConflict(d - 1e-9, attached, d).has_value());
    CHECK_EQ(findAttachConflict(2.0 + d * 0.5, attached, d).value_or(99), std::size_t{1});
    CHECK(!findAttachConflict(1.0, {}, d).has_value());
}

TEST_CASE(collision_flight_conflict_geometry) {
    const double L = geom::kNeedleLength, r = geom::kBallRadius;
    // Attached ball straight below the centre (relative pi/2, rotor 0) blocks the lane.
    const std::vector<double> bottom{geom::kAttachAngle};
    CHECK(findFlightConflict(L + 2 * r - 0.01, 0.0, bottom, L, r).has_value());
    CHECK(!findFlightConflict(L + 2 * r + 0.01, 0.0, bottom, L, r).has_value());
    // Rotating it a quarter turn away clears the lane.
    CHECK(!findFlightConflict(L + r, std::numbers::pi / 2, bottom, L, r).has_value());
}

TEST_CASE(collision_substeps_bound_motion) {
    const double L = geom::kNeedleLength, r = geom::kBallRadius;
    const int n = substepsFor(30.0, 0.01, L, r);
    CHECK(30.0 / n <= r / 4 + 1e-9);
    CHECK_EQ(substepsFor(0.0, 0.0, L, r), 1);
    const int m = substepsFor(1.0, 0.2, L, r);
    CHECK(0.2 * L / m <= r / 4 + 1e-9);
    CHECK_EQ(substepsFor(1e30, 0.0, L, r), 256);
}
