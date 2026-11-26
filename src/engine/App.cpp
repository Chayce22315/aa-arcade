#include "engine/App.hpp"

#include <algorithm>
#include <array>
#include <format>

#include <SDL3/SDL_time.h>

#include "assets/ControllerDbPin.hpp"
#include "config/AtomicFile.hpp"
#include "engine/Branding.hpp"
#include "engine/Log.hpp"
#include "engine/Platform.hpp"
#include "engine/SelfTest.hpp"
#include "gameplay/Geometry.hpp"
#include "menus/Screens.hpp"

namespace spindle {

namespace {
constexpr std::uint64_t kTickNs = std::uint64_t{1'000'000'000} / static_cast<std::uint64_t>(gameplay::geom::kTickRate);
constexpr int kMaxCatchUpTicks = 60; // 250 ms; beyond that the simulation drops time instead of spiralling
} // namespace

std::string localIsoDate() {
    SDL_Time now = 0;
    SDL_DateTime dt{};
    if (!SDL_GetCurrentTime(&now) || !SDL_TimeToDateTime(now, &dt, true)) return {};
    return std::format("{:04}-{:02}-{:02}", dt.year, dt.month, dt.day);
}

App::App(AppOptions options) : options_(std::move(options)) {}

App::~App() {
    // Stop the audio callback before the clips it reads are destroyed.
    audio_.close();
    log::close();
}

int App::run() {
    if (!boot()) return 1;
    if (options_.selftest) {
        selftest_ = std::make_unique<SelfTest>(*this, options_.selftestOut.value_or(paths_.writable / "selftest"));
        input_.setUseTimestamps(false);
    }
    running_ = true;
    simTimeNs_ = SDL_GetTicksNS();
    lastFrameNs_ = simTimeNs_;
    loop();
    saveBookkeeping();
    log::info("{} shutting down (exit code {})", kTitle, exitCode_);
    return exitCode_;
}

void App::requestQuit(int code) {
    exitCode_ = code;
    running_ = false;
}

void App::updateLayout() {
    const auto [w, h] = display_.pixelSize();
    // Keep the stations clear of notches and rounded corners (the safe area is in window points).
    render::Rect safe{};
    SDL_Rect area{};
    int ww = 0, wh = 0;
    if (display_.window() && SDL_GetWindowSafeArea(display_.window(), &area) && SDL_GetWindowSize(display_.window(), &ww, &wh) && ww > 0 && wh > 0) {
        const float sx = static_cast<float>(w) / static_cast<float>(ww), sy = static_cast<float>(h) / static_cast<float>(wh);
        if (area.w < ww || area.h < wh)
            safe = {static_cast<float>(area.x) * sx, static_cast<float>(area.y) * sy, static_cast<float>(area.w) * sx, static_cast<float>(area.h) * sy};
    }
    layout_ = render::computeLayout(w, h, safe);
}

config::Settings App::defaultSettings() const { return platform::kMobile ? config::Settings::mobileDefaults() : config::Settings::defaults(); }

bool App::touchUi() const { return platform::kMobile; }

input::TouchContext App::touchContext() const {
    input::TouchContext c;
    if (!arcade_ || (service_ && service_->isOpen())) {
        c.screen = input::TouchScreen::Ignore;
        return c;
    }
    for (std::size_t i = 0; i < 4; ++i) c.columnEdges[i] = layout_.columns[i].x / layout_.width;
    c.columnEdges[4] = (layout_.columns[3].x + layout_.columns[3].w) / layout_.width;
    if (arcade_->state() == arcade::ArcadeState::Playing || arcade_->state() == arcade::ArcadeState::Initials) {
        c.screen = input::TouchScreen::Playing;
        const gameplay::Match* m = arcade_->match();
        for (std::size_t i = 0; i < 4; ++i) {
            const int s = static_cast<int>(i);
            c.entering[i] = arcade_->entering(s);
            const bool human = arcade_->lineup().seats[i] == players::SeatKind::Human;
            const bool stillIn = m && m->participant(s) && !m->participant(s)->eliminated && !m->over();
            c.humans[i] = human && stillIn && arcade_->state() == arcade::ArcadeState::Playing;
        }
        c.pause = touchUi() && arcade_->state() == arcade::ArcadeState::Playing && !arcade_->suspended() &&
                  std::count(c.humans.begin(), c.humans.end(), true) == 1;
    } else {
        c.screen = input::TouchScreen::Lobby;
        c.gear = touchUi() && (arcade_->state() == arcade::ArcadeState::Attract || arcade_->state() == arcade::ArcadeState::InsertCoin);
        // On the vote screen a joined column picks and votes (with one mode on, a tap simply starts).
        if (arcade_->state() == arcade::ArcadeState::Vote && arcade_->votePhase() == arcade::VotePhase::Voting && arcade_->vote().modes().size() > 1)
            for (std::size_t i = 0; i < 4; ++i) c.voting[i] = arcade_->vote().joined(static_cast<int>(i));
    }
    return c;
}

void App::handleLifecycle(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        log::info("app: going to the background");
        if (arcade_) arcade_->suspend();
        saveBookkeeping();
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        // The loop did not run while away: start the clock again from now instead of catching up.
        simTimeNs_ = SDL_GetTicksNS();
        log::info("app: back in the foreground");
        break;
    case SDL_EVENT_TERMINATING:
        log::info("app: the system is terminating the app");
        saveBookkeeping();
        break;
    case SDL_EVENT_LOW_MEMORY: log::warn("app: the system is low on memory"); break;
    default: break;
    }
}

void App::updateScreenSaver() {
    if (!platform::kMobile || !arcade_) return;
    // A phone may sleep while the cabinet idles in attract, never while people are playing.
    const auto st = arcade_->state();
    const int allow = (st == arcade::ArcadeState::Attract || st == arcade::ArcadeState::InsertCoin) ? 1 : 0;
    if (allow == screenSaverAllowed_) return;
    screenSaverAllowed_ = allow;
    if (allow)
        SDL_EnableScreenSaver();
    else
        SDL_DisableScreenSaver();
}

void App::pumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            log::info("app: quit requested by the system");
            if (settings_.arcade.exitHoldSeconds == 0 && !options_.selftest && !platform::kMobile)
                log::info("app: quit request ignored (cabinet locked: exit_hold_seconds = 0)");
            else
                requestQuit(0);
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            updateLayout();
            break;
        case SDL_EVENT_RENDER_DEVICE_RESET: {
            // Shapes are rebuilt every frame; the one texture has to be created again.
            std::string err;
            if (textures_.load(display_.renderer(), &err))
                log::warn("render: device reset; textures reloaded");
            else
                log::warn("render: device reset; {}", err);
            updateLayout();
            break;
        }
        case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED: updateLayout(); break;
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
        case SDL_EVENT_DID_ENTER_BACKGROUND:
        case SDL_EVENT_DID_ENTER_FOREGROUND:
        case SDL_EVENT_TERMINATING:
        case SDL_EVENT_LOW_MEMORY: handleLifecycle(e); break;
        case SDL_EVENT_KEY_DOWN:
            // Android's back button: leaves a service page, or the app when nobody is playing.
            if (e.key.scancode == SDL_SCANCODE_AC_BACK && !e.key.repeat) {
                if (service_ && service_->isOpen()) {
                    service_->back();
                } else if (arcade_ && arcade_->state() != arcade::ArcadeState::Playing) {
                    log::info("app: back button, leaving");
                    requestQuit(0);
                }
                break;
            }
            input_.handleEvent(e);
            break;
        case SDL_EVENT_RENDER_TARGETS_RESET:
            log::warn("render: targets reset; geometry is rebuilt every frame so nothing to reload");
            updateLayout();
            break;
        default: input_.handleEvent(e); break;
        }
    }
}

