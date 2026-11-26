#include "arcade/Replay.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <format>

#include "config/IniDocument.hpp"

namespace spindle::arcade {
namespace {

// Doubles are stored as their IEEE-754 bits so they come back exactly (no decimal rounding, no locale).
std::string bits(double v) { return std::format("{:016x}", std::bit_cast<std::uint64_t>(v)); }

template <class T>
bool number(std::string_view text, T& out, int base = 10) {
    text = config::trim(text);
    auto [p, ec] = std::from_chars(text.data(), text.data() + text.size(), out, base);
    return ec == std::errc{} && p == text.data() + text.size();
}

bool real(std::string_view text, double& out) {
    std::uint64_t b = 0;
    if (!number(text, b, 16)) return false;
    out = std::bit_cast<double>(b);
    return std::isfinite(out);
}

std::string_view tallyName(gameplay::scoring::TallyStyle t) {
    switch (t) {
    case gameplay::scoring::TallyStyle::Full: return "full";
    case gameplay::scoring::TallyStyle::Short: return "short";
    case gameplay::scoring::TallyStyle::Off: return "off";
    }
    return "full";
}

} // namespace

std::string Replay::serialize() const {
    std::string out = "; spindle replay (written by the game)\n";
    out += std::format("build = {}\nmode = {}\nname = {}\nstation = {}\nscore = {}\n", build, gameplay::modeKey(mode), name, station, score);
    const auto& c = config;
    out += std::format("seed = {}\ntries = {}\nstart_round = {}\nround_time_limit = {}\nready_delay = {}\nresolve_delay = {}\n", c.seed, c.tries,
                       c.startRound, bits(c.roundTimeLimit), bits(c.readyDelay), bits(c.resolveDelay));
    out += std::format("rotation_scale = {}\nneedle_scale = {}\npowerups = {}\nfire_meter = {}\nfire_combo = {}\nfire_seconds = {}\n",
                       bits(c.rotationScale), bits(c.needleScale), gameplay::formatPowerupList(c.powerups), c.fireMeter ? 1 : 0, c.fireCombo,
                       bits(c.fireSeconds));
    out += std::format("fire_chain_window = {}\ntally = {}\n", bits(c.fireChainWindow), tallyName(c.tally));
    out += "seats = ";
    for (std::size_t i = 0; i < participants.size(); ++i)
        out += std::format("{}{}:{}", i ? "," : "", participants[i].station, participants[i].kind == gameplay::ParticipantKind::Bot ? "bot" : "human");
    out += std::format("\nscores = {},{},{},{}\nend_tick = {}\npresses = {}\n", finalScores[0], finalScores[1], finalScores[2], finalScores[3], endTick,
                       presses.size());
    for (const ReplayPress& p : presses) out += std::format("{} {} {}\n", p.tick, p.station, p.count);
    return out;
}

std::optional<Replay> Replay::parse(std::string_view text, std::string* error) {
    const auto fail = [error](std::string why) -> std::optional<Replay> {
        if (error) *error = std::move(why);
        return std::nullopt;
    };
    Replay r;
    std::size_t expected = 0;
    bool havePresses = false, haveMode = false, haveEnd = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = config::trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line.front() == ';') continue;
        if (havePresses) { // "tick station count"
            ReplayPress p;
            const std::size_t a = line.find(' '), b = a == std::string_view::npos ? a : line.find(' ', a + 1);
            if (b == std::string_view::npos || !number(line.substr(0, a), p.tick) || !number(line.substr(a + 1, b - a - 1), p.station) ||
                !number(line.substr(b + 1), p.count) || p.station < 0 || p.station >= gameplay::kMaxStations || p.count < 0 || p.count > 64)
                return fail(std::format("bad press line '{}'", line));
            if (!r.presses.empty() && p.tick < r.presses.back().tick) return fail("presses out of order");
            r.presses.push_back(p);
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) return fail(std::format("bad line '{}'", line));
        const std::string_view key = config::trim(line.substr(0, eq)), value = config::trim(line.substr(eq + 1));
        bool ok = true;
        auto& c = r.config;
        if (key == "build") r.build = std::string(value);
        else if (key == "mode") {
            const auto m = gameplay::modeFromKey(value);
            ok = m.has_value();
            if (m) r.mode = *m;
            haveMode = ok;
        } else if (key == "name") r.name = std::string(value.substr(0, 12));
        else if (key == "station") ok = number(value, r.station) && r.station >= -1 && r.station < gameplay::kMaxStations;
        else if (key == "score") ok = number(value, r.score);
        else if (key == "seed") ok = number(value, c.seed);
        else if (key == "tries") ok = number(value, c.tries) && c.tries >= 1 && c.tries <= 99;
        else if (key == "start_round") ok = number(value, c.startRound) && c.startRound >= 1;
        else if (key == "round_time_limit") ok = real(value, c.roundTimeLimit);
        else if (key == "ready_delay") ok = real(value, c.readyDelay);
        else if (key == "resolve_delay") ok = real(value, c.resolveDelay);
        else if (key == "rotation_scale") ok = real(value, c.rotationScale);
        else if (key == "needle_scale") ok = real(value, c.needleScale);
        else if (key == "powerups") c.powerups = gameplay::parsePowerupList(value);
        else if (key == "fire_meter") c.fireMeter = value == "1";
        else if (key == "fire_combo") ok = number(value, c.fireCombo) && c.fireCombo >= 1;
        else if (key == "fire_seconds") ok = real(value, c.fireSeconds);
        else if (key == "fire_chain_window") ok = real(value, c.fireChainWindow);
        else if (key == "tally") {
            if (value == "full") c.tally = gameplay::scoring::TallyStyle::Full;
            else if (value == "short") c.tally = gameplay::scoring::TallyStyle::Short;
            else if (value == "off") c.tally = gameplay::scoring::TallyStyle::Off;
            else ok = false;
        } else if (key == "seats") {
            std::string_view rest = value;
            while (ok && !rest.empty()) {
                const std::size_t comma = rest.find(',');
                const std::string_view seat = config::trim(rest.substr(0, comma));
                const std::size_t colon = seat.find(':');
                gameplay::ParticipantSpec spec;
                ok = colon != std::string_view::npos && number(seat.substr(0, colon), spec.station) && spec.station >= 0 &&
                     spec.station < gameplay::kMaxStations;
                if (ok) {
                    const std::string_view kind = seat.substr(colon + 1);
                    ok = kind == "bot" || kind == "human";
                    spec.kind = kind == "bot" ? gameplay::ParticipantKind::Bot : gameplay::ParticipantKind::Human;
                    r.participants.push_back(spec);
                }
                if (comma == std::string_view::npos) break;
                rest.remove_prefix(comma + 1);
            }
        } else if (key == "scores") {
            std::string_view rest = value;
            for (std::size_t i = 0; ok && i < r.finalScores.size(); ++i) {
                const std::size_t comma = rest.find(',');
                ok = number(rest.substr(0, comma), r.finalScores[i]);
                if (comma == std::string_view::npos) {
                    ok = ok && i + 1 == r.finalScores.size();
                    break;
                }
                rest.remove_prefix(comma + 1);
            }
        } else if (key == "end_tick") {
            ok = number(value, r.endTick);
            haveEnd = ok;
        } else if (key == "presses") {
            ok = number(value, expected) && expected <= ReplayRecorder::kMaxPresses;
            havePresses = ok;
        }
        // Unknown keys are ignored (a newer build may add some; its build id will not match anyway).
        if (!ok) return fail(std::format("bad value for {}: '{}'", key, value));
    }
    if (!haveMode || !haveEnd || !havePresses) return fail("incomplete replay");
    if (r.presses.size() != expected) return fail(std::format("{} presses, expected {}", r.presses.size(), expected));
    if (r.participants.empty() || r.participants.size() > static_cast<std::size_t>(gameplay::kMaxStations)) return fail("bad line-up");
    r.config.mode = gameplay::modeRules(r.mode);
    return r;
}

