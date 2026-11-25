#include "render/DisplaySetup.hpp"

#include <algorithm>
#include <format>

#include "engine/Branding.hpp"
#include "engine/Log.hpp"

namespace spindle::render {

bool Display::detect(int displayIndex, std::string* error) {
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    if (!ids || count <= 0) {
        if (ids) SDL_free(ids);
        if (error) *error = std::format("no displays found: {}", SDL_GetError());
        return false;
    }
    displayCount_ = count;
    int index = displayIndex;
    if (index < 0 || index >= count) {
        log::warn("display: display_index {} not present ({} display(s)); using 0", displayIndex, count);
        index = 0;
    }
    info_ = DisplayInfo{};
    info_.id = ids[index];
    info_.index = index;
    SDL_free(ids);
    const char* name = SDL_GetDisplayName(info_.id);
    info_.name = name ? name : "display";
    const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(info_.id);
    if (!desktop) {
        if (error) *error = std::format("cannot read desktop mode: {}", SDL_GetError());
        return false;
    }
    info_.desktopWidth = desktop->w;
    info_.desktopHeight = desktop->h;
    info_.refreshRate = desktop->refresh_rate;
    int modeCount = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(info_.id, &modeCount);
    if (modes) {
        for (int i = 0; i < modeCount; ++i) {
            std::pair<int, int> r{modes[i]->w, modes[i]->h};
            if (std::find(info_.resolutions.begin(), info_.resolutions.end(), r) == info_.resolutions.end())
                info_.resolutions.push_back(r);
        }
        SDL_free(modes);
    }
    if (info_.resolutions.empty()) info_.resolutions.push_back({info_.desktopWidth, info_.desktopHeight});
    std::sort(info_.resolutions.begin(), info_.resolutions.end(), [](auto a, auto b) { return a.first * a.second > b.first * b.second; });
    log::info("display: {} #{} desktop {}x{} @ {:.2f} Hz, {} mode(s)", info_.name, index, info_.desktopWidth, info_.desktopHeight,
              static_cast<double>(info_.refreshRate), info_.resolutions.size());
    return true;
}

bool Display::create(const config::Settings::Display& s, std::string* error) {
    const int w = s.resolution.automatic ? info_.desktopWidth : s.resolution.width;
    const int h = s.resolution.automatic ? info_.desktopHeight : s.resolution.height;
    const std::string title(kTitle);
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, static_cast<Sint64>(SDL_WINDOWPOS_CENTERED_DISPLAY(info_.id)));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, static_cast<Sint64>(SDL_WINDOWPOS_CENTERED_DISPLAY(info_.id)));
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    window_.reset(SDL_CreateWindowWithProperties(props));
    SDL_DestroyProperties(props);
    if (!window_) {
        if (error) *error = std::format("cannot create window: {}", SDL_GetError());
        return false;
    }
    renderer_.reset(SDL_CreateRenderer(window_.get(), nullptr));
    if (!renderer_) {
        if (error) *error = std::format("cannot create renderer: {}", SDL_GetError());
        return false;
    }
    const char* rname = SDL_GetRendererName(renderer_.get());
    rendererName_ = rname ? rname : "unknown";
    if (!applyMode(s, error)) return false;
    log::info("display: window ready, {}", describe());
    return true;
}

bool Display::applyMode(const config::Settings::Display& s, std::string* error) {
    SDL_Window* win = window_.get();
    const int w = s.resolution.automatic ? info_.desktopWidth : s.resolution.width;
    const int h = s.resolution.automatic ? info_.desktopHeight : s.resolution.height;
    switch (s.fullscreen) {
    case config::FullscreenMode::Borderless:
        SDL_SetWindowFullscreenMode(win, nullptr);
        if (!SDL_SetWindowFullscreen(win, true)) log::warn("display: borderless fullscreen refused: {}", SDL_GetError());
        break;
    case config::FullscreenMode::Exclusive: {
        SDL_DisplayMode mode{};
        if (SDL_GetClosestFullscreenDisplayMode(info_.id, w, h, 0.0f, true, &mode)) {
            if (mode.w != w || mode.h != h) log::warn("display: {}x{} not available; using closest mode {}x{}", w, h, mode.w, mode.h);
            SDL_SetWindowFullscreenMode(win, &mode);
        } else {
            log::warn("display: no exclusive mode near {}x{} ({}); using borderless", w, h, SDL_GetError());
            SDL_SetWindowFullscreenMode(win, nullptr);
        }
        if (!SDL_SetWindowFullscreen(win, true)) log::warn("display: exclusive fullscreen refused: {}", SDL_GetError());
        break;
    }
    case config::FullscreenMode::Windowed: {
        SDL_SetWindowFullscreen(win, false);
        SDL_Rect usable{};
        int ww = w, hh = h;
        if (SDL_GetDisplayUsableBounds(info_.id, &usable)) {
            ww = std::min(ww, usable.w);
            hh = std::min(hh, usable.h);
        }
        SDL_SetWindowSize(win, ww, hh);
        SDL_SetWindowPosition(win, static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(info_.id)),
                              static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(info_.id)));
        break;
    }
    }
    SDL_SyncWindow(win);
    mode_ = s.fullscreen;
    vsync_ = SDL_SetRenderVSync(renderer_.get(), s.vsync ? 1 : 0) && s.vsync;
    if (s.vsync && !vsync_) log::warn("display: vsync unavailable on renderer {}: {}", rendererName_, SDL_GetError());
    (void)error;
    return true;
}

bool Display::apply(const config::Settings::Display& s, std::string* error) {
    if (!window_) return false;
    if (s.displayIndex != info_.index) {
        std::string err;
        if (!detect(s.displayIndex, &err)) {
            if (error) *error = err;
            return false;
        }
        // Leave fullscreen and move to the new monitor first; fullscreen always uses the window's monitor.
        SDL_SetWindowFullscreen(window_.get(), false);
        SDL_SyncWindow(window_.get());
        SDL_SetWindowPosition(window_.get(), static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(info_.id)),
                              static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(info_.id)));
        SDL_SyncWindow(window_.get());
    }
    const bool ok = applyMode(s, error);
    log::info("display: applied, {}", describe());
    return ok;
}

void Display::show() {
    if (!window_) return;
    SDL_ShowWindow(window_.get());
    SDL_RaiseWindow(window_.get());
    SDL_HideCursor();
    SDL_DisableScreenSaver();
}

std::pair<int, int> Display::pixelSize() const {
    int w = 0, h = 0;
    if (renderer_) SDL_GetCurrentRenderOutputSize(renderer_.get(), &w, &h);
    if ((w <= 0 || h <= 0) && window_) SDL_GetWindowSizeInPixels(window_.get(), &w, &h);
    return {std::max(1, w), std::max(1, h)};
}

std::string Display::describe() const {
    const auto [w, h] = pixelSize();
    const char* mode = mode_ == config::FullscreenMode::Borderless  ? "borderless"
                       : mode_ == config::FullscreenMode::Exclusive ? "exclusive"
                                                                    : "windowed";
    return std::format("{}x{} px, {}, renderer {}, vsync {}", w, h, mode, rendererName_, vsync_ ? "on" : "off");
}

} // namespace spindle::render
