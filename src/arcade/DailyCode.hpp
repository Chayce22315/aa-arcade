#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The daily score code (docs/RULES.md "Daily score code"): a 12-character code a player can share to show
// their daily score. It packs the date and the score with a 20-bit check from SHA-256, in Crockford's
// base32 (no I, L, O or U; those letters are read as 1, 1, 0 when checked). It catches typos, not
// cheats: spindle is open source, so anyone can make a valid code.
namespace spindle::arcade {

struct DailyScore {
    std::string date; // YYYY-MM-DD
    std::int64_t score = 0;
    bool operator==(const DailyScore&) const = default;
};

inline constexpr std::string_view kCodeAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
inline constexpr std::size_t kCodeLength = 12;
inline constexpr std::int64_t kMaxCodeScore = (1 << 24) - 1;

// Nothing for a date before 2024-01-01 or after 2203, or a score outside 0..kMaxCodeScore.
std::optional<std::string> dailyCode(std::string_view isoDate, std::int64_t score);
// "XXXX-XXXX-XXXX" for display.
std::string formatDailyCode(std::string_view code);
// Checks a typed code (any case, dashes and spaces ignored); nothing if it is not a valid code.
std::optional<DailyScore> checkDailyCode(std::string_view code);

} // namespace spindle::arcade
