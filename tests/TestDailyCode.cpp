// The daily score code: round trips, typo detection, lenient reading, ranges.

#include "Test.hpp"
#include "arcade/DailyCode.hpp"

using namespace spindle::arcade;

TEST_CASE(daily_code_round_trips_date_and_score) {
    for (const auto& [date, score] : {std::pair<const char*, std::int64_t>{"2026-11-26", 12345}, {"2024-01-01", 0}, {"2028-02-29", 9'999'999},
                                       {"2199-12-31", kMaxCodeScore}}) {
        const auto code = dailyCode(date, score);
        REQUIRE(code.has_value());
        CHECK_EQ(code->size(), kCodeLength);
        const auto back = checkDailyCode(*code);
        REQUIRE(back.has_value());
        CHECK_EQ(back->date, std::string(date));
        CHECK_EQ(back->score, score);
        CHECK(checkDailyCode(formatDailyCode(*code)).has_value()); // dashes are fine
    }
    CHECK(dailyCode("2026-11-26", 1) != dailyCode("2026-11-26", 2));
    CHECK(dailyCode("2026-11-26", 1) != dailyCode("2026-11-27", 1));
}

TEST_CASE(daily_code_catches_typos_and_reads_leniently) {
    const std::string code = *dailyCode("2026-11-26", 4321);
    int caught = 0, tried = 0;
    for (std::size_t i = 0; i < code.size(); ++i)
        for (char c : kCodeAlphabet) {
            if (c == code[i]) continue;
            std::string typo = code;
            typo[i] = c;
            ++tried;
            if (!checkDailyCode(typo)) ++caught;
        }
    CHECK(caught * 100 >= tried * 99); // a 20-bit check misses about 1 in a million
    std::string lower = code;
    for (char& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    CHECK(checkDailyCode(lower).has_value());
    CHECK(!checkDailyCode(code.substr(0, 11)).has_value());
    CHECK(!checkDailyCode(code + "0").has_value());
    CHECK(!checkDailyCode("").has_value());
    CHECK(!checkDailyCode("UUUUUUUUUUUU").has_value()); // U is not in the alphabet
}

TEST_CASE(daily_code_refuses_what_it_cannot_carry) {
    CHECK(!dailyCode("2023-12-31", 10).has_value());
    CHECK(!dailyCode("2026-02-30", 10).has_value());
    CHECK(!dailyCode("26-11-2026", 10).has_value());
    CHECK(!dailyCode("2026-11-26", -1).has_value());
    CHECK(!dailyCode("2026-11-26", kMaxCodeScore + 1).has_value());
}
