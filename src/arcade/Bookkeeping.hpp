#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config/IniDocument.hpp"

namespace spindle::arcade {

struct WinnerRecord {
    int station = 0; // 0-based
    bool bot = false;
    int round = 0;
    bool operator==(const WinnerRecord&) const = default;
};

// Operator counters, persisted to bookkeeping.ini (atomic writes).
struct Bookkeeping {
    std::uint64_t coins = 0;
    std::uint64_t serviceCredits = 0;
    std::uint64_t gamesSingleplayer = 0;
    std::uint64_t gamesMultiplayer = 0;
    std::uint64_t humanWins = 0;
    std::uint64_t botWins = 0;
    std::uint64_t playSeconds = 0;
    std::vector<WinnerRecord> recentWinners; // newest first, at most kMaxRecent

    static constexpr std::size_t kMaxRecent = 5;

    void addWinner(const WinnerRecord& w);
    void clearCounters();

    std::string serialize() const;
    static Bookkeeping parse(const config::IniDocument& doc);
    bool operator==(const Bookkeeping&) const = default;
};

} // namespace spindle::arcade
