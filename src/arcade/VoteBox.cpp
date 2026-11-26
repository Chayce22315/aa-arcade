#include "arcade/VoteBox.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace spindle::arcade {
namespace {

constexpr double kTau = 2.0 * std::numbers::pi;
constexpr double kNeedleAngle = std::numbers::pi / 2.0; // straight down on screen (y grows downwards)
constexpr int kTurns = 4;

bool valid(int station) { return station >= 0 && station < 4; }

} // namespace

VoteBox::VoteBox(std::vector<gameplay::ModeId> modes, double seconds) : modes_(std::move(modes)), timeLeft_(std::max(1.0, seconds)) {
    if (modes_.empty()) modes_ = {gameplay::ModeId::Battle};
}

void VoteBox::join(int station) {
    if (!valid(station) || joined_[static_cast<std::size_t>(station)]) return;
    joined_[static_cast<std::size_t>(station)] = true;
    cursor_[static_cast<std::size_t>(station)] = 0;
    // A late joiner still gets a fair chance to vote.
    timeLeft_ = std::max(timeLeft_, kLateJoinSeconds);
}

void VoteBox::move(int station, int steps) {
    if (!valid(station) || !joined_[static_cast<std::size_t>(station)] || steps == 0) return;
    const int n = static_cast<int>(modes_.size());
    int& c = cursor_[static_cast<std::size_t>(station)];
    c = ((c + steps) % n + n) % n;
}

void VoteBox::cast(int station) {
    if (!valid(station) || !joined_[static_cast<std::size_t>(station)]) return;
    vote_[static_cast<std::size_t>(station)] = cursor_[static_cast<std::size_t>(station)];
    if (allVoted()) timeLeft_ = std::min(timeLeft_, kAllVotedSeconds);
}

void VoteBox::tick(double dt) { timeLeft_ = std::max(0.0, timeLeft_ - dt); }

bool VoteBox::joined(int station) const { return valid(station) && joined_[static_cast<std::size_t>(station)]; }

int VoteBox::cursor(int station) const { return valid(station) ? cursor_[static_cast<std::size_t>(station)] : 0; }

std::optional<int> VoteBox::vote(int station) const { return valid(station) ? vote_[static_cast<std::size_t>(station)] : std::nullopt; }

int VoteBox::votesFor(int card) const {
    int n = 0;
    for (std::size_t s = 0; s < 4; ++s)
        if (joined_[s] && vote_[s] == card) ++n;
    return n;
}

bool VoteBox::allVoted() const {
    bool any = false;
    for (std::size_t s = 0; s < 4; ++s) {
        if (!joined_[s]) continue;
        any = true;
        if (!vote_[s]) return false;
    }
    return any;
}

std::vector<int> VoteBox::leaders() const {
    int best = 0;
    for (int c = 0; c < static_cast<int>(modes_.size()); ++c) best = std::max(best, votesFor(c));
    std::vector<int> out;
    for (int c = 0; c < static_cast<int>(modes_.size()); ++c)
        if (best == 0 || votesFor(c) == best) out.push_back(c);
    return out;
}

double wheelAngle(double t, int slices, int result) {
    const int n = std::max(1, slices);
    const double w = kTau / n;
    const double centre = (std::clamp(result, 0, n - 1) + 0.5) * w;
    // Turn so the result's centre ends under the needle, after a few whole turns.
    double end = kNeedleAngle - centre;
    end = std::fmod(end, kTau);
    if (end < 0.0) end += kTau;
    end += kTau * kTurns;
    const double u = std::clamp(t / VoteBox::kSpinSeconds, 0.0, 1.0);
    const double eased = 1.0 - (1.0 - u) * (1.0 - u) * (1.0 - u);
    return end * eased;
}

int wheelSliceAt(double angle, int slices) {
    const int n = std::max(1, slices);
    double local = std::fmod(kNeedleAngle - angle, kTau);
    if (local < 0.0) local += kTau;
    return std::clamp(static_cast<int>(local / (kTau / n)), 0, n - 1);
}

} // namespace spindle::arcade
