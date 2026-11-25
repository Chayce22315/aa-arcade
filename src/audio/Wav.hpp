#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace spindle::audio {

struct PcmClip {
    int sampleRate = 0;
    std::vector<float> samples; // mono, -1..1
};

// Parses a RIFF/WAVE file with PCM 8/16-bit or IEEE float 32-bit samples, 1 or 2 channels
// (stereo is mixed down). Every chunk size is bounds-checked; malformed files return nullopt.
std::optional<PcmClip> parseWav(std::span<const unsigned char> bytes, std::string* error = nullptr);

} // namespace spindle::audio
