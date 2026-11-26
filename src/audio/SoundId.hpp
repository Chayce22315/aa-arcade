#pragma once

#include <array>
#include <string_view>

namespace spindle::audio {

enum class SoundId {
    NeedleHit,
    Fail,
    Clear,
    Coin,
    Join,
    MenuMove,
    MenuSelect,
    Winner,
    RoundStart,
    Powerup,
    Shield,
    Ignite,
    Burn,
    TallyTick1, // digits landing on the round end screen, rising
    TallyTick2,
    TallyTick3,
    TallyTick4,
    TallyDone,
    Crown,
    LetterShot,
    InitialsDone,
    Count
};
enum class SoundCategory { Sfx, Ui };

struct SoundInfo {
    SoundId id;
    std::string_view file; // embedded path
    SoundCategory category;
    std::string_view label;
};

inline constexpr std::array<SoundInfo, static_cast<std::size_t>(SoundId::Count)> kSounds{{
    {SoundId::NeedleHit, "audio/needle_hit.wav", SoundCategory::Sfx, "needle hit"},
    {SoundId::Fail, "audio/fail.wav", SoundCategory::Sfx, "fail"},
    {SoundId::Clear, "audio/clear.wav", SoundCategory::Sfx, "clear"},
    {SoundId::Coin, "audio/coin.wav", SoundCategory::Ui, "coin"},
    {SoundId::Join, "audio/join.wav", SoundCategory::Ui, "join"},
    {SoundId::MenuMove, "audio/menu_move.wav", SoundCategory::Ui, "menu move"},
    {SoundId::MenuSelect, "audio/menu_select.wav", SoundCategory::Ui, "menu select"},
    {SoundId::Winner, "audio/winner.wav", SoundCategory::Sfx, "winner"},
    {SoundId::RoundStart, "audio/round_start.wav", SoundCategory::Sfx, "round start"},
    {SoundId::Powerup, "audio/powerup.wav", SoundCategory::Sfx, "powerup"},
    {SoundId::Shield, "audio/shield.wav", SoundCategory::Sfx, "shield"},
    {SoundId::Ignite, "audio/ignite.wav", SoundCategory::Sfx, "ignite"},
    {SoundId::Burn, "audio/burn.wav", SoundCategory::Sfx, "burn"},
    {SoundId::TallyTick1, "audio/tally_tick_1.wav", SoundCategory::Ui, "tally tick 1"},
    {SoundId::TallyTick2, "audio/tally_tick_2.wav", SoundCategory::Ui, "tally tick 2"},
    {SoundId::TallyTick3, "audio/tally_tick_3.wav", SoundCategory::Ui, "tally tick 3"},
    {SoundId::TallyTick4, "audio/tally_tick_4.wav", SoundCategory::Ui, "tally tick 4"},
    {SoundId::TallyDone, "audio/tally_done.wav", SoundCategory::Ui, "tally done"},
    {SoundId::Crown, "audio/crown.wav", SoundCategory::Sfx, "crown"},
    {SoundId::LetterShot, "audio/letter_shot.wav", SoundCategory::Ui, "letter shot"},
    {SoundId::InitialsDone, "audio/initials_done.wav", SoundCategory::Ui, "initials saved"},
}};

} // namespace spindle::audio
