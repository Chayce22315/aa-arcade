// Shooting initials: the letter wheel, hold-to-repeat, delete / end, six letters, the clock.

#include "Test.hpp"
#include "arcade/ArcadeMachine.hpp"
#include "arcade/HoldRepeat.hpp"
#include "arcade/InitialsEntry.hpp"
#include "gameplay/Geometry.hpp"

using namespace spindle::arcade;

namespace {

constexpr double kDt = spindle::gameplay::geom::kTickSeconds;

StationButtons fire() {
    StationButtons b;
    b.firePresses = 1;
    return b;
}
StationButtons right(int n = 1) {
    StationButtons b;
    b.rightPresses = n;
    return b;
}
StationButtons left(int n = 1) {
    StationButtons b;
    b.leftPresses = n;
    return b;
}

} // namespace

TEST_CASE(hold_repeat_waits_then_repeats_and_restarts_on_a_new_press) {
    HoldRepeat h;
    CHECK_EQ(h.update(1, true, kDt), 1);
    int steps = 0;
    for (double t = 0.0; t < HoldRepeat::kDelay - 0.02; t += kDt) steps += h.update(0, true, kDt);
    CHECK_EQ(steps, 0); // nothing before the delay
    steps = 0;
    for (double t = 0.0; t < HoldRepeat::kRate * 5.0 + 0.03; t += kDt) steps += h.update(0, true, kDt);
    CHECK(steps >= 5 && steps <= 7);
    CHECK_EQ(h.update(0, false, kDt), 0); // released
    CHECK_EQ(h.update(2, false, kDt), 2); // quick taps all count
}

TEST_CASE(initials_wheel_shoots_letters_deletes_and_ends) {
    InitialsEntry e(1, 7, 15.0, "P2");
    CHECK_EQ(e.cursor(), 0);
    e.tick(fire(), kDt); // A
    e.tick(right(), kDt);
    e.tick(fire(), kDt); // B
    CHECK_EQ(e.typed(), std::string("AB"));
    e.tick(left(2), kDt); // B -> A -> wraps to "end"
    CHECK_EQ(e.cursor(), InitialsEntry::kEnd);
    e.tick(left(), kDt); // "<"
    CHECK_EQ(e.cursor(), InitialsEntry::kDelete);
    e.tick(fire(), kDt);
    CHECK_EQ(e.typed(), std::string("A"));
    CHECK(!e.done());
    e.tick(right(), kDt); // "end"
    e.tick(fire(), kDt);
    CHECK(e.done());
    CHECK(!e.timedOut());
    CHECK_EQ(e.name(), std::string("A"));
    const int shots = e.shots();
    e.tick(fire(), kDt); // nothing after done
    CHECK_EQ(e.shots(), shots);
    CHECK_EQ(InitialsEntry::symbolLabel(0), std::string("A"));
    CHECK_EQ(InitialsEntry::symbolLabel(35), std::string("9"));
    CHECK_EQ(InitialsEntry::symbolLabel(InitialsEntry::kDelete), std::string("<"));
    CHECK_EQ(InitialsEntry::symbolLabel(InitialsEntry::kEnd), std::string("end"));
}

TEST_CASE(initials_sixth_letter_finishes_and_the_clock_saves_what_is_typed) {
    InitialsEntry six(0, 1, 15.0, "P1");
    for (int i = 0; i < 6; ++i) six.tick(fire(), kDt);
    CHECK(six.done());
    CHECK_EQ(six.name(), std::string("AAAAAA"));

    InitialsEntry slow(2, 2, 15.0, "P3");
    slow.tick(right(25), kDt); // "Z"
    slow.tick(fire(), kDt);
    for (int i = 0; i < 16 * spindle::gameplay::geom::kTickRate && !slow.done(); ++i) slow.tick({}, kDt);
    CHECK(slow.done());
    CHECK(slow.timedOut());
    CHECK_EQ(slow.name(), std::string("Z"));

    InitialsEntry idle(3, 3, 10.0, "P4");
    for (int i = 0; i < 11 * spindle::gameplay::geom::kTickRate; ++i) idle.tick({}, kDt);
    CHECK(idle.done());
    CHECK_EQ(idle.name(), std::string("P4")); // nothing typed: the station tag
}
