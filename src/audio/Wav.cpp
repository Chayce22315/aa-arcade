#include "audio/Wav.hpp"

#include <cstdint>
#include <cstring>

namespace spindle::audio {
namespace {

std::uint32_t le32(const unsigned char* p) {
    return std::uint32_t{p[0]} | (std::uint32_t{p[1]} << 8) | (std::uint32_t{p[2]} << 16) | (std::uint32_t{p[3]} << 24);
}
std::uint16_t le16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }

bool fail(std::string* error, const char* msg) {
    if (error) *error = msg;
    return false;
}

} // namespace

std::optional<PcmClip> parseWav(std::span<const unsigned char> b, std::string* error) {
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0 || std::memcmp(b.data() + 8, "WAVE", 4) != 0) {
        fail(error, "not a RIFF/WAVE file");
        return std::nullopt;
    }
    std::uint16_t format = 0, channels = 0, bits = 0;
    std::uint32_t rate = 0;
    const unsigned char* data = nullptr;
    std::size_t dataSize = 0;
    std::size_t pos = 12;
    while (pos + 8 <= b.size()) {
        const unsigned char* chunk = b.data() + pos;
        const std::uint32_t size = le32(chunk + 4);
        if (size > b.size() - pos - 8) {
            fail(error, "chunk runs past the end of the file");
            return std::nullopt;
        }
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (size < 16) {
                fail(error, "fmt chunk too small");
                return std::nullopt;
            }
            format = le16(chunk + 8);
            channels = le16(chunk + 10);
            rate = le32(chunk + 12);
            bits = le16(chunk + 22);
            if (format == 0xFFFE && size >= 40) format = le16(chunk + 8 + 24); // WAVE_FORMAT_EXTENSIBLE subformat
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data = chunk + 8;
            dataSize = size;
        }
        pos += 8 + size + (size & 1u);
    }
    if (!data || channels == 0) {
        fail(error, "missing fmt or data chunk");
        return std::nullopt;
    }
    if (channels > 2 || rate < 8000 || rate > 192000) {
        fail(error, "unsupported channel count or sample rate");
        return std::nullopt;
    }
    const bool pcm8 = format == 1 && bits == 8;
    const bool pcm16 = format == 1 && bits == 16;
    const bool f32 = format == 3 && bits == 32;
    if (!pcm8 && !pcm16 && !f32) {
        fail(error, "unsupported sample format (need PCM 8/16-bit or float 32-bit)");
        return std::nullopt;
    }
    const std::size_t bytesPerSample = bits / 8u;
    const std::size_t frameBytes = bytesPerSample * channels;
    const std::size_t frames = dataSize / frameBytes;
    PcmClip clip;
    clip.sampleRate = static_cast<int>(rate);
    clip.samples.resize(frames);
    for (std::size_t f = 0; f < frames; ++f) {
        float sum = 0.0f;
        for (std::size_t c = 0; c < channels; ++c) {
            const unsigned char* s = data + f * frameBytes + c * bytesPerSample;
            float v = 0.0f;
            if (pcm8) {
                v = (static_cast<float>(s[0]) - 128.0f) / 128.0f;
            } else if (pcm16) {
                v = static_cast<float>(static_cast<std::int16_t>(le16(s))) / 32768.0f;
            } else {
                const std::uint32_t raw = le32(s);
                std::memcpy(&v, &raw, sizeof v);
                if (!(v == v)) v = 0.0f; // NaN guard
            }
            sum += v;
        }
        clip.samples[f] = sum / static_cast<float>(channels);
    }
    return clip;
}

} // namespace spindle::audio
