#include "arcade/Bookkeeping.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>

namespace spindle::arcade {
namespace {

std::uint64_t readCounter(const config::IniDocument& doc, const char* key) {
    auto v = doc.get("Counters", key);
    if (!v) return 0;
    std::uint64_t out = 0;
    const std::string_view s = config::trim(*v);
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    if (ec != std::errc{} || p != s.data() + s.size()) return 0;
    return out;
}

} // namespace

void Bookkeeping::addWinner(const WinnerRecord& w) {
    recentWinners.insert(recentWinners.begin(), w);
    if (recentWinners.size() > kMaxRecent) recentWinners.resize(kMaxRecent);
}

void Bookkeeping::clearCounters() {
    std::string seen = std::move(seenVersion);
    *this = Bookkeeping{};
    seenVersion = std::move(seen);
}

std::string Bookkeeping::serialize() const {
    std::string out = "; spindle bookkeeping (written by the game; clear it from the service menu)\n[Counters]\n";
    out += std::format("coins = {}\nservice_credits = {}\ngames_singleplayer = {}\ngames_multiplayer = {}\n", coins,
                       serviceCredits, gamesSingleplayer, gamesMultiplayer);
    out += std::format("human_wins = {}\nbot_wins = {}\nplay_seconds = {}\n", humanWins, botWins, playSeconds);
    out += "\n[RecentWinners]\n";
    for (std::size_t i = 0; i < recentWinners.size(); ++i) {
        const WinnerRecord& w = recentWinners[i];
        out += std::format("winner{} = {},{},{}\n", i + 1, w.station + 1, w.bot ? "bot" : "human", w.round);
    }
    if (!seenVersion.empty()) out += std::format("\n[Machine]\nseen_version = {}\n", seenVersion);
    return out;
}

Bookkeeping Bookkeeping::parse(const config::IniDocument& doc) {
    Bookkeeping b;
    b.coins = readCounter(doc, "coins");
    b.serviceCredits = readCounter(doc, "service_credits");
    b.gamesSingleplayer = readCounter(doc, "games_singleplayer");
    b.gamesMultiplayer = readCounter(doc, "games_multiplayer");
    b.humanWins = readCounter(doc, "human_wins");
    b.botWins = readCounter(doc, "bot_wins");
    b.playSeconds = readCounter(doc, "play_seconds");
    if (auto v = doc.get("Machine", "seen_version")) {
        const std::string_view t = config::trim(*v);
        // A version string only: digits, dots and letters, at most 32 characters.
        if (t.size() <= 32 && std::all_of(t.begin(), t.end(), [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '.' || ch == '-'; }))
            b.seenVersion = std::string(t);
    }
    for (std::size_t i = 0; i < kMaxRecent; ++i) {
        auto v = doc.get("RecentWinners", std::format("winner{}", i + 1));
        if (!v) break;
        const std::string& s = *v;
        const auto c1 = s.find(',');
        const auto c2 = c1 == std::string::npos ? std::string::npos : s.find(',', c1 + 1);
        if (c2 == std::string::npos) break;
        int station = 0, round = 0;
        const std::string_view st = config::trim(std::string_view(s).substr(0, c1));
        const std::string_view kind = config::trim(std::string_view(s).substr(c1 + 1, c2 - c1 - 1));
        const std::string_view rd = config::trim(std::string_view(s).substr(c2 + 1));
        auto r1 = std::from_chars(st.data(), st.data() + st.size(), station);
        auto r2 = std::from_chars(rd.data(), rd.data() + rd.size(), round);
        if (r1.ec != std::errc{} || r2.ec != std::errc{} || station < 1 || station > 4 || round < 1) break;
        if (kind != "bot" && kind != "human") break;
        b.recentWinners.push_back({station - 1, kind == "bot", round});
    }
    return b;
}

} // namespace spindle::arcade