void App::loop() {
    while (running_) {
        pumpEvents();
        if (selftest_) {
            // Virtual clock: a fixed number of ticks per frame, as fast as the machine allows.
            for (int i = 0; i < SelfTest::kTicksPerFrame && running_; ++i) {
                selftest_->beforeTick();
                pumpEvents();
                tick(0);
            }
            render();
            continue;
        }
        const std::uint64_t now = SDL_GetTicksNS();
        int n = 0;
        while (simTimeNs_ + kTickNs <= now && n < kMaxCatchUpTicks && running_) {
            simTimeNs_ += kTickNs;
            tick(simTimeNs_);
            ++n;
        }
        if (n == kMaxCatchUpTicks && simTimeNs_ + kTickNs <= now) {
            log::warn("app: simulation fell {} ms behind; skipping ahead", (now - simTimeNs_) / 1'000'000);
            simTimeNs_ = now;
        }
        render();
        if (!display_.vsyncActive() && settings_.display.maxFps > 0) {
            const auto fps = static_cast<std::uint64_t>(std::max(30, settings_.display.maxFps));
            const std::uint64_t frameNs = std::uint64_t{1'000'000'000} / fps;
            const std::uint64_t spent = SDL_GetTicksNS() - now;
            if (spent < frameNs) SDL_DelayPrecise(frameNs - spent);
        }
    }
}

void App::tick(std::uint64_t tickEndNs) {
    constexpr double dt = gameplay::geom::kTickSeconds;
    ++ticks_;
    clockSeconds_ += dt;
    input::TickInput in = input_.drain(tickEndNs);
    if (ticks_ % static_cast<std::uint64_t>(gameplay::geom::kTickRate) == 1) arcade_->setToday(localIsoDate()); // today's tables roll over at midnight
    for (int s : in.stationsLost) arcade_->stationDisconnected(s);
    for (int s : in.stationsRestored) arcade_->stationReconnected(s);

    if (service_->isOpen()) {
        service_->update(in, dt);
    } else {
        if (in.systemPressed[static_cast<std::size_t>(input::Action::Service)]) {
            service_->open(in.serviceByTouch);
            audio_.play(audio::SoundId::MenuSelect);
        } else if (in.systemPressed[static_cast<std::size_t>(input::Action::Options)]) {
            service_->openPlayerOptions();
            audio_.play(audio::SoundId::MenuSelect);
        } else {
            if (in.systemPressed[static_cast<std::size_t>(input::Action::Pause)]) arcade_->suspend();
            arcade_->tick(in.arcade);
        }
    }
    handleArcadeEvents();
    haptics_.update(SDL_GetTicksNS());
    updateScreenSaver();

    exitHeld_ = input_.systemHeld(input::Action::Exit) ? exitHeld_ + dt : 0.0;
    if (settings_.arcade.exitHoldSeconds > 0 && exitHeld_ >= settings_.arcade.exitHoldSeconds) {
        log::info("app: exit key held for {} s", settings_.arcade.exitHoldSeconds);
        requestQuit(0);
    }
    if (selftest_) selftest_->afterTick();
}

bool App::stationUsesPhone(int station) const { return touchUi() && !input_.devices().stationHasController(station); }

void App::handleArcadeEvents() {
    // Several touch players share one phone: it buzzes once per kind of moment per tick.
    std::array<bool, 16> phoneBuzzed{};
    const std::uint64_t now = SDL_GetTicksNS();
    for (const arcade::ArcadeEvent& e : arcade_->takeEvents()) {
        switch (e.type) {
        case arcade::ArcadeEventType::Haptic: {
            const auto k = static_cast<std::size_t>(e.haptic);
            bool phone = e.station >= 0 && stationUsesPhone(e.station) && k < phoneBuzzed.size() && !phoneBuzzed[k];
            if (phone) phoneBuzzed[k] = true;
            haptics_.play(e.haptic, e.station, phone, now);
            break;
        }
        case arcade::ArcadeEventType::Sound: audio_.play(e.sound); break;
        case arcade::ArcadeEventType::SaveBookkeeping: saveBookkeeping(); break;
        case arcade::ArcadeEventType::SaveHighScores: saveHighScores(); break;
        case arcade::ArcadeEventType::JoinRefused: audio_.play(audio::SoundId::MenuMove); break;
        default: break;
        }
    }
}

void App::render() {
    const std::uint64_t now = SDL_GetTicksNS();
    frameMs_ = static_cast<double>(now - lastFrameNs_) / 1e6;
    lastFrameNs_ = now;
    renderer_->begin(render::colors::kBlack);
    if (service_->isOpen()) {
        service_->draw(*renderer_, layout_, clockSeconds_);
    } else {
        const double sub = selftest_ ? 0.0 : static_cast<double>(now > simTimeNs_ ? now - simTimeNs_ : 0) / 1e9;
        menus::ScreenContext ctx{*renderer_, layout_, *arcade_, clockSeconds_, sub, exitHeld_, static_cast<double>(settings_.arcade.exitHoldSeconds),
                                textures_.flame(), touchUi()};
        menus::drawArcade(ctx);
    }
    renderer_->end();
    SDL_RenderPresent(display_.renderer());
}

void App::renderNow() { render(); }

bool App::saveScreenshot(const std::filesystem::path& file) {
    // Re-draw into the back buffer, read it back before presenting.
    renderer_->begin(render::colors::kBlack);
    if (service_->isOpen()) {
        service_->draw(*renderer_, layout_, clockSeconds_);
    } else {
        menus::ScreenContext ctx{*renderer_, layout_, *arcade_, clockSeconds_, 0.0, exitHeld_, static_cast<double>(settings_.arcade.exitHoldSeconds),
                                textures_.flame(), touchUi()};
        menus::drawArcade(ctx);
    }
    renderer_->end();
    sdl::SurfacePtr surface(SDL_RenderReadPixels(display_.renderer(), nullptr));
    SDL_RenderPresent(display_.renderer());
    if (!surface) {
        log::warn("screenshot: read failed: {}", SDL_GetError());
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    const std::string path = pathToUtf8(file);
    if (!SDL_SaveBMP(surface.get(), path.c_str())) {
        log::warn("screenshot: cannot write {}: {}", path, SDL_GetError());
        return false;
    }
    return true;
}

void App::commitSettings(const config::Settings& s) {
    const bool displayChanged = !(s.display == settings_.display);
    settings_ = s;
    config::storeSettings(settings_, settingsDoc_);
    std::string err;
    if (!config::writeFileAtomic(paths_.settings(), settingsDoc_.serialize(), &err))
        log::error("settings: save failed: {}", err);
    else
        log::info("settings: saved {}", pathToUtf8(paths_.settings()));
    settingsIssues_ = static_cast<std::size_t>(input_.configure(settings_));
    audio_.setVolumes(settings_.audio);
    haptics_.configure(settings_.feedback);
    arcade_->applySettings(settings_);
    log::setLevel(static_cast<log::Level>(static_cast<int>(settings_.system.logLevel)));
    if (displayChanged) previewDisplay(settings_.display);
}

void App::previewDisplay(const config::Settings::Display& d) {
    config::Settings::Display wanted = d;
    if (options_.forceWindowed) wanted.fullscreen = config::FullscreenMode::Windowed;
    std::string err;
    if (!display_.apply(wanted, &err)) log::warn("display: {}", err);
    updateLayout();
}

void App::previewVolumes(const config::Settings::Audio& a) { audio_.setVolumes(a); }

void App::previewFeedback(const config::Settings::Feedback& f) {
    haptics_.configure(f);
    const std::uint64_t now = SDL_GetTicksNS();
    for (int s = 0; s < gameplay::kMaxStations; ++s)
        if (input_.devices().stationHasController(s)) haptics_.play(arcade::HapticKind::Clear, s, false, now);
    if (touchUi()) haptics_.play(arcade::HapticKind::Clear, -1, true, now);
    haptics_.configure(settings_.feedback); // only a preview: the saved settings stay in force
}

void App::saveHighScores() {
    if (!arcade_) return;
    std::string err;
    if (!config::writeFileAtomic(paths_.highscores(), arcade_->highScores().serialize(), &err)) log::error("high scores: save failed: {}", err);
}

void App::saveBookkeeping() {
    if (!arcade_) return;
    std::string err;
    if (!config::writeFileAtomic(paths_.bookkeeping(), arcade_->bookkeeping().serialize(), &err))
        log::error("bookkeeping: save failed: {}", err);
}

std::string App::controllerDbSummary() const {
    switch (controllerDb_.state) {
    case assets::ControllerDbState::Present:
    case assets::ControllerDbState::Downloaded: return std::format("{} mappings", controllerDb_.mappingsLoaded);
    case assets::ControllerDbState::Missing: return "built-in mappings only";
    case assets::ControllerDbState::Rejected: return "rejected (not the pinned file)";
    }
    return "-";
}

} // namespace spindle
