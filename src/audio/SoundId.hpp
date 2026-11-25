#pragma once

#include <array>
#include <string_view>

namespace spindle::audio {

enum class SoundId { NeedleHit, Fail, Clear, Coin, Join, MenuMove, MenuSelect, Winner, RoundStart, Count };
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
}};

} // namespace spindle::audio
