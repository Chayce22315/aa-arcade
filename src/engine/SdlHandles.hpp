#pragma once

#include <memory>

#include <SDL3/SDL.h>

namespace spindle::sdl {

struct WindowDeleter {
    void operator()(SDL_Window* w) const { SDL_DestroyWindow(w); }
};
struct RendererDeleter {
    void operator()(SDL_Renderer* r) const { SDL_DestroyRenderer(r); }
};
struct GamepadDeleter {
    void operator()(SDL_Gamepad* g) const { SDL_CloseGamepad(g); }
};
struct JoystickDeleter {
    void operator()(SDL_Joystick* j) const { SDL_CloseJoystick(j); }
};
struct AudioStreamDeleter {
    void operator()(SDL_AudioStream* s) const { SDL_DestroyAudioStream(s); }
};
struct SurfaceDeleter {
    void operator()(SDL_Surface* s) const { SDL_DestroySurface(s); }
};

using WindowPtr = std::unique_ptr<SDL_Window, WindowDeleter>;
using RendererPtr = std::unique_ptr<SDL_Renderer, RendererDeleter>;
using GamepadPtr = std::unique_ptr<SDL_Gamepad, GamepadDeleter>;
using JoystickPtr = std::unique_ptr<SDL_Joystick, JoystickDeleter>;
using AudioStreamPtr = std::unique_ptr<SDL_AudioStream, AudioStreamDeleter>;
using SurfacePtr = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

// Owns SDL_Init/SDL_Quit for the lifetime of the application.
class SdlRuntime {
public:
    SdlRuntime() = default;
    ~SdlRuntime() {
        if (initialized_) SDL_Quit();
    }
    SdlRuntime(const SdlRuntime&) = delete;
    SdlRuntime& operator=(const SdlRuntime&) = delete;
    bool init(SDL_InitFlags flags) {
        initialized_ = SDL_Init(flags);
        return initialized_;
    }

private:
    bool initialized_ = false;
};

} // namespace spindle::sdl
