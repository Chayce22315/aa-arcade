#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "collision/Collision.hpp"
#include "config/AtomicFile.hpp"
#include "config/Settings.hpp"
#include "engine/App.hpp"
#include "engine/Branding.hpp"
#include "engine/Log.hpp"
#include "engine/Paths.hpp"
#include "gameplay/Geometry.hpp"
#include "gameplay/LevelScript.hpp"

namespace {

void usage() {
    std::printf(
        "%s %s\n"
        "usage: spindle [--root DIR] [--windowed] [--selftest [OUTDIR]]\n"
        "  --root DIR      keep settings, bookkeeping and logs in DIR instead of next to the executable\n"
        "  --windowed      force a window regardless of settings.ini\n"
        "  --selftest DIR  run the scripted four-station self-test (headless friendly) and write a report\n"
        "development tools (no window):\n"
        "  --write-default-settings FILE   write the commented default settings.ini\n"
        "  --level-report ROUNDS SEED      print the generated rounds as CSV\n",
        std::string(spindle::kTitle).c_str(), std::string(spindle::kVersion).c_str());
}

int writeDefaultSettings(const char* file) {
    std::string err;
    if (!spindle::config::writeFileAtomic(spindle::pathFromUtf8(file), spindle::config::defaultSettingsText(), &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    return 0;
}

int levelReport(int rounds, unsigned seed) {
    using namespace spindle::gameplay;
    spindle::log::setLevel(spindle::log::Level::Error);
    std::string err;
    auto sandbox = LevelScript::load(LevelScript::embeddedSource(), &err);
    if (!sandbox) {
        std::fprintf(stderr, "level script: %s\n", err.c_str());
        return 1;
    }
    LevelLimits limits = defaultLimits();
    limits.powerups.assign(kAllPowerups.begin(), kAllPowerups.end());
    std::printf("round,valid,launch,prepins,segments,max_speed_deg,reversals,packing,pickups,problem\n");
    for (int r = 1; r <= rounds; ++r) {
        auto def = LevelScript::generate(*sandbox, r, seed, limits, &err);
        if (!def) {
            std::printf("%d,0,0,0,0,0,0,0,0,%s\n", r, err.c_str());
            continue;
        }
        double fastest = std::fabs(def->rotation.initialSpeed);
        int reversals = 0;
        double prev = def->rotation.initialSpeed;
        for (const auto& seg : def->rotation.segments) {
            fastest = std::max(fastest, std::fabs(seg.targetSpeed));
            if (prev * seg.targetSpeed < 0.0) ++reversals;
            if (seg.targetSpeed != 0.0) prev = seg.targetSpeed;
        }
        const double total = static_cast<double>(def->prepins.size()) + def->launchCount;
        std::printf("%d,1,%d,%zu,%zu,%.1f,%d,%.3f,%zu,\n", r, def->launchCount, def->prepins.size(), def->rotation.segments.size(),
                    fastest * 180.0 / spindle::gameplay::geom::kPi, reversals, total * limits.minSeparation / spindle::gameplay::geom::kTwoPi,
                    def->pickups.size());
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--write-default-settings") == 0) return writeDefaultSettings(argv[2]);
    if (argc == 4 && std::strcmp(argv[1], "--level-report") == 0)
        return levelReport(std::atoi(argv[2]), static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10)));
    spindle::AppOptions options;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--root") == 0 && i + 1 < argc) {
            options.root = spindle::pathFromUtf8(argv[++i]);
        } else if (std::strcmp(a, "--windowed") == 0) {
            options.forceWindowed = true;
        } else if (std::strcmp(a, "--selftest") == 0) {
            options.selftest = true;
            options.forceWindowed = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') options.selftestOut = spindle::pathFromUtf8(argv[++i]);
        } else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            usage();
            return 0;
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", a);
            usage();
            return 2;
        }
    }
    const std::string title(spindle::kTitle), version(spindle::kVersion);
    SDL_SetAppMetadata(title.c_str(), version.c_str(), "spindle.arcade");
    spindle::log::setEchoToStderr(true);
    spindle::App app(std::move(options));
    return app.run();
}
