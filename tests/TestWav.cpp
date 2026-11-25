#include <cstdint>
#include <cstring>
#include <vector>

#include "Test.hpp"
#include "assets/EmbeddedAssets.hpp"
#include "audio/SoundId.hpp"
#include "audio/Wav.hpp"

using namespace spindle;

namespace {
std::vector<unsigned char> makeWav(std::uint16_t channels, std::uint32_t rate, const std::vector<std::int16_t>& samples) {
    std::vector<unsigned char> b;
    auto put32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<unsigned char>(v >> (8 * i))); };
    auto put16 = [&](std::uint16_t v) { b.push_back(static_cast<unsigned char>(v)); b.push_back(static_cast<unsigned char>(v >> 8)); };
    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * 2);
    b.insert(b.end(), {'R', 'I', 'F', 'F'});
    put32(36 + dataBytes);
    b.insert(b.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(16);
    put16(1);
    put16(channels);
    put32(rate);
    put32(rate * channels * 2);
    put16(static_cast<std::uint16_t>(channels * 2));
    put16(16);
    b.insert(b.end(), {'d', 'a', 't', 'a'});
    put32(dataBytes);
    for (auto s : samples) put16(static_cast<std::uint16_t>(s));
    return b;
}
} // namespace

TEST_CASE(wav_parses_mono_and_mixes_stereo) {
    auto mono = audio::parseWav(makeWav(1, 44100, {0, 16384, -16384, 32767}));
    REQUIRE(mono.has_value());
    CHECK_EQ(mono->sampleRate, 44100);
    CHECK_EQ(mono->samples.size(), std::size_t{4});
    CHECK_NEAR(mono->samples[1], 0.5, 1e-6);
    auto stereo = audio::parseWav(makeWav(2, 22050, {16384, -16384, 32767, 32767}));
    REQUIRE(stereo.has_value());
    CHECK_EQ(stereo->samples.size(), std::size_t{2});
    CHECK_NEAR(stereo->samples[0], 0.0, 1e-6);
}

TEST_CASE(wav_rejects_malformed_files) {
    std::string err;
    CHECK(!audio::parseWav(std::vector<unsigned char>{1, 2, 3}, &err));
    auto truncated = makeWav(1, 44100, {1, 2, 3, 4});
    truncated.resize(truncated.size() - 4);
    CHECK(!audio::parseWav(truncated, &err));
    auto bad = makeWav(1, 44100, {1});
    bad[22] = 9; // 9 channels
    CHECK(!audio::parseWav(bad, &err));
}

TEST_CASE(wav_every_embedded_sound_parses) {
    for (const auto& info : audio::kSounds) {
        auto bytes = assets::findEmbedded(info.file);
        REQUIRE(bytes.has_value());
        std::string err;
        auto clip = audio::parseWav(*bytes, &err);
        if (!clip) test::fail(__FILE__, __LINE__, std::string(info.file) + ": " + err);
        else CHECK(clip->samples.size() > 100);
    }
    CHECK(assets::embeddedText("scripts/levels.lua").has_value());
    CHECK(assets::embeddedText("scripts/bots/bot.lua").has_value());
}
