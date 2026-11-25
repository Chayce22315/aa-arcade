#include "players/PlayerManager.hpp"

#include <algorithm>

namespace spindle::players {

std::vector<gameplay::ParticipantSpec> Lineup::participants() const {
    std::vector<gameplay::ParticipantSpec> out;
    for (int s = 0; s < gameplay::kMaxStations; ++s) {
        const SeatKind k = seats[static_cast<std::size_t>(s)];
        if (k == SeatKind::Human) out.push_back({s, gameplay::ParticipantKind::Human});
        if (k == SeatKind::Bot) out.push_back({s, gameplay::ParticipantKind::Bot});
    }
    return out;
}

Lineup buildLineup(const std::array<bool, gameplay::kMaxStations>& joinedHumans, const BotFill& fill) {
    Lineup l;
    for (int s = 0; s < gameplay::kMaxStations; ++s) {
        if (joinedHumans[static_cast<std::size_t>(s)]) {
            l.seats[static_cast<std::size_t>(s)] = SeatKind::Human;
            ++l.humans;
        }
    }
    l.mode = l.humans >= 2 ? GameMode::Multiplayer : GameMode::Singleplayer;
    const int wanted = l.mode == GameMode::Singleplayer ? std::clamp(fill.singleplayerBots, 0, 3) : std::clamp(fill.multiplayerBots, 0, 2);
    int botsLeft = std::min(wanted, gameplay::kMaxStations - l.humans);
    for (int s = 0; s < gameplay::kMaxStations && botsLeft > 0; ++s) {
        if (l.seats[static_cast<std::size_t>(s)] == SeatKind::Empty) {
            l.seats[static_cast<std::size_t>(s)] = SeatKind::Bot;
            ++l.bots;
            --botsLeft;
        }
    }
    return l;
}

Lineup demoLineup() {
    Lineup l;
    l.mode = GameMode::Demo;
    l.seats.fill(SeatKind::Bot);
    l.bots = gameplay::kMaxStations;
    return l;
}

const char* gameModeName(GameMode mode) {
    switch (mode) {
    case GameMode::Singleplayer: return "singleplayer";
    case GameMode::Multiplayer: return "multiplayer";
    case GameMode::Demo: return "demo";
    }
    return "singleplayer";
}

} // namespace spindle::players
