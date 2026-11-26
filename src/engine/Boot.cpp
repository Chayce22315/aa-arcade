// The real initialization sequence (no boot animation):
//  1 check required files and dependencies     6 read the preferred resolution
//  2 download missing required components      7 set the window to it
//  3 verify downloaded files                   8 load display-dependent resources
//  4 load resources                            9 initialize the arcade system
//  5 detect the display resolution            10 enter attract mode

#include <format>
#include <random>

#include "assets/ControllerDbPin.hpp"
#include "assets/EmbeddedAssets.hpp"
#include "assets/PngInfo.hpp"
#include "config/AtomicFile.hpp"
#include "engine/App.hpp"
#include "engine/Branding.hpp"
#include "engine/Log.hpp"
#include "engine/Platform.hpp"
#include "engine/SelfTest.hpp"

namespace spindle {

namespace {
void step(int n, std::string_view what) { log::info("boot {}/10: {}", n, what); }

// Phones: every log line also goes to the system log (logcat on Android, the device console on iOS).
[[maybe_unused]] void systemLogSink(log::Level level, const char* line) {
    const SDL_LogPriority p = level == log::Level::Error  ? SDL_LOG_PRIORITY_ERROR
                              : level == log::Level::Warn ? SDL_LOG_PRIORITY_WARN
                              : level == log::Level::Info ? SDL_LOG_PRIORITY_INFO
                                                          : SDL_LOG_PRIORITY_DEBUG;
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, p, "%s", line);
}
} // namespace

void App::fatal(const std::string& message) {
    // On a phone the system can close the app while it is still starting (the activity is destroyed
    // and re-created); that is not an error worth a dialog, the next start carries on.
    if (platform::kMobile && (SDL_HasEvent(SDL_EVENT_TERMINATING) || SDL_HasEvent(SDL_EVENT_QUIT))) {
        log::info("app: closed by the system while starting ({})", message);
        return;
    }
    log::error("fatal: {}", message);
    if (!options_.selftest) {
        const std::string title = std::format("{} cannot start", kTitle);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), message.c_str(), display_.window());
    }
}

void App::loadSettingsFile() {
    const auto text = config::readFile(paths_.settings());
    std::vector<config::IniIssue> syntax;
    settingsDoc_ = config::IniDocument::parse(text.value_or(""), &syntax);
    for (const auto& i : syntax) log::warn("settings: line {}: {} ({})", i.line, i.message, i.text);
    config::LoadedSettings loaded = config::loadSettings(settingsDoc_, defaultSettings());
    for (const auto& i : loaded.issues) {
        if (i.kind == config::IssueKind::Invalid)
            log::warn("settings: [{}] {} = '{}': {}", i.section, i.key, i.value, i.message);
        else
            log::debug("settings: [{}] {} {}", i.section, i.key, i.message);
    }
    settingsIssues_ = loaded.invalidCount() + syntax.size();
    settings_ = loaded.settings;
}

