#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Powerups: rings placed on the circle by the level script. A needle that lands on a ring takes its
// power. Every station gets the same rings (they are part of the round definition) and every effect
// applies to the collecting station only, so the shared rotation and the identical round conditions
// are never touched. There are no rings in sudden death.
namespace spindle::gameplay {

enum class PowerupKind {
    Shield, // the next touch this round bounces off; that needle returns to the queue
    Skip,   // one needle fewer to place this round
    Slim,   // this station's balls shrink for the rest of the round
    Mend    // gives back one lost try (never above the starting tries; no effect in sudden death)
};

inline constexpr std::array<PowerupKind, 4> kAllPowerups{PowerupKind::Shield, PowerupKind::Skip, PowerupKind::Slim, PowerupKind::Mend};

struct Pickup {
    double angle = 0.0; // relative to the rotor, radians
    PowerupKind kind = PowerupKind::Shield;
    bool operator==(const Pickup&) const = default;
};

namespace powerup {
// Ball radius multiplier while slim. Uncalibrated: tune by feel.
inline constexpr double kSlimFactor = 0.7;
// A needle claims a ring when it lands within this fraction of the normal minimum needle separation
// of the ring's centre (each side).
inline constexpr double kClaimFactor = 0.75;
} // namespace powerup

std::string_view powerupName(PowerupKind kind);
std::optional<PowerupKind> parsePowerup(std::string_view name);

// "shield, skip" -> {Shield, Skip}; duplicates removed, canonical order. Unknown names are skipped
// and appended to *unknown when given. "none" or an empty list yields no powerups.
std::vector<PowerupKind> parsePowerupList(std::string_view list, std::vector<std::string>* unknown = nullptr);
std::string formatPowerupList(const std::vector<PowerupKind>& kinds);
bool contains(const std::vector<PowerupKind>& kinds, PowerupKind kind);

} // namespace spindle::gameplay
