#pragma once

#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>

#include "arcade/Haptics.hpp"
#include "config/Settings.hpp"

namespace spindle::input {
class DeviceRegistry;
}

// Turns the cabinet's haptic moments into feel: a rumble on the station's own gamepad, or a phone
// vibration for touch players. Android vibrates through SDL's haptic device for the phone's vibrator;
// iOS through UIKit's feedback generators (SDL has no iPhone haptics), see engine/ios/HapticsIos.mm.
namespace spindle {

class Haptics {
public:
    Haptics() = default;
    ~Haptics();
    Haptics(const Haptics&) = delete;
    Haptics& operator=(const Haptics&) = delete;

    // devices must outlive this object (or close() must run first).
    void open(const input::DeviceRegistry& devices);
    void close();
    void configure(const config::Settings::Feedback& feedback) { cfg_ = feedback; }

    // station: whose gamepad rumbles (-1 = none); phone: vibrate the device itself.
    void play(arcade::HapticKind kind, int station, bool phone, std::uint64_t nowNs);
    void update(std::uint64_t nowNs); // second pulses of double taps
    int requests() const { return requests_; } // moments that reached a pad or the phone since boot
    bool phoneAvailable() const;

private:
    void pulse(arcade::HapticKind kind, int station, bool phone);

    const input::DeviceRegistry* devices_ = nullptr;
    config::Settings::Feedback cfg_{};
    SDL_Haptic* phone_ = nullptr; // Android: the vibrator
    bool subsystem_ = false;
    struct Pending {
        std::uint64_t at = 0;
        arcade::HapticKind kind = arcade::HapticKind::Tick;
        int station = -1;
        bool phone = false;
    };
    std::vector<Pending> pending_;
    int requests_ = 0;
};

namespace ios {
// Implemented in HapticsIos.mm on iOS; no-ops elsewhere.
void hapticPrepare();
void haptic(arcade::HapticKind kind, float strength);
} // namespace ios

} // namespace spindle
