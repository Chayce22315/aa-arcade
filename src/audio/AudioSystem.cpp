#include "audio/AudioSystem.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "assets/EmbeddedAssets.hpp"
#include "audio/Wav.hpp"
#include "engine/Log.hpp"

namespace spindle::audio {

AudioSystem::~AudioSystem() { close(); }

int AudioSystem::loadClips() {
    clipsLoaded_ = 0;
    for (const SoundInfo& info : kSounds) {
        auto bytes = assets::findEmbedded(info.file);
        std::string err;
        std::optional<PcmClip> clip = bytes ? parseWav(*bytes, &err) : std::nullopt;
        if (!clip) {
            log::error("audio: {} unusable: {}", info.file, bytes ? err : std::string("not embedded"));
            continue;
        }
        std::vector<float>& out = clips_[static_cast<std::size_t>(info.id)];
        if (clip->sampleRate == kSampleRate) {
            out = std::move(clip->samples);
        } else {
            // Linear resampling to the mixer rate.
            const double ratio = static_cast<double>(clip->sampleRate) / kSampleRate;
            const auto n = static_cast<std::size_t>(static_cast<double>(clip->samples.size()) / ratio);
            out.resize(n);
            for (std::size_t i = 0; i < n; ++i) {
                const double src = static_cast<double>(i) * ratio;
                const auto j = static_cast<std::size_t>(src);
                const double f = src - static_cast<double>(j);
                const float a = clip->samples[std::min(j, clip->samples.size() - 1)];
                const float b = clip->samples[std::min(j + 1, clip->samples.size() - 1)];
                out[i] = a + (b - a) * static_cast<float>(f);
            }
        }
        ++clipsLoaded_;
    }
    return clipsLoaded_;
}

bool AudioSystem::open(std::string* error) {
    close();
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    spec.freq = kSampleRate;
    stream_.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioSystem::callback, this));
    if (!stream_) {
        if (error) *error = std::format("no audio device: {}", SDL_GetError());
        return false;
    }
    const SDL_AudioDeviceID dev = SDL_GetAudioStreamDevice(stream_.get());
    const char* name = SDL_GetAudioDeviceName(dev);
    deviceName_ = name ? name : "default device";
    if (!SDL_ResumeAudioStreamDevice(stream_.get())) {
        if (error) *error = std::format("cannot start audio: {}", SDL_GetError());
        stream_.reset();
        return false;
    }
    log::info("audio: playing on {} ({} Hz mono mixer, {} voices)", deviceName_, kSampleRate, kVoices);
    return true;
}

void AudioSystem::close() {
    stream_.reset(); // destroying the stream stops its callback before returning
}

void AudioSystem::setVolumes(const config::Settings::Audio& v) {
    master_.store(static_cast<float>(std::clamp(v.masterVolume, 0, 100)) / 100.0f);
    sfx_.store(static_cast<float>(std::clamp(v.sfxVolume, 0, 100)) / 100.0f);
    ui_.store(static_cast<float>(std::clamp(v.uiVolume, 0, 100)) / 100.0f);
}

void AudioSystem::play(SoundId id) {
    if (!stream_) return;
    const auto index = static_cast<std::size_t>(id);
    if (index >= clips_.size() || clips_[index].empty()) return;
    const SoundInfo& info = kSounds[index];
    const float gain = (info.category == SoundCategory::Ui ? ui_.load() : sfx_.load());
    SDL_LockAudioStream(stream_.get());
    // Take a free voice, or steal the one closest to finishing.
    Voice* slot = nullptr;
    std::size_t bestRemaining = SIZE_MAX;
    for (Voice& v : voices_) {
        if (!v.clip) {
            slot = &v;
            break;
        }
        const std::size_t remaining = v.clip->size() - v.position;
        if (remaining < bestRemaining) {
            bestRemaining = remaining;
            slot = &v;
        }
    }
    if (slot) *slot = {&clips_[index], 0, gain};
    SDL_UnlockAudioStream(stream_.get());
}

void SDLCALL AudioSystem::callback(void* userdata, SDL_AudioStream* stream, int additional, int) {
    auto* self = static_cast<AudioSystem*>(userdata);
    if (additional <= 0) return;
    const int frames = additional / static_cast<int>(sizeof(float));
    self->mix(frames);
    SDL_PutAudioStreamData(stream, self->buffer_.data(), frames * static_cast<int>(sizeof(float)));
}

void AudioSystem::mix(int frames) {
    buffer_.assign(static_cast<std::size_t>(frames), 0.0f);
    const float master = master_.load();
    for (Voice& v : voices_) {
        if (!v.clip) continue;
        const std::size_t n = std::min(static_cast<std::size_t>(frames), v.clip->size() - v.position);
        for (std::size_t i = 0; i < n; ++i) buffer_[i] += (*v.clip)[v.position + i] * v.gain;
        v.position += n;
        if (v.position >= v.clip->size()) v.clip = nullptr;
    }
    for (float& s : buffer_) s = std::clamp(s * master, -1.0f, 1.0f);
}

} // namespace spindle::audio
