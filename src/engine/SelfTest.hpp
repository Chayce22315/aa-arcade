#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "config/Settings.hpp"

namespace spindle {

class App;

// Scripted end-to-end test of the real executable (`spindle --selftest DIR`). It drives the cabinet
// through SDL itself: keyboard events are pushed into SDL's queue and controllers are SDL virtual
// joysticks (an Xbox-type pad, a PlayStation-type pad and an unmapped arcade stick), so the same
// event, hotplug and assignment code runs as with real hardware. It writes screenshots and
// selftest_report.txt to DIR and exits 0 only if every step passed.
class SelfTest {
public:
    static constexpr int kTicksPerFrame = 24; // render 10 times per simulated second to keep the run fast

    SelfTest(App& app, std::filesystem::path outDir);
    ~SelfTest();

    static config::Settings adjustSettings(config::Settings s);

    void beforeTick();
    void afterTick() {}

private:
    enum class CtlKind { None, Keyboard, Pad, Stick };
    struct Controller {
        CtlKind kind = CtlKind::None;
        SDL_Scancode fireKey = SDL_SCANCODE_UNKNOWN;
        SDL_Scancode startKey = SDL_SCANCODE_UNKNOWN;
        SDL_JoystickID id = 0;
        SDL_Joystick* joystick = nullptr;
        int fireButton = 0;
        int startButton = 0;
        int lastPressTick = -1000;
    };
    struct Step {
        std::string name;
        std::function<bool()> run;
        double timeout = 30.0;
    };
    struct Result {
        std::string name;
        bool ok = false;
        double seconds = 0.0;
        std::string detail;
    };
    enum class Policy { Idle, Oracle, Spam };

    void add(std::string name, std::function<bool()> run, double timeout = 30.0);
    void build();
    void finish(bool ok);
    void check(bool condition, const std::string& what);
    void screenshot(const std::string& name);

    void tapKey(SDL_Scancode sc);
    void pressFire(int station);
    void pressStart(int station);
    void setButton(Controller& c, int button, bool down);
    bool attachPad(int station, Uint16 vendor, Uint16 product, const char* name);
    bool attachStick(int station);
    void detach(int station);
    bool oracleWantsFire(int station) const;
    void drive(const std::array<Policy, 4>& policy);

    App& app_;
    std::filesystem::path out_;
    std::vector<Step> steps_;
    std::vector<Result> results_;
    std::size_t current_ = 0;
    double stepTime_ = 0.0;
    int tick_ = 0;
    std::string failures_;
    std::array<Controller, 4> ctl_{};
    std::vector<std::pair<int, std::function<void()>>> releases_;
    std::vector<std::string> shots_;
    std::uint64_t frozenTick_ = 0;
    double waitStart_ = 0.0;
    bool done_ = false;
};

} // namespace spindle
