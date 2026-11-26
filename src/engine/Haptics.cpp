#include "engine/Haptics.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "engine/Log.hpp"
#include "engine/Platform.hpp"
#include "input/DeviceRegistry.hpp"

namespace spindle {
namespace {

constexpr std::uint64_t kDoubleGapNs = 120'000'000;
constexpr std::size_t kMaxPending = 16;

} // namespace

Haptics::~Haptics() { close(); }

void Haptics::open(const input::DeviceRegistry& devices) {
    devices_ = &devices;
    if constexpr (platform::kIos) {
        ios::hapticPrepare();
        log::info("haptics: UIKit feedback generators ready");
        return;
    }
    if constexpr (!platform::kAndroid) return; // desktops: gamepad rumble only, which needs no haptic subsystem
    if (!SDL_InitSubSystem(SDL_INIT_HAPTIC)) {
        log::warn("haptics: subsystem unavailable ({}); no vibration", SDL_GetError());
        return;
    }
    subsystem_ = true;
    int count = 0;
    SDL_HapticID* ids = SDL_GetHaptics(&count);
    for (int i = 0; ids && i < count && !phone_; ++i) {
        const char* name = SDL_GetHapticNameForID(ids[i]);
        if (!name || std::string_view(name).find("VIBRATOR") == std::string_view::npos) continue;
        phone_ = SDL_OpenHaptic(ids[i]);
        if (phone_ && !SDL_InitHapticRumble(phone_)) {
            log::warn("haptics: the vibrator cannot rumble ({})", SDL_GetError());
            SDL_CloseHaptic(phone_);
            phone_ = nullptr;
        }
    }
    SDL_free(ids);
    log::info("haptics: {}", phone_ ? "phone vibrator ready" : "no phone vibrator found");
}

void Haptics::close() {
    if (phone_) SDL_CloseHaptic(phone_);
    phone_ = nullptr;
    if (subsystem_) SDL_QuitSubSystem(SDL_INIT_HAPTIC);
    subsystem_ = false;
    pending_.clear();
    devices_ = nullptr;
}

bool Haptics::phoneAvailable() const {
    if constexpr (platform::kIos) return true;
    return phone_ != nullptr;
}

void Haptics::pulse(arcade::HapticKind kind, int station, bool phone) {
    const arcade::HapticPattern p = arcade::hapticPattern(kind);
    const float strength = std::clamp(p.strength * static_cast<float>(cfg_.strength) / 100.0f, 0.0f, 1.0f);
    if (strength <= 0.0f) return;
    if (station >= 0 && cfg_.gamepadRumble && devices_) {
        // Looked up per pulse: a pad unplugged between the two taps of a double is simply skipped.
        if (const input::InputDevice* d = devices_->controllerForStation(station); d && d->gamepad) {
            const auto level = static_cast<Uint16>(std::lround(strength * 65535.0f));
            SDL_RumbleGamepad(d->gamepad.get(), level, level, p.milliseconds);
        }
    }
    if (phone && cfg_.vibration) {
        if constexpr (platform::kIos) {
            ios::haptic(kind, strength);
        } else if (phone_) {
            SDL_PlayHapticRumble(phone_, strength, p.milliseconds);
        }
    }
}

void Haptics::play(arcade::HapticKind kind, int station, bool phone, std::uint64_t nowNs) {
    const bool pad = station >= 0 && cfg_.gamepadRumble;
    if (!pad && !(phone && cfg_.vibration)) return;
    ++requests_;
    pulse(kind, station, phone);
    if (arcade::hapticPattern(kind).doublePulse && pending_.size() < kMaxPending)
        pending_.push_back({nowNs + kDoubleGapNs, kind, station, phone});
}

void Haptics::update(std::uint64_t nowNs) {
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (nowNs < it->at) {
            ++it;
            continue;
        }
        const Pending p = *it;
        it = pending_.erase(it);
        pulse(p.kind, p.station, p.phone);
    }
}

#if !defined(SDL_PLATFORM_IOS)
namespace ios {
void hapticPrepare() {}
void haptic(arcade::HapticKind, float) {}
} // namespace ios
#endif

} // namespace spindle
