#pragma once

#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "config/Settings.hpp"
#include "engine/SdlHandles.hpp"

namespace spindle::render {

struct DisplayInfo {
    SDL_DisplayID id = 0;
    int index = 0;
    std::string name;
    int desktopWidth = 0;
    int desktopHeight = 0;
    float refreshRate = 0.0f;
    std::vector<std::pair<int, int>> resolutions; // unique fullscreen sizes, largest first
};

// Owns the window and renderer. Boot steps 5-8: detect the display, read the configured mode, create
// the (hidden) window at that mode, then report the pixel size for display-dependent resources.
class Display {
public:
    bool detect(int displayIndex, std::string* error);
    bool create(const config::Settings::Display& settings, std::string* error);
    // Live re-apply from the service menu (fullscreen mode, resolution, vsync, monitor).
    bool apply(const config::Settings::Display& settings, std::string* error);
    void show();

    SDL_Window* window() const { return window_.get(); }
    SDL_Renderer* renderer() const { return renderer_.get(); }
    const DisplayInfo& info() const { return info_; }
    int displayCount() const { return displayCount_; }

    // Current output size in pixels (changes on resize, DPI or mode changes).
    std::pair<int, int> pixelSize() const;
    const std::string& rendererName() const { return rendererName_; }
    bool vsyncActive() const { return vsync_; }
    std::string describe() const;

private:
    bool applyMode(const config::Settings::Display& settings, std::string* error);

    DisplayInfo info_;
    int displayCount_ = 0;
    sdl::WindowPtr window_;
    sdl::RendererPtr renderer_;
    std::string rendererName_;
    bool vsync_ = false;
    config::FullscreenMode mode_ = config::FullscreenMode::Windowed;
};

} // namespace spindle::render
