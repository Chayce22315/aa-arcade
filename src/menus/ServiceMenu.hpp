#pragma once

#include <functional>
#include <optional>
#include <string>

#include "arcade/ArcadeMachine.hpp"
#include "audio/SoundId.hpp"
#include "config/Settings.hpp"
#include "input/InputSystem.hpp"
#include "menus/MenuModel.hpp"
#include "render/DisplaySetup.hpp"
#include "render/Layout.hpp"
#include "render/Renderer2D.hpp"

namespace spindle::menus {

// What the service menu needs from the running cabinet.
class ServiceHost {
public:
    virtual ~ServiceHost() = default;
    virtual const config::Settings& currentSettings() const = 0;
    virtual void commitSettings(const config::Settings& settings) = 0; // apply everywhere and save settings.ini
    virtual void previewDisplay(const config::Settings::Display& display) = 0;
    virtual void previewVolumes(const config::Settings::Audio& audio) = 0;
    virtual void playSound(audio::SoundId id) = 0;
    virtual input::InputSystem& inputSystem() = 0;
    virtual arcade::ArcadeMachine& arcadeMachine() = 0;
    virtual const render::Display& display() const = 0;
    virtual bool audioAvailable() const = 0;
    virtual std::string audioDevice() const = 0;
    virtual std::size_t settingsIssueCount() const = 0;
    virtual std::string controllerDbSummary() const = 0;
    virtual void saveBookkeeping() = 0;
    virtual void requestShutdown() = 0;
    virtual double frameMilliseconds() const = 0;
};

// Operator service / test menu. Edits a working copy of the settings; Save & Exit commits it.
class ServiceMenu {
public:
    explicit ServiceMenu(ServiceHost& host);

    void open();
    bool isOpen() const { return open_; }
    void update(const input::TickInput& in, double dt);
    void draw(render::Renderer2D& r, const render::ScreenLayout& layout, double time);

    // Introspection for the self-test.
    std::string pageTitle() const { return page_.title(); }
    std::string selectedLabel() const;
    const config::Settings& edited() const { return edit_; }

private:
    enum class PageId { Main, InputTest, Assignment, Keybinds, DisplayTest, Video, SoundTest, Volumes, Game, Coin, Bots, Bookkeeping };
    enum class Capture { None, Key, Controller, Keyboard };

    void show(PageId id);
    void build(PageId id);
    void close(bool save);
    bool confirm(const std::string& key);
    void drawInputTest(render::Renderer2D& r, const render::ScreenLayout& l, double time);
    void drawDisplayTest(render::Renderer2D& r, const render::ScreenLayout& l, double time);

    MenuItem intItem(std::string label, int& ref, int lo, int hi, int step = 1, std::string suffix = {});
    MenuItem realItem(std::string label, double& ref, double lo, double hi, double step);
    MenuItem boolItem(std::string label, bool& ref);
    MenuItem actionItem(std::string label, std::function<void()> fn, std::function<std::string()> value = {});
    MenuItem pageItem(std::string label, PageId id);
    MenuItem keyItem(std::string label, std::string& binding);

    ServiceHost& host_;
    config::Settings edit_;
    MenuPage page_;
    PageId pageId_ = PageId::Main;
    std::optional<PageId> pendingPage_;
    bool open_ = false;
    Capture capture_ = Capture::None;
    std::string* captureTarget_ = nullptr;
    int captureStation_ = -1;
    std::string captureLabel_;
    double captureTime_ = 0.0;
    std::string pendingConfirm_;
    double confirmTime_ = 0.0;
    double backHeld_ = 0.0;
    std::string notice_;
    double noticeTime_ = 0.0;
};

} // namespace spindle::menus
