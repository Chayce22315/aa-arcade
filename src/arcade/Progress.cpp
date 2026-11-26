#include "arcade/Progress.hpp"

#include <algorithm>
#include <charconv>
#include <format>

namespace spindle::arcade {
namespace {

constexpr int kRoundTen = 10;
constexpr int kRoundTwentyFive = 25;
constexpr int kCloseShaves = 5;
constexpr int kHotHands = 3;
constexpr int kRoyalty = 5;
constexpr int kRings = 5;
constexpr std::int64_t kTenThousand = 10'000;
constexpr std::int64_t kSixFigures = 100'000;
constexpr std::uint64_t kThousandNeedles = 1'000;

constexpr std::uint32_t allModesMask() {
    std::uint32_t m = 0;
    for (std::size_t i = 0; i < gameplay::kModeCount; ++i) m |= 1u << i;
    return m;
}

template <class T>
T readNumber(const config::IniDocument& doc, std::string_view section, std::string_view key) {
    auto v = doc.get(section, key);
    if (!v) return T{};
    const std::string_view t = config::trim(*v);
    T out{};
    auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), out);
    return ec == std::errc{} && p == t.data() + t.size() ? out : T{};
}

} // namespace

AchievementInfo achievementInfo(Achievement a) {
    switch (a) {
    case Achievement::FirstPin: return {"first_pin", "first pin", "place a needle", std::nullopt};
    case Achievement::Round10: return {"round_10", "round 10", "reach round 10", std::nullopt};
    case Achievement::Round25: return {"round_25", "round 25", "reach round 25", Look::Trails};
    case Achievement::CloseShaves: return {"close_shaves", "close shaves", "5 close calls in one game", std::nullopt};
    case Achievement::HotHands: return {"hot_hands", "hot hands", "catch fire 3 times in one game", Look::BlueFlames};
    case Achievement::Crowned: return {"crowned", "crowned", "take the crown", std::nullopt};
    case Achievement::Royalty: return {"royalty", "royalty", "take the crown 5 times in one game", Look::RoyalCrown};
    case Achievement::RingCollector: return {"ring_collector", "ring collector", "take 5 rings in one game", std::nullopt};
    case Achievement::TenThousand: return {"ten_thousand", "ten thousand", "score 10000 in one game", std::nullopt};
    case Achievement::SixFigures: return {"six_figures", "six figures", "score 100000 in one game", std::nullopt};
    case Achievement::OnTheBoard: return {"on_the_board", "on the board", "shoot your initials into a high score table", std::nullopt};
    case Achievement::Flawless: return {"flawless", "flawless", "win a game against someone without losing a try", std::nullopt};
    case Achievement::Tourist: return {"tourist", "tourist", "finish a game in every mode", std::nullopt};
    case Achievement::ThousandNeedles: return {"thousand_needles", "thousand needles", "1000 needles placed on this machine", std::nullopt};
    case Achievement::Count: break;
    }
    return {"", "", "", std::nullopt};
}

std::string_view lookKey(Look l) {
    switch (l) {
    case Look::BlueFlames: return "blue_flames";
    case Look::RoyalCrown: return "royal_crown";
    case Look::Trails: return "trails";
    case Look::Count: break;
    }
    return "";
}

std::string_view lookName(Look l) {
    switch (l) {
    case Look::BlueFlames: return "blue flames";
    case Look::RoyalCrown: return "royal crown";
    case Look::Trails: return "comet trails";
    case Look::Count: break;
    }
    return "";
}

bool Progress::award(Achievement a, std::vector<Achievement>& out) {
    auto& e = earned_[static_cast<std::size_t>(a)];
    if (e) return false;
    e = true;
    out.push_back(a);
    if (const auto look = achievementInfo(a).unlocks) looks_[static_cast<std::size_t>(*look)] = true; // a new look starts on
    return true;
}

std::vector<Achievement> Progress::update(const StationProgress& s) {
    std::vector<Achievement> out;
    if (s.needles >= 1) award(Achievement::FirstPin, out);
    if (s.round >= kRoundTen) award(Achievement::Round10, out);
    if (s.round >= kRoundTwentyFive) award(Achievement::Round25, out);
    if (s.closeCalls >= kCloseShaves) award(Achievement::CloseShaves, out);
    if (s.ignitions >= kHotHands) award(Achievement::HotHands, out);
    if (s.crowns >= 1) award(Achievement::Crowned, out);
    if (s.crowns >= kRoyalty) award(Achievement::Royalty, out);
    if (s.rings >= kRings) award(Achievement::RingCollector, out);
    if (s.score >= kTenThousand) award(Achievement::TenThousand, out);
    if (s.score >= kSixFigures) award(Achievement::SixFigures, out);
    if (s.finished && s.won && s.flawless) award(Achievement::Flawless, out);
    if (stats_.needles + static_cast<std::uint64_t>(std::max(0, s.needles)) >= kThousandNeedles) award(Achievement::ThousandNeedles, out);
    return out;
}

