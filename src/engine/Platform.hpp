#pragma once

#include <SDL3/SDL_platform_defines.h>

// Build-time platform facts the platform layer branches on. The core never needs them.
namespace spindle::platform {

#if defined(SDL_PLATFORM_ANDROID)
inline constexpr bool kAndroid = true;
#else
inline constexpr bool kAndroid = false;
#endif

#if defined(SDL_PLATFORM_IOS)
inline constexpr bool kIos = true;
#else
inline constexpr bool kIos = false;
#endif

// Phones and tablets: touch first, always full screen, no coin slot, the app can be sent to the background.
inline constexpr bool kMobile = kAndroid || kIos;

} // namespace spindle::platform
