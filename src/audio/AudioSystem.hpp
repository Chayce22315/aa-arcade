#pragma once

#include <array>
#include <atomic>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "audio/SoundId.hpp"
#include "config/Settings.hpp"
#include "engine/SdlHandles.hpp"

namespace spindle::audio {

// Fixed-voice software mixer on one SDL3 audio stream (44.1 kHz mono float). If no audio device can
// be opened the game keeps running silently and the service menu shows audio as unavailable.
class AudioSystem {
public:
    AudioSystem() = default;
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // Loads the embedded clips (boot step 4); returns the number loaded.
    int loadClips();
    // Opens the default playback device (boot step 9).
    bool open(std::string* error);
    void close();

    void setVolumes(const config::Settings::Audio& volumes);
    void play(SoundId id);

    bool available() const { return stream_ != nullptr; }
    const std::string& deviceName() const { return deviceName_; }
    int clipsLoaded() const { return clipsLoaded_; }

    static constexpr int kSampleRate = 44100;
    static constexpr int kVoices = 16;

private:
    struct Voice {
        const std::vector<float>* clip = nullptr;
        std::size_t position = 0;
        float gain = 0.0f;
    };

    static void SDLCALL callback(void* userdata, SDL_AudioStream* stream, int additional, int total);
    void mix(int frames);

    std::array<std::vector<float>, static_cast<std::size_t>(SoundId::Count)> clips_{};
    std::array<Voice, kVoices> voices_{};
    std::vector<float> buffer_;
    sdl::AudioStreamPtr stream_;
    std::atomic<float> master_{0.8f};
    std::atomic<float> sfx_{1.0f};
    std::atomic<float> ui_{0.8f};
    std::string deviceName_;
    int clipsLoaded_ = 0;
};

} // namespace spindle::audio