std::vector<Achievement> Progress::addMatch(const StationProgress& s, int roundsCleared) {
    stats_.needles += static_cast<std::uint64_t>(std::max(0, s.needles));
    stats_.roundsCleared += static_cast<std::uint64_t>(std::max(0, roundsCleared));
    stats_.closeCalls += static_cast<std::uint64_t>(std::max(0, s.closeCalls));
    stats_.fires += static_cast<std::uint64_t>(std::max(0, s.ignitions));
    stats_.crowns += static_cast<std::uint64_t>(std::max(0, s.crowns));
    stats_.rings += static_cast<std::uint64_t>(std::max(0, s.rings));
    stats_.bestScore = std::max(stats_.bestScore, s.score);
    stats_.bestRound = std::max(stats_.bestRound, s.round);
    const auto mode = static_cast<std::size_t>(s.mode);
    if (mode < gameplay::kModeCount) stats_.modesFinished |= 1u << mode;
    std::vector<Achievement> out;
    if (stats_.needles >= kThousandNeedles) award(Achievement::ThousandNeedles, out);
    if ((stats_.modesFinished & allModesMask()) == allModesMask()) award(Achievement::Tourist, out);
    return out;
}

std::vector<Achievement> Progress::addHighScore() {
    ++stats_.highScores;
    std::vector<Achievement> out;
    award(Achievement::OnTheBoard, out);
    return out;
}

int Progress::earnedCount() const { return static_cast<int>(std::count(earned_.begin(), earned_.end(), true)); }

bool Progress::unlocked(Look l) const {
    for (std::size_t i = 0; i < kAchievementCount; ++i)
        if (earned_[i] && achievementInfo(static_cast<Achievement>(i)).unlocks == l) return true;
    return false;
}

void Progress::setLook(Look l, bool on) { looks_[static_cast<std::size_t>(l)] = on && unlocked(l); }

std::string Progress::serialize() const {
    std::string out = "; spindle progress: lifetime stats and achievements (written by the game; reset it from the service menu)\n[Stats]\n";
    out += std::format("matches = {}\nneedles = {}\nrounds_cleared = {}\nclose_calls = {}\nfires = {}\ncrowns = {}\nrings = {}\n", stats_.matches,
                       stats_.needles, stats_.roundsCleared, stats_.closeCalls, stats_.fires, stats_.crowns, stats_.rings);
    out += std::format("high_scores = {}\nbest_score = {}\nbest_round = {}\nmodes_finished = ", stats_.highScores, stats_.bestScore, stats_.bestRound);
    bool first = true;
    for (std::size_t i = 0; i < gameplay::kModeCount; ++i)
        if (stats_.modesFinished & (1u << i)) {
            out += std::format("{}{}", first ? "" : ", ", gameplay::modeKey(static_cast<gameplay::ModeId>(i)));
            first = false;
        }
    out += "\n\n[Achievements]\n";
    for (std::size_t i = 0; i < kAchievementCount; ++i)
        out += std::format("{} = {}\n", achievementInfo(static_cast<Achievement>(i)).key, earned_[i] ? 1 : 0);
    out += "\n[Looks]\n";
    for (std::size_t i = 0; i < kLookCount; ++i) out += std::format("{} = {}\n", lookKey(static_cast<Look>(i)), looks_[i] ? 1 : 0);
    return out;
}

Progress Progress::parse(const config::IniDocument& doc) {
    Progress p;
    auto& s = p.stats_;
    s.matches = readNumber<std::uint64_t>(doc, "Stats", "matches");
    s.needles = readNumber<std::uint64_t>(doc, "Stats", "needles");
    s.roundsCleared = readNumber<std::uint64_t>(doc, "Stats", "rounds_cleared");
    s.closeCalls = readNumber<std::uint64_t>(doc, "Stats", "close_calls");
    s.fires = readNumber<std::uint64_t>(doc, "Stats", "fires");
    s.crowns = readNumber<std::uint64_t>(doc, "Stats", "crowns");
    s.rings = readNumber<std::uint64_t>(doc, "Stats", "rings");
    s.highScores = readNumber<std::uint64_t>(doc, "Stats", "high_scores");
    s.bestScore = std::max<std::int64_t>(0, readNumber<std::int64_t>(doc, "Stats", "best_score"));
    s.bestRound = std::max(0, readNumber<int>(doc, "Stats", "best_round"));
    if (auto modes = doc.get("Stats", "modes_finished")) {
        std::string_view rest = *modes;
        while (!rest.empty()) {
            const std::size_t comma = rest.find(',');
            const std::string_view token = config::trim(rest.substr(0, comma));
            if (auto m = gameplay::modeFromKey(token)) s.modesFinished |= 1u << static_cast<std::size_t>(*m);
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
    }
    for (std::size_t i = 0; i < kAchievementCount; ++i)
        p.earned_[i] = readNumber<int>(doc, "Achievements", achievementInfo(static_cast<Achievement>(i)).key) == 1;
    for (std::size_t i = 0; i < kLookCount; ++i) {
        const auto l = static_cast<Look>(i);
        p.looks_[i] = readNumber<int>(doc, "Looks", lookKey(l)) == 1 && p.unlocked(l);
    }
    return p;
}

} // namespace spindle::arcade
