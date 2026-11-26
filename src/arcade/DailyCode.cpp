#include "arcade/DailyCode.hpp"

#include <cctype>
#include <charconv>
#include <format>

#include "arcade/HighScores.hpp"
#include "assets/Sha256.hpp"

namespace spindle::arcade {
namespace {

constexpr std::int64_t kEpochDays = 19723; // 2024-01-01 in days since 1970-01-01
constexpr std::uint64_t kCheckBits = 20;

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's algorithm).
std::int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? static_cast<unsigned>(-3) : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

std::string civilFromDays(std::int64_t z) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    return std::format("{:04}-{:02}-{:02}", y + (m <= 2 ? 1 : 0), m, d);
}

std::optional<std::int64_t> parseDate(std::string_view iso) {
    if (!isIsoDate(iso)) return std::nullopt;
    int y = 0, m = 0, d = 0;
    std::from_chars(iso.data(), iso.data() + 4, y);
    std::from_chars(iso.data() + 5, iso.data() + 7, m);
    std::from_chars(iso.data() + 8, iso.data() + 10, d);
    const std::int64_t days = daysFromCivil(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
    if (civilFromDays(days) != iso) return std::nullopt; // 2026-02-30 and the like
    return days;
}

std::uint64_t check(std::string_view date, std::int64_t score) {
    assets::Sha256 h;
    h.update(std::format("spindle daily {} {}", date, score));
    const assets::Sha256Digest digest = h.finish();
    const std::uint64_t top = (static_cast<std::uint64_t>(digest[0]) << 16) | (static_cast<std::uint64_t>(digest[1]) << 8) | digest[2];
    return top >> (24 - kCheckBits);
}

} // namespace

std::optional<std::string> dailyCode(std::string_view isoDate, std::int64_t score) {
    const auto days = parseDate(isoDate);
    if (!days || score < 0 || score > kMaxCodeScore) return std::nullopt;
    const std::int64_t offset = *days - kEpochDays;
    if (offset < 0 || offset > 0xFFFF) return std::nullopt;
    const std::uint64_t value = (static_cast<std::uint64_t>(offset) << 44) | (static_cast<std::uint64_t>(score) << kCheckBits) | check(isoDate, score);
    std::string out(kCodeLength, '0');
    for (std::size_t i = 0; i < kCodeLength; ++i) out[kCodeLength - 1 - i] = kCodeAlphabet[(value >> (5 * i)) & 31u];
    return out;
}

std::string formatDailyCode(std::string_view code) {
    std::string out;
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (i > 0 && i % 4 == 0) out += '-';
        out += code[i];
    }
    return out;
}

std::optional<DailyScore> checkDailyCode(std::string_view code) {
    std::uint64_t value = 0;
    std::size_t n = 0;
    for (char raw : code) {
        if (raw == '-' || raw == ' ') continue;
        char ch = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
        if (ch == 'O') ch = '0';
        if (ch == 'I' || ch == 'L') ch = '1';
        const std::size_t digit = kCodeAlphabet.find(ch);
        if (digit == std::string_view::npos || ++n > kCodeLength) return std::nullopt;
        value = (value << 5) | digit;
    }
    if (n != kCodeLength) return std::nullopt;
    const auto offset = static_cast<std::int64_t>(value >> 44);
    const auto score = static_cast<std::int64_t>((value >> kCheckBits) & 0xFFFFFFu);
    const std::string date = civilFromDays(kEpochDays + offset);
    if ((value & ((1u << kCheckBits) - 1)) != check(date, score)) return std::nullopt;
    return DailyScore{date, score};
}

} // namespace spindle::arcade