bool App::bootCheck() {
    if constexpr (platform::kMobile) {
        log::setEchoToStderr(false);
        log::setSink(&systemLogSink);
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight"); // the four stations need the wide side
        SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");                // App::pumpEvents decides what back does
    }
    step(1, "checking required files and dependencies");
    paths_ = resolvePaths(options_.root);
    log::open(paths_.logFile());
    log::info("{} {} starting; files in {}{}", kTitle, kVersion, pathToUtf8(paths_.writable), paths_.portable ? "" : " (user folder)");

    // settings.ini: required; written with commented defaults when missing.
    if (!std::filesystem::exists(paths_.settings())) {
        std::string err;
        if (config::writeFileAtomic(paths_.settings(), config::defaultSettingsText(defaultSettings()), &err))
            log::info("settings: created {} with defaults", pathToUtf8(paths_.settings()));
        else
            log::error("settings: cannot create {}: {}", pathToUtf8(paths_.settings()), err);
    }
    loadSettingsFile();
    if (options_.selftest) settings_ = SelfTest::adjustSettings(settings_);
    log::setLevel(static_cast<log::Level>(static_cast<int>(settings_.system.logLevel)));

    // bookkeeping.ini: optional, starts empty.
    if (auto bk = config::readFile(paths_.bookkeeping())) log::info("bookkeeping: loaded");
    if (!config::isDirectoryWritable(paths_.writable)) log::error("files: {} is not writable; settings and bookkeeping cannot be saved", pathToUtf8(paths_.writable));

    // Dependencies: SDL subsystems and drivers, Lua.
    const int v = SDL_GetVersion();
    log::info("deps: SDL {}.{}.{} ({})", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v), SDL_GetRevision());
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!sdl_.init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS)) {
        fatal(std::format("SDL could not start video and input: {}", SDL_GetError()));
        return false;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) log::warn("deps: audio subsystem unavailable ({}); running silent", SDL_GetError());
    log::info("deps: video driver {}, audio driver {}", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "none",
              SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "none");
    std::string renderers;
    for (int i = 0; i < SDL_GetNumRenderDrivers(); ++i) renderers += std::string(i ? ", " : "") + SDL_GetRenderDriver(i);
    log::info("deps: render drivers: {}", renderers.empty() ? std::string("none") : renderers);
    if (SDL_GetNumRenderDrivers() <= 0) {
        fatal("no SDL render driver is available");
        return false;
    }
    {
        scripting::LuaSandbox probe("probe");
        std::string err;
        if (!probe.valid() || !probe.run("return 1", "probe", &err)) {
            fatal("the Lua runtime could not start: " + err);
            return false;
        }
        log::info("deps: Lua 5.4 ready");
    }

    // The optional controller database.
    controllerDbCheck_ = assets::checkControllerDb(paths_.controllerDb());
    log::info("assets: {} {}", assets::controller_db_pin::kFileName,
              controllerDbCheck_.valid ? "present and pinned" : controllerDbCheck_.exists ? "present but not the pinned version" : "missing");
    return true;
}

void App::bootDownload() {
    step(2, "downloading missing components");
    step(3, "verifying downloads");
    controllerDb_ = assets::ensureControllerDb(paths_.controllerDb(), settings_.assets, controllerDbCheck_);
    log::info("assets: controller database {}: {}", assets::controllerDbStateName(controllerDb_.state), controllerDb_.detail);
}

bool App::bootLoad() {
    step(4, "loading resources");
    if (controllerDb_.state == assets::ControllerDbState::Present || controllerDb_.state == assets::ControllerDbState::Downloaded) {
        controllerDb_.mappingsLoaded = assets::loadControllerDb(paths_.controllerDb());
        log::info("assets: {} controller mappings added", controllerDb_.mappingsLoaded);
    } else {
        log::info("assets: using SDL's built-in controller mappings");
    }
    if (auto text = config::readFile(paths_.levelScriptOverride()))
        levelOverride_ = gameplay::ScriptSource{*text, pathToUtf8(paths_.levelScriptOverride())};
    levels_ = std::make_unique<gameplay::LevelScript>(levelOverride_);
    if (!levels_->embeddedHealthy()) log::error("levels: built-in script broken; emergency rounds in use");

    botSource_ = bots::BotBrain::embeddedSource();
    if (auto text = config::readFile(paths_.botScriptOverride())) {
        gameplay::ScriptSource candidate{*text, pathToUtf8(paths_.botScriptOverride())};
        std::string err;
        if (bots::BotBrain::create(candidate, config::Difficulty::Normal, 1, &err)) {
            botSource_ = std::move(candidate);
            log::info("bots: using operator bot script {}", botSource_.origin);
        } else {
            log::warn("bots: {} rejected ({}); using the built-in bot script", candidate.origin, err);
        }
    }
    std::string err;
    if (!bots::BotBrain::create(botSource_, config::Difficulty::Normal, 1, &err)) log::error("bots: bot script broken: {}", err);

    const int clips = audio_.loadClips();
    log::info("audio: {} of {} sounds loaded", clips, audio::kSounds.size());
    // The image becomes a texture at step 8, once there is a renderer; its header is checked now.
    if (const auto png = assets::findEmbedded(render::kFlameImage); png && assets::readPngInfo(*png)) {
        const auto info = *assets::readPngInfo(*png);
        log::info("assets: {} {}x{} ({} bytes)", render::kFlameImage, info.width, info.height, png->size());
    } else {
        log::warn("assets: {} missing or damaged", render::kFlameImage);
    }
    const int keyIssues = input_.configure(settings_);
    settingsIssues_ += static_cast<std::size_t>(keyIssues);
    return true;
}

