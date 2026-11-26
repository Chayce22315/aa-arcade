#include "arcade/HighScores.hpp"

#include <algorithm>
#include <charconv>
#include <format>

#include "gameplay/Scoring.hpp"

namespace spindle::arcade {
namespace {

const ModeTables kEmpty{};

bool parseInt64(std::string_view s, std::int64_t& out) {
    s = config::trim(s);
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && p == s.data() + s.size();
}

// "NAME, score, round, date" -> entry, or nothing when any field is unusable.
std::optional<ScoreEntry> parseRow(std::string_view v) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t c = v.find(',', start);
        parts.push_back(config::trim(v.substr(start, c == std::string_view::npos ? std::string_view::npos : c - start)));
        if (c == std::string_view::npos) break;
        start = c + 1;
    }
    if (parts.size() != 4) return std::nullopt;
    ScoreEntry e;
    e.name = sanitizeName(parts[0]);
    std::int64_t score = 0, round = 0;
    if (e.name.empty() || !parseInt64(parts[1], score) || !parseInt64(parts[2], round)) return std::nullopt;
    if (score <= 0 || score > gameplay::scoring::kMaxScore || round < 1 || round > 100000) return std::nullopt;
    e.score = score;
    e.round = static_cast<int>(round);
    if (!parts[3].empty() && !isIsoDate(parts[3])) return std::nullopt;
    e.date = std::string(parts[3]);
    return e;
}

void readTable(const config::IniDocument& doc, const std::string& section, HighScoreTable& table) {
    for (std::size_t i = 1; i <= kTableSize; ++i) {
        const auto v = doc.get(section, std::to_string(i));
        if (!v) continue;
        if (auto e = parseRow(*v)) table.insert(std::move(*e));
    }
}

void writeTable(std::string& out, const std::string& section, const HighScoreTable& table) {
    if (table.empty()) return;
    out += std::format("\n[{}]\n", section);
    for (std::size_t i = 0; i < table.rows().size(); ++i) {
        const ScoreEntry& e = table.rows()[i];
        out += std::format("{} = {}, {}, {}, {}\n", i + 1, e.name, e.score, e.round, e.date);
    }
}

} // namespace

std::string sanitizeName(std::string_view name) {
    std::string out;
    for (char ch : name) {
        if (out.size() >= kMaxNameLength) break;
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) out += ch;
    }
    return out;
}

bool isIsoDate(std::string_view s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (std::size_t i : {0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u})
        if (s[i] < '0' || s[i] > '9') return false;
    const int month = (s[5] - '0') * 10 + (s[6] - '0');
    const int day = (s[8] - '0') * 10 + (s[9] - '0');
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

bool HighScoreTable::qualifies(std::int64_t score) const {
    if (score <= 0) return false;
    if (rows_.size() < kTableSize) return true;
    // Some unreserved row with a lower score has to give way.
    return std::any_of(rows_.begin(), rows_.end(), [&](const ScoreEntry& r) { return r.token == 0 && r.score < score; });
}

std::optional<std::size_t> HighScoreTable::insert(ScoreEntry e) {
    if (!qualifies(e.score)) return std::nullopt;
    // Below every row with an equal or higher score: the earlier entry keeps its place.
    const auto at = std::find_if(rows_.begin(), rows_.end(), [&](const ScoreEntry& r) { return r.score < e.score; });
    std::size_t index = static_cast<std::size_t>(at - rows_.begin());
    rows_.insert(at, std::move(e));
    if (rows_.size() > kTableSize) {
        // The lowest row nobody is still typing a name for leaves.
        for (std::size_t i = rows_.size(); i-- > 0;) {
            if (rows_[i].token != 0) continue;
            rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(i));
            if (i < index) --index;
            break;
        }
    }
    return index;
}