void ReplayRecorder::begin(const gameplay::MatchConfig& config, const std::vector<gameplay::ParticipantSpec>& participants, gameplay::ModeId mode,
                           std::string build) {
    *this = ReplayRecorder{};
    replay_.config = config;
    replay_.participants = participants;
    replay_.mode = mode;
    replay_.build = std::move(build);
    active_ = true;
}

void ReplayRecorder::record(std::uint64_t tick, const std::array<int, gameplay::kMaxStations>& fire) {
    if (!active_ || finished_) return;
    for (int s = 0; s < gameplay::kMaxStations; ++s) {
        const int n = fire[static_cast<std::size_t>(s)];
        if (n <= 0) continue;
        if (replay_.presses.size() >= kMaxPresses) {
            overflow_ = true;
            return;
        }
        replay_.presses.push_back({tick, s, std::min(n, 64)});
    }
}

void ReplayRecorder::finish(const gameplay::Match& match) {
    if (!active_) return;
    active_ = false;
    if (overflow_) return;
    for (int s = 0; s < gameplay::kMaxStations; ++s)
        if (const auto& p = match.participant(s)) replay_.finalScores[static_cast<std::size_t>(s)] = p->score;
    replay_.endTick = match.tickCount();
    finished_ = true;
}

std::array<int, gameplay::kMaxStations> ReplayPlayer::presses(std::uint64_t tick) {
    std::array<int, gameplay::kMaxStations> out{};
    const auto& list = replay_->presses;
    while (next_ < list.size() && list[next_].tick < tick) ++next_; // never stalls on a stale entry
    while (next_ < list.size() && list[next_].tick == tick) {
        out[static_cast<std::size_t>(list[next_].station)] += list[next_].count;
        ++next_;
    }
    return out;
}

std::array<std::int64_t, gameplay::kMaxStations> runReplay(const Replay& replay, gameplay::LevelProvider& levels) {
    gameplay::Match m(replay.config, replay.participants, levels);
    ReplayPlayer player(replay);
    while (!m.over() && !player.done(m.tickCount())) m.tick(player.presses(m.tickCount()));
    std::array<std::int64_t, gameplay::kMaxStations> out{};
    for (int s = 0; s < gameplay::kMaxStations; ++s)
        if (const auto& p = m.participant(s)) out[static_cast<std::size_t>(s)] = p->score;
    return out;
}

} // namespace spindle::arcade
