#pragma once

#include <cstdint>

// What the cabinet asks a player's hands to feel (docs/SETTINGS.md "[Feedback]"). The arcade machine
// decides when; the platform layer turns a moment into a gamepad rumble or a phone vibration.
namespace spindle::arcade {

enum class HapticKind {
    Tick,      // a needle stuck
    CloseCall, // it stuck close to another
    Fail,      // a touch or the clock
    Fire,      // caught fire
    Clear,     // cleared the round
    Digit,     // a digit landed on the round end screen
    Letter,    // a letter was shot into the high score table
    Crown,     // took the crown
    Select     // moved the vote cursor / voted
};

// How a moment feels on a plain rumble motor: strength 0..1, length, and a second pulse for doubles.
struct HapticPattern {
    float strength = 0.0f;
    std::uint32_t milliseconds = 0;
    bool doublePulse = false;
};

constexpr HapticPattern hapticPattern(HapticKind k) {
    switch (k) {
    case HapticKind::Tick: return {0.25f, 14, false};
    case HapticKind::CloseCall: return {0.5f, 22, false};
    case HapticKind::Fail: return {1.0f, 140, false};
    case HapticKind::Fire: return {0.8f, 220, false};
    case HapticKind::Clear: return {0.6f, 70, false};
    case HapticKind::Digit: return {0.2f, 10, false};
    case HapticKind::Letter: return {0.3f, 16, false};
    case HapticKind::Crown: return {0.7f, 45, true};
    case HapticKind::Select: return {0.15f, 9, false};
    }
    return {};
}

} // namespace spindle::arcade
