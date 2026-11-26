#include "arcade/InitialsEntry.hpp"

#include <algorithm>

#include "arcade/ArcadeMachine.hpp"

namespace spindle::arcade {

InitialsEntry::InitialsEntry(int station, std::uint32_t token, double seconds, std::string fallbackName)
    : station_(station), token_(token), seconds_(std::max(1.0, seconds)), timeLeft_(seconds_), fallback_(sanitizeName(fallbackName)) {
    if (fallback_.empty()) fallback_ = "P";
}

std::string InitialsEntry::symbolLabel(int symbol) {
    if (symbol >= 0 && symbol < kDelete) return std::string(1, kLetters[static_cast<std::size_t>(symbol)]);
    if (symbol == kDelete) return "<";
    return "end";
}

void InitialsEntry::shoot() {
    ++shots_;
    sinceShot_ = 0.0;
    lastShot_ = cursor_;
    if (cursor_ == kEnd) {
        done_ = true;
        return;
    }
    if (cursor_ == kDelete) {
        if (!typed_.empty()) typed_.pop_back();
        return;
    }
    typed_ += kLetters[static_cast<std::size_t>(cursor_)];
    if (typed_.size() >= kMaxNameLength) done_ = true; // the sixth letter finishes
}

void InitialsEntry::tick(const StationButtons& b, double dt) {
    if (done_) return;
    sinceShot_ += dt;
    const int steps = right_.update(b.rightPresses, b.rightHeld, dt) - left_.update(b.leftPresses, b.leftHeld, dt);
    cursor_ = ((cursor_ + steps) % kSymbols + kSymbols) % kSymbols;
    const int presses = b.firePresses + (b.startPressed ? 1 : 0);
    for (int i = 0; i < presses && !done_; ++i) shoot();
    if (done_) return;
    timeLeft_ -= dt;
    if (timeLeft_ <= 0.0) {
        timeLeft_ = 0.0;
        timedOut_ = true;
        done_ = true;
    }
}

} // namespace spindle::arcade
