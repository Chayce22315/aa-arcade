// The live music synth: finite, never clipping, layers following intensity, deterministic, fading out.

#include <algorithm>
#include <cmath>
#include <vector>

#include "Test.hpp"
#include "audio/MusicSynth.hpp"

using spindle::audio::MusicScene;
using spindle::audio::MusicSynth;

namespace {

constexpr int kRate = 44100;

struct Stats {
    float peak = 0.0f;
    double rms = 0.0;
    bool finite = true;
};

Stats renderFor(MusicSynth& m, double seconds, float gain = 1.0f) {
    std::vector<float> buf(static_cast<std::size_t>(seconds * kRate), 0.0f);
    m.render(buf.data(), static_cast<int>(buf.size()), gain);
    Stats s;
    double sum = 0.0;
    for (float v : buf) {
        if (!std::isfinite(v)) s.finite = false;
        s.peak = std::max(s.peak, std::fabs(v));
        sum += static_cast<double>(v) * static_cast<double>(v);
    }
    s.rms = buf.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(buf.size()));
    return s;
}

} // namespace

TEST_CASE(music_is_finite_and_never_clips_at_any_intensity) {
    for (MusicScene scene : {MusicScene::Attract, MusicScene::Match})
        for (float intensity : {0.0f, 0.3f, 0.6f, 1.0f}) {
            MusicSynth m(kRate);
            m.setCue(scene, intensity);
            const Stats s = renderFor(m, 12.0);
            CHECK(s.finite);
            CHECK(s.peak <= 0.9f);
            CHECK(s.rms > 0.005); // audible
        }
}

TEST_CASE(music_builds_up_with_intensity) {
    MusicSynth calm(kRate), hot(kRate);
    calm.setCue(MusicScene::Match, 0.0f);
    hot.setCue(MusicScene::Match, 1.0f);
    renderFor(calm, 2.0); // let the layers fade in
    renderFor(hot, 2.0);
    CHECK(renderFor(hot, 8.0).rms > 1.5 * renderFor(calm, 8.0).rms);
}

TEST_CASE(music_is_deterministic_and_loops_four_bars) {
    MusicSynth a(kRate), b(kRate);
    a.setCue(MusicScene::Match, 0.9f);
    b.setCue(MusicScene::Match, 0.9f);
    std::vector<float> x(48000, 0.0f), y(48000, 0.0f);
    a.render(x.data(), static_cast<int>(x.size()), 1.0f);
    b.render(y.data(), static_cast<int>(y.size()), 1.0f);
    CHECK(x == y);
    // 64 sixteenth notes at 116 bpm = 8.28 s; after exactly one loop the step is back where it started.
    MusicSynth c(kRate);
    c.setCue(MusicScene::Attract, 0.0f);
    renderFor(c, 0.01);
    const int first = c.step();
    renderFor(c, 64.0 * 60.0 / (MusicSynth::kBpm * 4.0));
    CHECK_EQ(c.step(), first);
}

TEST_CASE(music_fades_out_and_honours_gain) {
    MusicSynth m(kRate);
    m.setCue(MusicScene::Match, 1.0f);
    renderFor(m, 3.0);
    m.setCue(MusicScene::Off, 0.0f);
    renderFor(m, 4.0);
    CHECK(renderFor(m, 1.0).peak < 0.002f);
    MusicSynth q(kRate);
    q.setCue(MusicScene::Match, 1.0f);
    CHECK(renderFor(q, 3.0, 0.0f).peak == 0.0f);
    CHECK(renderFor(q, 3.0, std::nanf("")).peak == 0.0f);
}
