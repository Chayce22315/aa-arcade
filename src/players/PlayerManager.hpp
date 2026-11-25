#pragma once

#include <array>
#include <vector>

#include "gameplay/Match.hpp"

namespace spindle::players {

enum class GameMode { Singleplayer, Multiplayer, Demo };

enum class SeatKind { Empty, Human, Bot };

struct Lineup {
    GameMode mode = GameMode::Singleplayer;
    std::array<SeatKind, gameplay::kMaxStations> seats{};
    int humans = 0;
    int bots = 0;

    std::vector<gameplay::ParticipantSpec> participants() const;
};

struct BotFill {
    int singleplayerBots = 3; // 0..3
    int multiplayerBots = 0;  // 0..2
};

// Mode rule: exactly one human = singleplayer, two or more = multiplayer. Bots take the lowest free
// stations, capped so that humans + bots <= 4. Stations keep their physical positions.
Lineup buildLineup(const std::array<bool, gameplay::kMaxStations>& joinedHumans, const BotFill& fill);

// Attract demo: every station is a bot.
Lineup demoLineup();

const char* gameModeName(GameMode mode);

} // namespace spindle::players