bool App::bootDisplay() {
    step(5, "detecting the display");
    std::string err;
    if (!display_.detect(settings_.display.displayIndex, &err)) {
        fatal(err);
        return false;
    }
    step(6, std::format("preferred mode {} {}", config::formatResolution(settings_.display.resolution),
                        settings_.display.fullscreen == config::FullscreenMode::Borderless  ? "borderless"
                        : settings_.display.fullscreen == config::FullscreenMode::Exclusive ? "exclusive"
                                                                                            : "windowed"));
    config::Settings::Display wanted = settings_.display;
    if (options_.forceWindowed) wanted.fullscreen = config::FullscreenMode::Windowed;
    if constexpr (platform::kMobile) {
        // A phone has one mode: the whole screen at its own resolution.
        wanted.fullscreen = config::FullscreenMode::Borderless;
        wanted.resolution = config::Resolution{};
    }
    step(7, "setting the window");
    if (!display_.create(wanted, &err)) {
        fatal(err);
        return false;
    }
    step(8, "loading display-dependent resources");
    renderer_ = std::make_unique<render::Renderer2D>(display_.renderer());
    if (std::string texErr; textures_.load(display_.renderer(), &texErr))
        log::info("boot: flame image loaded ({})", render::kFlameImage);
    else
        log::warn("boot: {}; the fire meter is drawn without it", texErr);
    updateLayout();
    return true;
}

bool App::bootArcade() {
    step(9, "initializing the arcade");
    std::string err;
    if (!audio_.open(&err)) log::warn("audio: {}; running silent", err);
    audio_.setVolumes(settings_.audio);

    arcade::Bookkeeping bk;
    if (auto text = config::readFile(paths_.bookkeeping())) bk = arcade::Bookkeeping::parse(config::IniDocument::parse(*text));
    std::random_device rd;
    const std::uint64_t entropy = (static_cast<std::uint64_t>(rd()) << 32) ^ rd() ^ SDL_GetTicksNS();
    arcade_ = std::make_unique<arcade::ArcadeMachine>(
        settings_, *levels_, [this](config::Difficulty d, std::uint64_t seed) { return makeBot(d, seed); }, std::move(bk), entropy);
    if (auto text = config::readFile(paths_.highscores())) {
        arcade_->setHighScores(arcade::HighScores::parse(config::IniDocument::parse(*text), arcade::ArcadeMachine::kScoreTables));
        log::info("high scores: loaded");
    }
    arcade_->setToday(localIsoDate());
    service_ = std::make_unique<menus::ServiceMenu>(*this);

    input_.setTouchContext([this] { return touchContext(); });
    input_.setClaimPolicy([this]() -> int {
        // A station waiting for its lost controller first; otherwise the lowest station nobody holds.
        for (int s = 0; s < 4; ++s)
            if (arcade_->stationWaitingForReconnect(s) && !input_.devices().stationHasController(s)) return s;
        if (arcade_->state() == arcade::ArcadeState::Playing) return -1;
        for (int s = 0; s < 4; ++s)
            if (!arcade_->joined()[static_cast<std::size_t>(s)] && !input_.devices().stationHasController(s)) return s;
        return -1;
    });
    return true;
}

bool App::boot() {
    if (!bootCheck()) return false;
    bootDownload();
    if (!bootLoad()) return false;
    if (!bootDisplay()) return false;
    if (!bootArcade()) return false;
    step(10, "entering attract mode");
    display_.show();
    return true;
}

std::unique_ptr<bots::BotController> App::makeBot(config::Difficulty difficulty, std::uint64_t seed) {
    std::string err;
    auto brain = bots::BotBrain::create(botSource_, difficulty, seed, &err);
    if (!brain) {
        log::error("bots: cannot create bot: {}", err);
        return nullptr;
    }
    return std::make_unique<bots::BotController>(std::move(brain));
}

} // namespace spindle
