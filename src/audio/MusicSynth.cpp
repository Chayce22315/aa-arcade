#include "audio/MusicSynth.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace spindle::audio {
namespace {

constexpr double kTau = 2.0 * std::numbers::pi;
constexpr int kStepsPerBar = 16;
constexpr int kBars = 4;

// A minor: Am, F, C/G, G (one chord per bar), as MIDI notes, with the bass roots.
constexpr int kChords[kBars][3] = {{57, 60, 64}, {53, 57, 60}, {55, 60, 64}, {55, 59, 62}};
constexpr int kRoots[kBars] = {45, 41, 48, 43};
constexpr int kArp[8] = {0, 1, 2, 1, 2, 0, 1, 2};

double midiHz(int note) { return 440.0 * std::pow(2.0, (note - 69) / 12.0); }

float triangle(double phase) { // phase 0..1
    const double t = phase - std::floor(phase);
    return static_cast<float>(t < 0.5 ? 4.0 * t - 1.0 : 3.0 - 4.0 * t);
}

// Layer level for an intensity: 0 below `from`, rising to 1 over the next 0.15.
float ramp(float intensity, float from) { return std::clamp((intensity - from) / 0.15f, 0.0f, 1.0f); }

float decayPerSample(double seconds, double rate) { return static_cast<float>(std::exp(-1.0 / (seconds * rate))); }

} // namespace

MusicSynth::MusicSynth(int sampleRate) {
    rate_ = static_cast<double>(std::max(8000, sampleRate));
    samplesPerStep_ = rate_ * 60.0 / (kBpm * 4.0);
    stepClock_ = samplesPerStep_; // the first sample starts step 0
    smooth_ = 1.0f - decayPerSample(0.45, rate_);
    bassDecay_ = decayPerSample(0.16, rate_);
    kickDecay_ = decayPerSample(0.13, rate_);
    snareDecay_ = decayPerSample(0.07, rate_);
    hatDecay_ = decayPerSample(0.025, rate_);
    leadDecay_ = decayPerSample(0.09, rate_);
}

void MusicSynth::setCue(MusicScene scene, float intensity) {
    scene_ = scene;
    intensity_ = std::isfinite(intensity) ? std::clamp(intensity, 0.0f, 1.0f) : 0.0f;
    switch (scene_) {
    case MusicScene::Off: padT_ = bassT_ = kickT_ = hatsT_ = snareT_ = leadT_ = 0.0f; break;
    case MusicScene::Attract:
        padT_ = 0.8f;
        bassT_ = 0.4f;
        leadT_ = 0.3f;
        kickT_ = hatsT_ = snareT_ = 0.0f;
        break;
    case MusicScene::Match:
        padT_ = 0.7f;
        bassT_ = ramp(intensity_, 0.15f);
        kickT_ = ramp(intensity_, 0.3f);
        hatsT_ = ramp(intensity_, 0.45f);
        snareT_ = ramp(intensity_, 0.55f);
        leadT_ = ramp(intensity_, 0.8f);
        break;
    }
}

float MusicSynth::noise() {
    noise_ ^= noise_ << 13;
    noise_ ^= noise_ >> 17;
    noise_ ^= noise_ << 5;
    return static_cast<float>(noise_) / 2147483648.0f - 1.0f;
}

void MusicSynth::onStep() {
    step_ = (step_ + 1) % (kStepsPerBar * kBars);
    const int bar = step_ / kStepsPerBar, beat = step_ % kStepsPerBar;
    for (int i = 0; i < 3; ++i) padFreq_[i] = midiHz(kChords[bar][i]);
    if (beat % 2 == 0) { // bass on the 8ths, an octave up on the off-beats
        bassFreq_ = midiHz(kRoots[bar] + (beat % 4 == 2 ? 12 : 0));
        bassEnv_ = 1.0f;
    }
    if (beat % 4 == 0) {
        kickEnv_ = 1.0f;
        kickTime_ = 0.0f;
        kickPhase_ = 0.0;
    }
    if (beat == 4 || beat == 12) snareEnv_ = 1.0f;
    if (beat % 2 == 0) hatEnv_ = beat % 4 == 2 ? 1.0f : 0.55f;
    // The lead plays 16ths in a match, 8ths in attract.
    if (scene_ == MusicScene::Match || beat % 2 == 0) {
        leadFreq_ = midiHz(kChords[bar][kArp[beat % 8]] + 12);
        leadEnv_ = 1.0f;
    }
}

