#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "config/IniDocument.hpp"

// High score tables (docs/RULES.md "High scores"), saved to highscores.ini. Every mode keeps an
// all-time top 10 and today's top 10; today's tables empty themselves when the date changes.
namespace spindle::arcade {

inline constexpr std::size_t kTableSize = 10;
inline constexpr std::size_t kMaxNameLength = 6;

struct ScoreEntry {
    std::string name;      // A-Z 0-9, at most kMaxNameLength
    std::int64_t score = 0;
    int round = 0;         // round the player reached
    std::string date;      // YYYY-MM-DD (local) or empty when the platform gave none
    std::uint32_t token = 0; // non-zero while its player is still shooting their initials
    bool operator==(const ScoreEntry&) const = default;
};

// Keeps only A-Z and 0-9 (letters uppercased), at most kMaxNameLength characters.
std::string sanitizeName(std::string_view name);
bool isIsoDate(std::string_view s);

class HighScoreTable {
public:
    const std::vector<ScoreEntry>& rows() const { return rows_; }
    bool empty() const { return rows_.empty(); }
    // Would this score get a row? It must be above zero and, in a full table, beat a row that is not
    // reserved by a player still shooting their initials (an equal score ranks below the row there).
    bool qualifies(std::int64_t score) const;
    // Inserts in rank order and drops the lowest unreserved row if the table overflows, so a reserved
    // row is never pushed out. Returns the row index, or nothing if it did not make the table.
    std::optional<std::size_t> insert(ScoreEntry e);
    bool rename(std::uint32_t token, std::string_view name); // fills in a reserved row
    void clear() { rows_.clear(); }

private:
    std::vector<ScoreEntry> rows_;
};

struct ModeTables {
    HighScoreTable allTime;
    HighScoreTable today;
};

class HighScores {
public:
    // The local date (YYYY-MM-DD). A different date than the stored one empties every today table.
    void setToday(std::string_view date);
    const std::string& today() const { return today_; }

    const ModeTables& tables(std::string_view mode) const;
    bool anyEntries() const;
    bool qualifies(std::string_view mode, std::int64_t score, bool todayOnly = false) const;

    // Reserves rows for a player who qualified, under a fallback name, before they shoot their
    // initials, so two players qualifying at once can never push each other off. Returns the token
    // to finish with, or 0 when the score made neither table.
    std::uint32_t reserve(std::string_view mode, std::int64_t score, int round, std::string_view fallbackName, bool todayOnly = false);
    // Fills in the name for a reservation (an empty name keeps the fallback). Returns false for an unknown token.
    bool finish(std::uint32_t token, std::string_view name);
    // Rank (0-based) of a reservation in the all-time and today tables, if it is in them.
    std::optional<std::size_t> rankOf(std::string_view mode, std::uint32_t token, bool today) const;

    void clear(std::string_view mode);
    void clearAll();

    std::string serialize() const;
    // modes: the table names to read (unknown sections in the file are ignored).
    static HighScores parse(const config::IniDocument& doc, std::span<const std::string_view> modes);

private:
    std::map<std::string, ModeTables, std::less<>> modes_;
    std::string today_;
    std::uint32_t nextToken_ = 1;
};

} // namespace spindle::arcade
