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
    virtual config::Settings defaultSettings() const = 0; // what "reset defaults" restores on this platform
    virtual bool touchUi() const = 0;                      // phones and tablets: show touch controls
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
    virtual void saveHighScores() = 0;
    virtual void requestShutdown() = 0;
    virtual double frameMilliseconds() const = 0;
};

// Operator service / test menu. Edits a working copy of the settings; Save & Exit commits it.
class ServiceMenu {
public:
    explicit ServiceMenu(ServiceHost& host);

    // byTouch: opened from the hidden corner, so show the touch controls even on a cabinet.
    void open(bool byTouch = false);
    bool isOpen() const { return open_; }
    void update(const input::TickInput& in, double dt);
    // Same as the back key: leaves a sub page; on the main page moves to "save & exit".
    void back();
    void draw(render::Renderer2D& r, const render::ScreenLayout& layout, double time);

    // Introspection for the self-test.
    std::string pageTitle() const { return page_.title(); }
    std::string selectedLabel() const;
    const config::Settings& edited() const { return edit_; }
    int selectedIndex() const { return page_.selected(); }
    bool touchMode() const { return touchMode_; }
    const render::Rect& backButton() const { return backButton_; }
    // Centre of a row as last drawn (screen pixels), if it is on screen.
    std::optional<render::Vec2> rowPoint(int index) const;

private:
    enum class PageId { Main, InputTest, Assignment, Keybinds, DisplayTest, Video, SoundTest, Volumes, Game, Coin, Bots, Bookkeeping, HighScores };
    enum class Capture { None, Key, Controller, Keyboard };

    void show(PageId id);
    void build(PageId id);
    void close(bool save);
    bool confirm(const std::string& key);
    void handleTap(float nx, float ny);
    void drawInputTest(render::Renderer2D& r, const render::ScreenLayout& l, double time);
    void drawDisplayTest(render::Renderer2D& r, const render::ScreenLayout& l, double time);

    MenuItem intItem(std::string label, int& ref, int lo, int hi, int step = 1, std::string suffix = {});
    MenuItem realItem(std::string label, double& ref, double lo, double hi, double step);
    MenuItem boolItem(std::string label, bool& ref);
    MenuItem actionItem(std::string label, std::function<void()> fn, std::function<std::string()> value = {});
    MenuItem pageItem(std::string label, PageId id);
    MenuItem keyItem(std::string label, std::string& binding);

    ServiceHost& host_;
    // Touch: where the last frame drew its rows and the back button (screen pixels).
    struct RowHit {
        float y0 = 0.0f;
        float y1 = 0.0f;
        int index = 0;
    };
    std::vector<RowHit> rowHits_;
    render::Rect backButton_{};
    float valueZoneX_ = 0.0f;
    float valueZoneEnd_ = 0.0f;
    render::ScreenLayout lastLayout_{};
    bool touchMode_ = false;
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
    bool rebuildPending_ = false; // rebuild the page on the next update (never from inside one of its items)
    double confirmTime_ = 0.0;
    double backHeld_ = 0.0;
    std::string notice_;
    double noticeTime_ = 0.0;
};

} // namespace spindle::menus