float MusicSynth::next() {
    stepClock_ += 1.0;
    if (stepClock_ >= samplesPerStep_) {
        stepClock_ -= samplesPerStep_;
        onStep();
    }
    pad_ += (padT_ - pad_) * smooth_;
    bass_ += (bassT_ - bass_) * smooth_;
    kick_ += (kickT_ - kick_) * smooth_;
    hats_ += (hatsT_ - hats_) * smooth_;
    snare_ += (snareT_ - snare_) * smooth_;
    lead_ += (leadT_ - lead_) * smooth_;

    float out = 0.0f;
    if (pad_ > 1e-4f) { // two slightly detuned triangles per chord tone
        float p = 0.0f;
        for (int i = 0; i < 3; ++i) {
            padPhase_[i] += padFreq_[i] / rate_;
            padPhase2_[i] += padFreq_[i] * 1.004 / rate_;
            p += triangle(padPhase_[i]) + triangle(padPhase2_[i]);
        }
        out += p * 0.028f * pad_;
    }
    if (bass_ > 1e-4f) { // a softened pulse
        bassPhase_ += bassFreq_ / rate_;
        bassPhase_ -= std::floor(bassPhase_);
        const float pulse = bassPhase_ < 0.3 ? 1.0f : -1.0f;
        bassLp_ += (pulse - bassLp_) * 0.08f;
        out += bassLp_ * bassEnv_ * 0.13f * bass_;
    }
    bassEnv_ *= bassDecay_;
    if (kick_ > 1e-4f && kickEnv_ > 1e-4f) { // a sine falling from 130 Hz to 45 Hz
        const double f = 45.0 + 85.0 * std::exp(-static_cast<double>(kickTime_) / 0.035);
        kickPhase_ += kTau * f / rate_;
        out += static_cast<float>(std::sin(kickPhase_)) * kickEnv_ * 0.26f * kick_;
    }
    kickTime_ += static_cast<float>(1.0 / rate_);
    kickEnv_ *= kickDecay_;
    const float n = noise();
    if (snare_ > 1e-4f && snareEnv_ > 1e-4f) {
        snarePhase_ += kTau * 190.0 / rate_;
        out += (n * 0.7f + static_cast<float>(std::sin(snarePhase_)) * 0.3f) * snareEnv_ * 0.12f * snare_;
    }
    snareEnv_ *= snareDecay_;
    if (hats_ > 1e-4f && hatEnv_ > 1e-4f) { // high-passed noise
        hatLp_ += (n - hatLp_) * 0.25f;
        out += (n - hatLp_) * hatEnv_ * 0.06f * hats_;
    }
    hatEnv_ *= hatDecay_;
    if (lead_ > 1e-4f) {
        leadPhase_ += leadFreq_ / rate_;
        leadPhase_ -= std::floor(leadPhase_);
        const float sq = leadPhase_ < 0.5 ? 1.0f : -1.0f;
        leadLp_ += (sq - leadLp_) * 0.12f;
        out += leadLp_ * leadEnv_ * 0.07f * lead_;
    }
    leadEnv_ *= leadDecay_;
    return out;
}

void MusicSynth::render(float* out, int frames, float gain) {
    if (!out || frames <= 0) return;
    const float g = std::isfinite(gain) ? std::clamp(gain, 0.0f, 1.0f) : 0.0f;
    for (int i = 0; i < frames; ++i) out[i] += next() * g;
}

} // namespace spindle::audio
