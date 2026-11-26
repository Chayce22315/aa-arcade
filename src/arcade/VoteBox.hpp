#pragma once

#include <array>
#include <optional>
#include <vector>

#include "gameplay/Modes.hpp"

// The game mode vote (docs/RULES.md "Game modes"): every joined human points a cursor at a mode card
// and casts a vote; when the clock runs out the mode with the most votes wins, and a tie goes to a
// wheel with the tied modes as equal slices. Pure logic; the arcade machine feeds it input.
namespace spindle::arcade {

class VoteBox {
public:
    static constexpr double kAllVotedSeconds = 5.0; // once every joined player has voted the clock drops to this
    static constexpr double kLateJoinSeconds = 5.0; // a player joining in the last seconds pushes it back up to this
    static constexpr double kSpinSeconds = 3.5;     // the tie-break wheel
    static constexpr double kIntroSeconds = 2.0;    // the chosen mode's card

    VoteBox() = default;
    VoteBox(std::vector<gameplay::ModeId> modes, double seconds);

    void join(int station);
    void move(int station, int steps); // left / right over the cards (wraps)
    void cast(int station);            // a vote for the card under the cursor; casting again changes it
    void tick(double dt);

    const std::vector<gameplay::ModeId>& modes() const { return modes_; }
    double timeLeft() const { return timeLeft_; }
    bool expired() const { return timeLeft_ <= 0.0; }
    bool joined(int station) const;
    int cursor(int station) const;            // card index
    std::optional<int> vote(int station) const; // card index
    int votesFor(int card) const;
    bool allVoted() const;                    // every joined player has a vote in
    // Cards with the most votes; every card when nobody voted.
    std::vector<int> leaders() const;

private:
    std::vector<gameplay::ModeId> modes_{gameplay::ModeId::Battle};
    double timeLeft_ = 30.0;
    std::array<bool, 4> joined_{};
    std::array<int, 4> cursor_{};
    std::array<std::optional<int>, 4> vote_{};
};

// The wheel's turn (radians, clockwise on screen) at `t` seconds into the spin; it slows down and stops
// with the centre of slice `result` (of `slices`) under the needle, which comes up from the bottom.
double wheelAngle(double t, int slices, int result);
// The slice under the needle at a given wheel turn.
int wheelSliceAt(double angle, int slices);

} // namespace spindle::arcade
