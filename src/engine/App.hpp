#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "arcade/ArcadeMachine.hpp"
#include "assets/ControllerDb.hpp"
#include "audio/AudioSystem.hpp"
#include "config/IniDocument.hpp"
#include "config/Settings.hpp"
#include "engine/Paths.hpp"
#include "engine/SdlHandles.hpp"
#include "gameplay/LevelScript.hpp"
#include "input/InputSystem.hpp"
#include "menus/ServiceMenu.hpp"
#include "render/DisplaySetup.hpp"
#include "render/Layout.hpp"
#include "render/Renderer2D.hpp"
#include "render/Textures.hpp"

namespace spindle {

struct AppOptions {
    bool selftest = false;
    std::optional<std::filesystem::path> root;  // replaces the exe folder for all files
    std::optional<std::filesystem::path> selftestOut;
    bool forceWindowed = false;
};

class SelfTest;

// The cabinet process: the 10-step boot (Boot.cpp), then the fixed-timestep loop (App.cpp).
class App final : public menus::ServiceHost {
public:
    explicit App(AppOptions options);
    ~App() override;

    int run();

    // ServiceHost
    const config::Settings& currentSettings() const override { return settings_; }
    config::Settings defaultSettings() const override;
    bool touchUi() const override;
    void commitSettings(const config::Settings& settings) override;
    void previewDisplay(const config::Settings::Display& display) override;
    void previewVolumes(const config::Settings::Audio& audio) override;
    void playSound(audio::SoundId id) override { audio_.play(id); }
    input::InputSystem& inputSystem() override { return input_; }
    arcade::ArcadeMachine& arcadeMachine() override { return *arcade_; }
    const render::Display& display() const override { return display_; }
    bool audioAvailable() const override { return audio_.available(); }
    std::string audioDevice() const override { return audio_.deviceName(); }
    std::size_t settingsIssueCount() const override { return settingsIssues_; }
    std::string controllerDbSummary() const override;
    void saveBookkeeping() override;
    void requestShutdown() override { requestQuit(0); }
    double frameMilliseconds() const override { return frameMs_; }

    // Used by the self-test driver.
    menus::ServiceMenu& serviceMenu() { return *service_; }
    const Paths& paths() const { return paths_; }
    std::uint64_t tickCount() const { return ticks_; }
    void requestQuit(int code);
    bool saveScreenshot(const std::filesystem::path& file);
    void renderNow();
    const render::ScreenLayout& layout() const { return layout_; }
    const assets::ControllerDbStatus& controllerDb() const { return controllerDb_; }

private:
    bool boot();
    bool bootCheck();       // step 1
    void bootDownload();    // steps 2-3
    bool bootLoad();        // step 4
    bool bootDisplay();     // steps 5-8
    bool bootArcade();      // step 9
    void fatal(const std::string& message);

    void loop();
    void pumpEvents();
    void tick(std::uint64_t tickEndNs);
    void render();
    void handleArcadeEvents();
    void updateLayout();
    void handleLifecycle(const SDL_Event& e);
    void updateScreenSaver();
    input::TouchContext touchContext() const;
    void loadSettingsFile();
    std::unique_ptr<bots::BotController> makeBot(config::Difficulty difficulty, std::uint64_t seed);

    AppOptions options_;
    sdl::SdlRuntime sdl_;
    Paths paths_;
    config::IniDocument settingsDoc_;
    config::Settings settings_;
    std::size_t settingsIssues_ = 0;
    std::optional<gameplay::ScriptSource> levelOverride_;
    gameplay::ScriptSource botSource_;
    std::unique_ptr<gameplay::LevelScript> levels_;
    assets::ControllerDbCheck controllerDbCheck_;
    assets::ControllerDbStatus controllerDb_;
    render::Display display_;
    std::unique_ptr<render::Renderer2D> renderer_;
    render::Textures textures_; // declared after display_ so it is destroyed before the renderer
    render::ScreenLayout layout_;
    input::InputSystem input_;
    audio::AudioSystem audio_;
    std::unique_ptr<arcade::ArcadeMachine> arcade_;
    std::unique_ptr<menus::ServiceMenu> service_;
    std::unique_ptr<SelfTest> selftest_;

    bool running_ = false;
    int exitCode_ = 0;
    std::uint64_t simTimeNs_ = 0;
    std::uint64_t ticks_ = 0;
    std::uint64_t lastFrameNs_ = 0;
    double frameMs_ = 0.0;
    double exitHeld_ = 0.0;
    double clockSeconds_ = 0.0;
    int screenSaverAllowed_ = -1; // last state set on a phone (-1 = not yet)

    friend class SelfTest;
};

} // namespace spindle