bool HighScoreTable::rename(std::uint32_t token, std::string_view name) {
    bool found = false;
    for (ScoreEntry& r : rows_) {
        if (token == 0 || r.token != token) continue;
        const std::string clean = sanitizeName(name);
        if (!clean.empty()) r.name = clean;
        r.token = 0;
        found = true;
    }
    return found;
}

void HighScores::setToday(std::string_view date) {
    if (!isIsoDate(date) || date == today_) return;
    if (!today_.empty())
        for (auto& [mode, t] : modes_) t.today.clear();
    today_ = std::string(date);
}

const ModeTables& HighScores::tables(std::string_view mode) const {
    const auto it = modes_.find(mode);
    return it == modes_.end() ? kEmpty : it->second;
}

bool HighScores::anyEntries() const {
    return std::any_of(modes_.begin(), modes_.end(), [](const auto& m) { return !m.second.allTime.empty() || !m.second.today.empty(); });
}

bool HighScores::qualifies(std::string_view mode, std::int64_t score, bool todayOnly) const {
    const ModeTables& t = tables(mode);
    return t.today.qualifies(score) || (!todayOnly && t.allTime.qualifies(score));
}

std::uint32_t HighScores::reserve(std::string_view mode, std::int64_t score, int round, std::string_view fallbackName, bool todayOnly) {
    if (!qualifies(mode, score, todayOnly)) return 0;
    auto it = modes_.find(mode);
    if (it == modes_.end()) it = modes_.emplace(std::string(mode), ModeTables{}).first;
    ScoreEntry e;
    e.name = sanitizeName(fallbackName);
    if (e.name.empty()) e.name = "P";
    e.score = std::min(score, gameplay::scoring::kMaxScore);
    e.round = std::max(1, round);
    e.date = today_;
    e.token = nextToken_++;
    if (nextToken_ == 0) nextToken_ = 1;
    bool any = it->second.today.insert(e).has_value();
    if (!todayOnly) any = it->second.allTime.insert(e).has_value() || any;
    return any ? e.token : 0;
}

bool HighScores::finish(std::uint32_t token, std::string_view name) {
    if (token == 0) return false;
    bool found = false;
    for (auto& [mode, t] : modes_) {
        found = t.allTime.rename(token, name) || found;
        found = t.today.rename(token, name) || found;
    }
    return found;
}

std::optional<std::size_t> HighScores::rankOf(std::string_view mode, std::uint32_t token, bool today) const {
    const HighScoreTable& t = today ? tables(mode).today : tables(mode).allTime;
    for (std::size_t i = 0; i < t.rows().size(); ++i)
        if (token != 0 && t.rows()[i].token == token) return i;
    return std::nullopt;
}

void HighScores::clear(std::string_view mode) {
    if (auto it = modes_.find(mode); it != modes_.end()) {
        it->second.allTime.clear();
        it->second.today.clear();
    }
}

void HighScores::clearAll() {
    for (auto& [mode, t] : modes_) {
        t.allTime.clear();
        t.today.clear();
    }
}

std::string HighScores::serialize() const {
    std::string out = "; spindle high scores (written by the game; clear them from the service menu)\n[HighScores]\n";
    out += std::format("today = {}\n", today_);
    for (const auto& [mode, t] : modes_) {
        writeTable(out, mode, t.allTime);
        writeTable(out, mode + ".today", t.today);
    }
    return out;
}

HighScores HighScores::parse(const config::IniDocument& doc, std::span<const std::string_view> modes) {
    HighScores h;
    if (auto d = doc.get("HighScores", "today"); d && isIsoDate(config::trim(*d))) h.today_ = std::string(config::trim(*d));
    for (std::string_view mode : modes) {
        ModeTables t;
        readTable(doc, std::string(mode), t.allTime);
        readTable(doc, std::string(mode) + ".today", t.today);
        if (!t.allTime.empty() || !t.today.empty()) h.modes_.emplace(std::string(mode), std::move(t));
    }
    return h;
}

} // namespace spindle::arcade
