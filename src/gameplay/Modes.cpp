#include "gameplay/Modes.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "gameplay/Geometry.hpp"

namespace spindle::gameplay {

ModeRules modeRules(ModeId id) {
    ModeRules r;
    r.id = id;
    switch (id) {
    case ModeId::Battle: break;
    case ModeId::Marathon: r.end = MatchEnd::AllOut; break;
    case ModeId::Blitz:
        r.end = MatchEnd::Clock;
        r.knockouts = false;
        r.clockSeconds = kBlitzSeconds;
        r.failPenalty = true;
        break;
    case ModeId::Race:
        r.end = MatchEnd::FirstTo;
        r.knockouts = false;
        r.target = kRaceTarget;
        break;
    case ModeId::Crown:
        r.end = MatchEnd::Rounds;
        r.knockouts = false;
        r.target = kCrownRounds;
        break;
    case ModeId::Sabotage: r.junk = true; break;
    case ModeId::Teams: r.teams = true; break;
    case ModeId::Chaos: r.mutators = true; break;
    case ModeId::Daily:
        r.end = MatchEnd::AllOut;
        r.dailySeed = true;
        r.todayOnly = true;
        break;
    }
    return r;
}

std::string_view modeKey(ModeId id) {
    switch (id) {
    case ModeId::Battle: return "battle";
    case ModeId::Marathon: return "marathon";
    case ModeId::Blitz: return "blitz";
    case ModeId::Race: return "race";
    case ModeId::Crown: return "crown";
    case ModeId::Sabotage: return "sabotage";
    case ModeId::Teams: return "teams";
    case ModeId::Chaos: return "chaos";
    case ModeId::Daily: return "daily";
    }
    return "battle";
}

std::string_view modeName(ModeId id) { return modeKey(id); }

std::string_view modeBlurb(ModeId id) {
    switch (id) {
    case ModeId::Battle: return "last one standing wins";
    case ModeId::Marathon: return "play till everyone is out - best score wins";
    case ModeId::Blitz: return "90 seconds, no knock-outs - best score wins";
    case ModeId::Race: return "first to clear takes the round - first to 5";
    case ModeId::Crown: return "hold the crown at round ends - 8 rounds";
    case ModeId::Sabotage: return "clears and close calls send junk to rivals";
    case ModeId::Teams: return "P1 + P2 against P3 + P4, shared tries";
    case ModeId::Chaos: return "every round rolls a mutator";
    case ModeId::Daily: return "today's levels, same for everyone";
    }
    return "";
}

std::optional<ModeId> modeFromKey(std::string_view key) {
    for (ModeId m : kAllModes)
        if (modeKey(m) == key) return m;
    return std::nullopt;
}

std::string_view mutatorName(Mutator m) {
    switch (m) {
    case Mutator::None: return "";
    case Mutator::Blind: return "blind";
    case Mutator::Tiny: return "tiny balls";
    case Mutator::Big: return "big balls";
    case Mutator::Fast: return "fast";
    case Mutator::Extra: return "extra needles";
    }
    return "";
}

namespace {

std::uint32_t mix(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

} // namespace

Mutator rollMutator(std::uint32_t seed, int round) {
    constexpr Mutator kAll[] = {Mutator::Blind, Mutator::Tiny, Mutator::Big, Mutator::Fast, Mutator::Extra};
    const std::uint32_t h = mix(seed ^ mix(static_cast<std::uint32_t>(round) * 0x9e3779b9U));
    return kAll[h % 5U];
}

int addJunk(LevelDef& level, int count, const LevelLimits& limits, scripting::Pcg32& rng) {
    int added = 0;
    for (int k = 0; k < count; ++k) {
        bool placed = false;
        for (int attempt = 0; attempt < 64 && !placed; ++attempt) {
            const double angle = rng.nextDouble() * geom::kTwoPi - geom::kPi;
            LevelDef trial = level;
            trial.prepins.push_back(angle);
            if (validateLevel(trial, limits)) continue;
            level.prepins.push_back(angle);
            ++level.junk;
            placed = true;
        }
        if (!placed) break; // the circle is full enough
        ++added;
    }
    return added;
}

std::uint32_t dailySeed(std::string_view isoDate) {
    std::uint32_t h = 2166136261U;
    for (char ch : std::string_view("spindle-daily-")) h = (h ^ static_cast<unsigned char>(ch)) * 16777619U;
    for (char ch : isoDate) h = (h ^ static_cast<unsigned char>(ch)) * 16777619U;
    return h == 0 ? 1U : h;
}

int bestByTotals(std::span<const ScoreTotals> players) {
    if (players.empty()) return -1;
    const ScoreTotals* best = &players.front();
    for (const ScoreTotals& p : players) {
        if (&p == best) continue;
        const auto key = [](const ScoreTotals& t) { return std::make_tuple(t.points, t.score, t.placed, t.roundsCleared); };
        if (key(p) > key(*best) || (key(p) == key(*best) && p.id < best->id)) best = &p;
    }
    return best->id;
}

RoundDecision resolveEndless(std::span<const PlayerRecord> players, std::span<const RoundOutcome> outcomes,
                             std::span<const ScoreTotals> totals, int participants) {
    RoundDecision d;
    d.rule = RuleCase::Score;
    int alive = 0;
    for (const PlayerRecord& p : players) {
        const auto o = std::find_if(outcomes.begin(), outcomes.end(), [&](const RoundOutcome& r) { return r.id == p.id; });
        const bool failed = o != outcomes.end() && !o->cleared;
        if (failed) {
            d.lostTry.push_back(p.id);
            if (p.tries <= 1) {
                d.eliminated.push_back(p.id);
                continue;
            }
        }
        ++alive;
    }
    if (alive > 0) return d; // Continue
    if (participants <= 1) {
        d.kind = DecisionKind::NoWinner;
        return d;
    }
    d.kind = DecisionKind::Winner;
    d.winner = bestByTotals(totals);
    return d;
}

TeamDecision resolveTeamRound(std::array<int, 2> pools, std::span<const RoundOutcome> outcomes, std::span<const int> teamOf,
                              std::span<const ScoreTotals> totals) {
    TeamDecision t;
    t.decision.rule = RuleCase::Team;
    t.pools = pools;
    for (std::size_t i = 0; i < outcomes.size() && i < teamOf.size(); ++i) {
        const int team = teamOf[i];
        if (team < 0 || team > 1 || outcomes[i].cleared) continue;
        t.decision.lostTry.push_back(outcomes[i].id);
        t.pools[static_cast<std::size_t>(team)] = std::max(0, t.pools[static_cast<std::size_t>(team)] - 1);
    }
    const bool out0 = pools[0] > 0 && t.pools[0] == 0, out1 = pools[1] > 0 && t.pools[1] == 0;
    for (std::size_t i = 0; i < outcomes.size() && i < teamOf.size(); ++i)
        if ((teamOf[i] == 0 && out0) || (teamOf[i] == 1 && out1)) t.decision.eliminated.push_back(outcomes[i].id);
    const bool alive0 = t.pools[0] > 0, alive1 = t.pools[1] > 0;
    if (alive0 && alive1) return t;
    std::optional<int> team;
    if (alive0 != alive1) {
        team = alive0 ? 0 : 1;
    } else {
        // Both teams went out in the same round: the team with the better combined totals wins.
        ScoreTotals sum[2]{{0, 0, 0, 0, 0}, {1, 0, 0, 0, 0}};
        for (const ScoreTotals& p : totals) {
            const auto it = std::find_if(outcomes.begin(), outcomes.end(), [&](const RoundOutcome& o) { return o.id == p.id; });
            if (it == outcomes.end()) continue;
            const int tm = teamOf[static_cast<std::size_t>(it - outcomes.begin())];
            if (tm < 0 || tm > 1) continue;
            sum[tm].score += p.score;
            sum[tm].placed += p.placed;
            sum[tm].roundsCleared += p.roundsCleared;
        }
        team = bestByTotals(sum);
    }
    t.winningTeam = team;
    // The match winner station is the team's best scorer (bookkeeping and the results screen).
    std::vector<ScoreTotals> members;
    for (const ScoreTotals& p : totals) {
        const auto it = std::find_if(outcomes.begin(), outcomes.end(), [&](const RoundOutcome& o) { return o.id == p.id; });
        if (it != outcomes.end() && teamOf[static_cast<std::size_t>(it - outcomes.begin())] == *team) members.push_back(p);
    }
    t.decision.kind = DecisionKind::Winner;
    t.decision.winner = members.empty() ? -1 : bestByTotals(members);
    return t;
}

} // namespace spindle::gameplay
