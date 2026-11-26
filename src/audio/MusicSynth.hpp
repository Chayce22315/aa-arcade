#pragma once

#include <cstdint>

// Live music for the cabinet: a small step sequencer with five layers (pad, bass, drums, hats, lead)
// whose levels follow the game's intensity (docs/SETTINGS.md "music_volume"). Rendered sample by sample
// in the audio callback, so it reacts at once and costs no WAV loops in the executable. Pure code: the
// tests render it without a device. Not thread-safe: only the audio thread touches one instance.
namespace spindle::audio {

enum class MusicScene {
    Off,     // fades out
    Attract, // calm: pad, a soft bass and a gentle arpeggio
    Match    // layers come in with intensity: bass, then the kick, hats and snare, then the lead
};

class MusicSynth {
public:
    explicit MusicSynth(int sampleRate = 44100);

    // Where the music should head; layers fade towards it over about half a second.
    void setCue(MusicScene scene, float intensity);
    // Adds `frames` mono samples (times gain) into out.
    void render(float* out, int frames, float gain);

    int step() const { return step_; } // 16th-note step in the 4-bar loop (0..63)
    static constexpr double kBpm = 116.0;

private:
    float next();
    void onStep();
    float noise();

    double rate_ = 44100.0;
    double samplesPerStep_ = 0.0;
    double stepClock_ = 0.0;
    int step_ = -1;
    MusicScene scene_ = MusicScene::Off;
    float intensity_ = 0.0f;

    // Layer levels (current, smoothed) and targets.
    float pad_ = 0, bass_ = 0, kick_ = 0, hats_ = 0, snare_ = 0, lead_ = 0;
    float padT_ = 0, bassT_ = 0, kickT_ = 0, hatsT_ = 0, snareT_ = 0, leadT_ = 0;
    float smooth_ = 0.0f;

    // Voices.
    double padPhase_[3] = {0, 0, 0}, padPhase2_[3] = {0, 0, 0}, padFreq_[3] = {0, 0, 0};
    double bassPhase_ = 0, bassFreq_ = 0;
    float bassEnv_ = 0, bassLp_ = 0;
    double kickPhase_ = 0;
    float kickEnv_ = 0, kickTime_ = 1;
    float snareEnv_ = 0, hatEnv_ = 0, hatLp_ = 0;
    double snarePhase_ = 0;
    double leadPhase_ = 0, leadFreq_ = 0;
    float leadEnv_ = 0, leadLp_ = 0;
    float bassDecay_ = 0, kickDecay_ = 0, snareDecay_ = 0, hatDecay_ = 0, leadDecay_ = 0;
    std::uint32_t noise_ = 0x9E3779B9u;
};

} // namespace spindle::audio
