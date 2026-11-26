#include "engine/SelfTest.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <memory>

#include "collision/Collision.hpp"
#include "config/AtomicFile.hpp"
#include "engine/App.hpp"
#include "engine/Branding.hpp"
#include "engine/Log.hpp"
#include "gameplay/Geometry.hpp"

namespace spindle {
namespace {

using arcade::ArcadeState;
using arcade::AttractPage;
namespace geom = gameplay::geom;

constexpr double kDt = geom::kTickSeconds;

} // namespace

config::Settings SelfTest::adjustSettings(config::Settings s) {
    s.gameplay.seed = 4242;
    s.gameplay.readyDelay = 0.75;
    s.arcade.freePlay = false; // the script inserts coins (phones default to free play)
    // A shorter fire meter so the scripted match is sure to set someone on fire.
    s.gameplay.fireMeter = true;
    s.gameplay.fireCombo = 4;
    s.gameplay.fireChainWindow = 2.0;
    s.arcade.voteSeconds = 10;
    s.modes = config::Settings::defaults().modes; // every mode on: the script votes battle where it needs it
    s.bots.difficulty = config::Difficulty::Easy;  // the singleplayer bots finish their match sooner
    s.arcade.gameOverTime = 3;
    s.display.fullscreen = config::FullscreenMode::Windowed;
    s.display.resolution = config::Resolution{false, 1280, 720};
    s.display.vsync = false;
    return s;
}

SelfTest::SelfTest(App& app, std::filesystem::path outDir) : app_(app), out_(std::move(outDir)) {
    std::error_code ec;
    std::filesystem::create_directories(out_, ec);
    ctl_[0].kind = CtlKind::Keyboard;
    ctl_[0].fireKey = SDL_SCANCODE_LCTRL;
    ctl_[0].startKey = SDL_SCANCODE_1;
    build();
    log::info("selftest: {} steps, writing to {}", steps_.size(), pathToUtf8(out_));
}

SelfTest::~SelfTest() {
    for (int s = 0; s < 4; ++s) detach(s);
}

void SelfTest::add(std::string name, std::function<bool()> run, double timeout) {
    steps_.push_back({std::move(name), std::move(run), timeout});
}

void SelfTest::check(bool condition, const std::string& what) {
    if (condition) return;
    failures_ += (failures_.empty() ? "" : "; ") + what;
}

void SelfTest::screenshot(const std::string& name) {
    const auto file = out_ / (name + ".bmp");
    if (app_.saveScreenshot(file))
        shots_.push_back(name + ".bmp");
    else
        check(false, "screenshot " + name + " failed");
}

// ------------------------------------------------------------------ input helpers

void SelfTest::tapKey(SDL_Scancode sc) {
    const auto send = [sc](bool down) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.scancode = sc;
        e.key.key = SDL_GetKeyFromScancode(sc, SDL_KMOD_NONE, false);
        e.key.down = down;
        e.key.which = 0;
        SDL_PushEvent(&e);
    };
    send(true);
    releases_.push_back({1, [send] { send(false); }});
}

void SelfTest::fingerEvent(Uint32 type, SDL_FingerID id, float x, float y) {
    SDL_Event e{};
    e.type = type;
    e.tfinger.touchID = 1;
    e.tfinger.fingerID = id;
    e.tfinger.x = x;
    e.tfinger.y = y;
    e.tfinger.pressure = 1.0f;
    e.tfinger.windowID = SDL_GetWindowID(app_.display().window());
    SDL_PushEvent(&e);
}

void SelfTest::tapScreen(float x, float y) {
    const SDL_FingerID id = nextFinger_++;
    fingerEvent(SDL_EVENT_FINGER_DOWN, id, x, y);
    releases_.push_back({1, [this, id, x, y] { fingerEvent(SDL_EVENT_FINGER_UP, id, x, y); }});
}

void SelfTest::setButton(Controller& c, int button, bool down) {
    if (c.joystick) SDL_SetJoystickVirtualButton(c.joystick, button, down);
}

void SelfTest::pressFire(int station) {
    Controller& c = ctl_[static_cast<std::size_t>(station)];
    c.lastPressTick = tick_;
    if (c.kind == CtlKind::Keyboard) {
        tapKey(c.fireKey);
    } else if (c.joystick) {
        setButton(c, c.fireButton, true);
        Controller* pc = &c;
        releases_.push_back({1, [this, pc] { setButton(*pc, pc->fireButton, false); }});
    }
}

void SelfTest::pressStart(int station) {
    Controller& c = ctl_[static_cast<std::size_t>(station)];
    if (c.kind == CtlKind::Keyboard) {
        tapKey(c.startKey);
    } else if (c.joystick) {
        setButton(c, c.startButton, true);
        Controller* pc = &c;
        releases_.push_back({1, [this, pc] { setButton(*pc, pc->startButton, false); }});
    }
}

void SelfTest::pressButton(int station, int button) {
    Controller& c = ctl_[static_cast<std::size_t>(station)];
    if (!c.joystick) return;
    setButton(c, button, true);
    Controller* pc = &c;
    releases_.push_back({1, [this, pc, button] { setButton(*pc, button, false); }});
}

bool SelfTest::attachPad(int station, Uint16 vendor, Uint16 product, const char* name) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.vendor_id = vendor;
    desc.product_id = product;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1u;
    desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1u;
    desc.name = name;
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        check(false, std::format("attach {} failed: {}", name, SDL_GetError()));
        return false;
    }
    Controller& c = ctl_[static_cast<std::size_t>(station)];
    c.kind = CtlKind::Pad;
    c.id = id;
    c.joystick = SDL_OpenJoystick(id);
    c.fireButton = SDL_GAMEPAD_BUTTON_SOUTH;
    c.startButton = SDL_GAMEPAD_BUTTON_START;
    return c.joystick != nullptr;
}

bool SelfTest::attachStick(int station) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_ARCADE_STICK;
    desc.vendor_id = 0x1209; // pid.codes open-source vendor id
    desc.product_id = 0x5A57;
    desc.naxes = 2;
    desc.nbuttons = 8;
    desc.nhats = 1;
    desc.name = "selftest arcade stick";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    if (id == 0) {
        check(false, std::format("attach arcade stick failed: {}", SDL_GetError()));
        return false;
    }
    Controller& c = ctl_[static_cast<std::size_t>(station)];
    c.kind = CtlKind::Stick;
    c.id = id;
    c.joystick = SDL_OpenJoystick(id);
    c.fireButton = 0;
    c.startButton = 5;
    return c.joystick != nullptr;
}

void SelfTest::detach(int station) {
    Controller& c = ctl_[static_cast<std::size_t>(station)];
    if (c.kind != CtlKind::Pad && c.kind != CtlKind::Stick) return;
    if (c.joystick) SDL_CloseJoystick(c.joystick);
    SDL_DetachVirtualJoystick(c.id);
    c.joystick = nullptr;
    c.id = 0;
    c.kind = CtlKind::None;
}

bool SelfTest::oracleWantsFire(int s) const {
    const gameplay::Match* m = app_.arcadeMachine().match();
    if (!m || m->phase() != gameplay::MatchPhase::Playing || !m->stationInRound(s)) return false;
    const gameplay::Playfield& f = m->field(s);
    if (!f.canFireAt(m->tickCount() + 1) || f.inFlight() > 0) return false;
    // On fire every needle burns through, so a careful player fires at once.
    if (f.onFire(m->tickCount() + 1)) return true;
    const auto& cfg = f.config();
    const double flight = (cfg.launchDistance - cfg.needleLength) / f.needleSpeed(); // swift flies faster
    const auto& r = m->rotor().state();
    const double look = flight + kDt;
    const double land = collision::wrapAngle(geom::kAttachAngle - (r.angle + r.speed * look + 0.5 * r.accel * look * look));
    double clearance = geom::kPi;
    for (double a : f.attachedAngles()) clearance = std::min(clearance, collision::angularDistance(land, a));
    // A careful player goes for a powerup ring passing the launcher when the landing is safe.
    for (const auto& ring : f.pickups())
        if (!ring.claimed && collision::angularDistance(land, ring.pickup.angle) < f.claimAngle() * 0.5 &&
            clearance >= 1.3 * f.minSeparation())
            return true;
    return clearance >= 2.5 * f.minSeparation();
}

void SelfTest::drive(const std::array<Policy, 4>& policy) {
    const gameplay::Match* m = app_.arcadeMachine().match();
    if (!m || m->phase() != gameplay::MatchPhase::Playing || m->paused()) return;
    for (int s = 0; s < 4; ++s) {
        const Controller& c = ctl_[static_cast<std::size_t>(s)];
        if (c.kind == CtlKind::None || tick_ - c.lastPressTick < 2) continue;
        bool fire = false;
        switch (policy[static_cast<std::size_t>(s)]) {
        case Policy::Idle: break;
        case Policy::Oracle: fire = oracleWantsFire(s); break;
        case Policy::Spam: fire = m->stationInRound(s) && m->field(s).canFireAt(m->tickCount() + 1); break;
        }
        if (fire) pressFire(s);
    }
}

// ------------------------------------------------------------------ the scenario

void SelfTest::build() {
    using P = Policy;
    auto A = [this]() -> arcade::ArcadeMachine& { return app_.arcadeMachine(); };
    auto M = [this]() { return app_.arcadeMachine().match(); };
    auto once = [](std::function<void()> fn) {
        return [fn] {
            fn();
            return true;
        };
    };
    auto shot = [this, once](const std::string& name) { add("screenshot " + name, once([this, name] { screenshot(name); })); };
    auto taps = [this](SDL_Scancode sc, int count) {
        auto n = std::make_shared<int>(0);
        return [this, sc, count, n] {
            if (*n >= count) return true;
            if (tick_ % 4 == 0) {
                tapKey(sc);
                ++*n;
            }
            return false;
        };
    };
    auto serviceSelect = [this](std::string label) {
        auto pressed = std::make_shared<int>(0);
        return [this, label, pressed] {
            auto& sm = app_.serviceMenu();
            if (*pressed > 0) return ++*pressed > 3;
            if (tick_ % 3 != 0) return false;
            if (sm.selectedLabel() == label) {
                tapKey(SDL_SCANCODE_RETURN);
                *pressed = 1;
            } else {
                tapKey(SDL_SCANCODE_DOWN);
            }
            return false;
        };
    };
    auto waitFor = [this](std::string name, std::function<bool()> pred, double timeout = 30.0) { add(std::move(name), std::move(pred), timeout); };
    auto settle = [](double seconds) {
        auto t = std::make_shared<double>(0.0);
        return [t, seconds] {
            *t += kDt;
            return *t >= seconds;
        };
    };
    auto joinTwoKeyboardPlayers = [this, taps, waitFor, A](const std::string& tag) {
        add(tag + ": P2 uses the keyboard", [this] {
            ctl_[1] = Controller{};
            ctl_[1].kind = CtlKind::Keyboard;
            ctl_[1].fireKey = SDL_SCANCODE_A;
            ctl_[1].startKey = SDL_SCANCODE_2;
            return true;
        });
        add(tag + ": insert 2 coins", taps(SDL_SCANCODE_5, 2));
        waitFor(tag + ": 2 credits", [A] { return A().credits().credits() == 2; });
        add(tag + ": P1 start", [this] { tapKey(SDL_SCANCODE_1); return true; });
        waitFor(tag + ": P1 joined", [A] { return A().state() == ArcadeState::Vote && A().joined()[0]; });
        add(tag + ": P2 start", [this] { tapKey(SDL_SCANCODE_2); return true; });
        waitFor(tag + ": P2 joined", [A] { return A().joined()[1]; });
        add(tag + ": P1 and P2 vote battle (fire)", taps(SDL_SCANCODE_LCTRL, 1));
        add(tag + ": P2 votes too", taps(SDL_SCANCODE_A, 1));
        waitFor(tag + ": multiplayer, 2 humans, 0 bots", [A] {
            return A().state() == ArcadeState::Playing && A().lineup().mode == players::GameMode::Multiplayer && A().lineup().humans == 2 &&
                   A().lineup().bots == 0;
        });
    };

    // --- attract mode
    waitFor("attract: title page", [A] { return A().state() == ArcadeState::Attract && A().attractPage() == AttractPage::Title; }, 5);
    add("let the screen open (01_attract_title)", settle(1.6)); // the iris and any intro animation
    shot("01_attract_title");
    // A fresh install has not seen this version's card yet: it comes once, after the title.
    waitFor("attract: what's new page (once per version)", [A] { return A().attractPage() == AttractPage::WhatsNew; }, 12);
    add("let the lines pop in (25_whats_new)", settle(3.0));
    shot("25_whats_new");
    add("attract: the card is remembered as seen", [this, A] {
        check(A().bookkeeping().seenVersion == kVersion, std::format("seen version '{}'", A().bookkeeping().seenVersion));
        check(!A().whatsNewPending(), "what's new still pending");
        return true;
    });
    waitFor("attract: how-to page", [A] { return A().attractPage() == AttractPage::HowTo; }, 12);
    add("let the screen open (02_attract_howto)", settle(0.8)); // the iris and any intro animation
    shot("02_attract_howto");
    waitFor("attract: bot demo playing", [A, M] {
        return A().attractPage() == AttractPage::Demo && M() && M()->phase() == gameplay::MatchPhase::Playing && M()->roundTime() > 0.8;
    }, 20);
    add("attract: demo bots place needles", [this, M] {
        int placed = 0;
        for (int s = 0; s < 4; ++s) placed += M()->field(s).placed();
        check(placed > 0, "demo bots placed no needles");
        check(app_.arcadeMachine().lineup().bots == 4, "demo is not four bots");
        return true;
    });
    shot("03_attract_demo");

    // --- coins and four-station join through four different device types
    add("coin: insert 4 coins with the coin key", taps(SDL_SCANCODE_5, 4));
    waitFor("coin: insert-coin screen shows 4 credits", [A] { return A().state() == ArcadeState::InsertCoin && A().credits().credits() == 4; });
    add("let the screen open (04_insert_coin)", settle(0.5)); // the iris and any intro animation
    shot("04_insert_coin");
    add("controllers: attach xbox pad, playstation pad, arcade stick", once([this] {
        attachPad(1, 0x045e, 0x0b13, "selftest xbox pad");
        attachPad(2, 0x054c, 0x0ce6, "selftest playstation pad");
        attachStick(3);
    }));
    waitFor("controllers: three controllers registered", [this] {
        // The three the test attached (a phone may list more devices of its own).
        auto& reg = app_.inputSystem().devices();
        return reg.find(input::DeviceKind::Gamepad, ctl_[1].id) && reg.find(input::DeviceKind::Gamepad, ctl_[2].id) &&
               reg.find(input::DeviceKind::Joystick, ctl_[3].id);
    });
    add("controllers: device types recognised", [this] {
        auto& reg = app_.inputSystem().devices();
        const input::InputDevice* xbox = reg.find(input::DeviceKind::Gamepad, ctl_[1].id);
        const input::InputDevice* ps = reg.find(input::DeviceKind::Gamepad, ctl_[2].id);
        const input::InputDevice* stick = reg.find(input::DeviceKind::Joystick, ctl_[3].id);
        check(xbox && xbox->typeLabel == "xbox", std::format("xbox pad type '{}'", xbox ? xbox->typeLabel : "missing"));
        check(ps && ps->typeLabel == "playstation", std::format("playstation pad type '{}'", ps ? ps->typeLabel : "missing"));
        check(stick != nullptr, "arcade stick not registered as an unmapped joystick");
        return true;
    });
    add("join: P1 presses start on the keyboard", [this] { tapKey(SDL_SCANCODE_1); return true; });
    waitFor("join: P1 joined", [A] { return A().state() == ArcadeState::Vote && A().joined()[0]; });
    add("join: start on the xbox pad", [this] { pressStart(1); return true; });
    waitFor("join: xbox pad claimed station 2", [this, A] {
        const auto* d = app_.inputSystem().devices().controllerForStation(1);
        return A().joined()[1] && d && d->id == ctl_[1].id;
    });
    add("join: start on the playstation pad", [this] { pressStart(2); return true; });
    waitFor("join: playstation pad claimed station 3", [this, A] {
        const auto* d = app_.inputSystem().devices().controllerForStation(2);
        return A().joined()[2] && d && d->id == ctl_[2].id;
    });
    add("join: any button on the arcade stick", [this] { pressStart(3); return true; });
    waitFor("join: the arcade stick claimed station 4", [A] { return A().joined()[3]; });
    // The vote: P3 looks at marathon first, then everyone votes battle.
    add("vote: P3 moves right with the d-pad", [this] { pressButton(2, SDL_GAMEPAD_BUTTON_DPAD_RIGHT); return true; });
    add("vote: P1 votes battle (left ctrl)", taps(SDL_SCANCODE_LCTRL, 1));
    add("vote: P2 votes battle (pad)", [this] { pressFire(1); return true; });
    add("vote: let it draw", settle(0.3));
    add("vote: P3's cursor is on marathon, two votes for battle", [this, A] {
        check(A().vote().cursor(2) == 1, std::format("P3 cursor {}", A().vote().cursor(2)));
        check(A().vote().votesFor(0) == 2, std::format("battle votes {}", A().vote().votesFor(0)));
        return true;
    });
    shot("05_vote");
    add("vote: P3 moves back left", [this] { pressButton(2, SDL_GAMEPAD_BUTTON_DPAD_LEFT); return true; });
    add("vote: P3 votes battle", [this] { pressFire(2); return true; });
    add("vote: P4 votes battle (stick)", [this] { pressFire(3); return true; });
    waitFor("vote: battle wins outright - its card shows", [A] {
        return A().state() == ArcadeState::Vote && A().votePhase() == arcade::VotePhase::Intro && A().chosenMode() == gameplay::ModeId::Battle;
    }, 15);
    add("vote: let the card grow", settle(0.6));
    shot("23_mode_intro");
    waitFor("match: four humans, multiplayer, started at once", [A] {
        return A().state() == ArcadeState::Playing && A().lineup().humans == 4 && A().lineup().mode == players::GameMode::Multiplayer &&
               A().lineup().bots == 0;
    });

    // --- the four-station match
    waitFor("round 1: the round end screen types the scores in", [this, M] {
        if (!M()) return false;
        drive({P::Spam, P::Oracle, P::Oracle, P::Oracle});
        return M()->phase() == gameplay::MatchPhase::Tally && M()->phaseTime() >= M()->tallyTiming().breakdownStart() + 0.9;
    }, 120);
    add("round 1: every player is tallied, the careful ones scored", [this, M] {
        for (int s = 0; s < 4; ++s) check(M()->tallied(s), std::format("P{} not on the round end screen", s + 1));
        for (int s = 1; s < 4; ++s) check(M()->participant(s)->score > 0, std::format("P{} scored nothing", s + 1));
        check(M()->participant(1)->roundBonus > 0, "no clear bonus for P2");
        // The pads on P2/P3 (and a phone's touch player) were asked to rumble for their needles.
        check(app_.haptics_.requests() > 0, "no haptic moment reached a pad or the phone");
        log::info("selftest: {} haptic moments so far", app_.haptics_.requests());
        return true;
    });
    shot("18_round_end");
    waitFor("round 1: P1 misses, P2-P4 play carefully", [this, M] {
        if (!M()) return false;
        drive({P::Spam, P::Oracle, P::Oracle, P::Oracle});
        return M()->round() == 2 && M()->phase() == gameplay::MatchPhase::Ready;
    }, 120);
    add("round 1: only P1 lost a try (stations are independent)", [this, M] {
        check(M()->participant(0)->tries == 2, std::format("P1 tries {}", M()->participant(0)->tries));
        for (int s = 1; s < 4; ++s) check(M()->participant(s)->tries == 3, std::format("P{} tries {}", s + 1, M()->participant(s)->tries));
        for (int s = 1; s < 4; ++s) check(M()->participant(s)->roundsCleared == 1, std::format("P{} did not clear round 1", s + 1));
        (void)this;
        return true;
    });
    waitFor("round 2: play for a second", [this, M] {
        if (!M()) return false;
        drive({P::Spam, P::Oracle, P::Oracle, P::Oracle});
        return M()->round() == 2 && M()->phase() == gameplay::MatchPhase::Playing && M()->roundTime() > 1.0;
    }, 30);
    shot("06_match");
    add("disconnect: unplug the xbox pad mid-round", once([this] { detach(1); }));
    waitFor("disconnect: match pauses for station 2", [A] { return A().pausedForDisconnect() && A().stationWaitingForReconnect(1); });
    shot("07_disconnected");
    add("disconnect: remember the frozen tick", once([this, M] { frozenTick_ = M()->tickCount(); }));
    add("disconnect: one second passes", settle(1.0));
    add("disconnect: nothing moved while paused", [this, M] {
        check(M()->tickCount() == frozenTick_, "match advanced while paused");
        return true;
    });
    add("reconnect: plug an xbox pad back in", once([this] { attachPad(1, 0x045e, 0x0b13, "selftest xbox pad"); }));
    waitFor("reconnect: it re-attaches to station 2 and play resumes", [this, A] {
        const auto* d = app_.inputSystem().devices().controllerForStation(1);
        return !A().pausedForDisconnect() && d && d->id == ctl_[1].id;
    });
    const auto driveMatch = [this, A, M] {
        const gameplay::Match* m = M();
        if (A().state() != ArcadeState::Playing || !m) return;
        const int r = m->round();
        drive({P::Spam, r >= 2 ? P::Spam : P::Oracle, r >= 3 ? P::Spam : P::Oracle, P::Oracle});
    };
    waitFor("crown: it flies to the round's outright winner", [A, M, driveMatch] {
        driveMatch();
        const gameplay::Match* m = M();
        if (A().state() != ArcadeState::Playing || !m) return true;
        const auto& t = m->tallyTiming();
        return m->phase() == gameplay::MatchPhase::Tally && m->crownHolder() >= 0 && m->crownHolder() != m->crownBefore() &&
               m->phaseTime() >= t.crownStart() + t.crown * 0.88;
    }, 240);
    add("crown: someone holds it", [this, M] {
        check(M() && M()->crownHolder() >= 0, "nobody took the crown");
        (void)this;
        return true;
    });
    shot("19_crown");
    waitFor("round 4: powerup rings on the circles", [this, A, M] {
        const gameplay::Match* m = M();
        if (A().state() != ArcadeState::Playing || !m) return true;
        const int r = m->round();
        drive({P::Spam, r >= 2 ? P::Spam : P::Oracle, r >= 3 ? P::Spam : P::Oracle, P::Oracle});
        if (r != 4 || m->phase() != gameplay::MatchPhase::Playing || m->roundTime() < 0.6) return false;
        for (int s = 0; s < 4; ++s)
            if (m->stationInRound(s) && !m->field(s).pickups().empty()) return true;
        return false;
    }, 120);
    shot("15_powerup_rings");
    // P1 is out by now with a score: they shoot their initials in their own column (W / D pick a letter,
    // left ctrl shoots it) while the others play on.
    waitFor("initials: P1's entry starts in its column", [A, driveMatch] {
        driveMatch();
        return A().entering(0) || A().state() != ArcadeState::Playing;
    }, 240);
    const auto typeKeys = [this, driveMatch](std::vector<SDL_Scancode> keys) {
        auto i = std::make_shared<std::size_t>(0);
        auto wait = std::make_shared<int>(0);
        return [this, driveMatch, keys, i, wait] {
            driveMatch();
            if (*i < keys.size()) {
                if (tick_ % 5 == 0) tapKey(keys[(*i)++]);
                return false;
            }
            return ++*wait > 6;
        };
    };
    add("initials: shoot A, then C", typeKeys({SDL_SCANCODE_LCTRL, SDL_SCANCODE_D, SDL_SCANCODE_D, SDL_SCANCODE_LCTRL}));
    add("initials: P1 has typed AC", [this, A] {
        check(A().entering(0) && A().entry(0)->typed() == "AC", std::format("typed '{}'", A().entry(0) ? A().entry(0)->typed() : "-"));
        return true;
    });
    shot("20_initials_in_column");
    add("initials: shoot E, then turn back to end and shoot it",
        typeKeys({SDL_SCANCODE_D, SDL_SCANCODE_D, SDL_SCANCODE_LCTRL, SDL_SCANCODE_W, SDL_SCANCODE_W, SDL_SCANCODE_W, SDL_SCANCODE_W,
                  SDL_SCANCODE_W, SDL_SCANCODE_LCTRL}));
    add("initials: ACE is in the table", [this, A] {
        const auto& rows = A().highScores().tables(A().scoreTable()).allTime.rows();
        const bool found = std::any_of(rows.begin(), rows.end(), [](const arcade::ScoreEntry& e) { return e.name == "ACE"; });
        check(found, "ACE not saved");
        check(!A().entering(0), "P1 still entering");
        return true;
    });
    waitFor("fire: a careful player chains needles until the circle catches fire", [this, A, M] {
        const gameplay::Match* m = M();
        if (A().state() != ArcadeState::Playing || !m) return true;
        const int r = m->round();
        drive({P::Spam, r >= 2 ? P::Spam : P::Oracle, r >= 3 ? P::Spam : P::Oracle, P::Oracle});
        for (int s = 0; s < 4; ++s)
            if (m->stationInRound(s) && m->field(s).onFire(m->tickCount()) &&
                m->tickCount() - m->field(s).fireStartTick() >= static_cast<std::uint64_t>(geom::kTickRate / 4))
                return true;
        return false;
    }, 240);
    add("fire: the burning station shows it", [this, A, M] {
        const gameplay::Match* m = M();
        check(A().state() == ArcadeState::Playing && m, "match ended before anyone caught fire");
        if (m) {
            int burning = 0, ignitions = 0;
            for (int s = 0; s < 4; ++s) {
                if (m->field(s).onFire(m->tickCount())) ++burning;
                if (m->participant(s)) ignitions += m->participant(s)->ignitions;
            }
            check(burning >= 1, "no station on fire");
            check(ignitions >= 1, "no ignition counted");
        }
        (void)this;
        return true;
    });
    shot("16_on_fire");
    waitFor("match: P1, P2, P3 run out of tries in turn; P4 plays on", [this, A, M] {
        const gameplay::Match* m = M();
        if (A().state() != ArcadeState::Playing || !m) return A().state() == ArcadeState::GameOver;
        const int r = m->round();
        drive({P::Spam, r >= 2 ? P::Spam : P::Oracle, r >= 3 ? P::Spam : P::Oracle, P::Oracle});
        return false;
    }, 900);
    add("match: P4 wins as the last survivor (T1)", [this, A, M] {
        const auto& res = A().lastResult();
        check(res && res->winner == 3, std::format("winner {}", res ? res->winner : -9));
        check(M() && M()->lastDecision().rule == gameplay::RuleCase::T1, "match not decided by T1");
        if (M()) {
            // Powerups can buy a player a round (mend), so only the order of the eliminations is fixed.
            const int e1 = M()->participant(0)->eliminatedRound, e2 = M()->participant(1)->eliminatedRound,
                      e3 = M()->participant(2)->eliminatedRound;
            check(e1 >= 3 && e2 >= 4 && e3 >= 5, std::format("eliminated in rounds {}, {}, {}", e1, e2, e3));
            check(M()->participant(3)->powerupsClaimed >= 1, "P4 never took a powerup ring");
        }
        (void)this;
        return true;
    });
    add("let the screen open (08_game_over)", settle(0.6)); // the iris and any intro animation
    shot("08_game_over");
    waitFor("game over: back to attract with no credits", [A] { return A().state() == ArcadeState::Attract; }, 10);
    // The four-station match made the battle record (P4's win), so the attract loop replays that match
    // before the battle table.
    waitFor("attract: the record game is replayed", [A] { return A().state() == ArcadeState::Attract && A().attractPage() == AttractPage::Replay; }, 120);
    add("attract: the replay is the battle record's match", [this, A] {
        const arcade::Replay* r = A().showingReplay();
        const auto& rows = A().highScores().tables("battle").allTime.rows();
        check(r && !rows.empty() && r->name == rows.front().name && r->score == rows.front().score && r->mode == gameplay::ModeId::Battle,
              std::format("replay '{}' {}", r ? r->name : "-", r ? r->score : 0));
        std::error_code ec;
        check(std::filesystem::exists(app_.paths().replay("battle"), ec), "replays/battle.txt not written");
        return true;
    });
    waitFor("attract: the replay plays for a while", [A, M] {
        return A().attractPage() == AttractPage::Replay && M() && M()->round() >= 2 && M()->phase() == gameplay::MatchPhase::Playing && M()->roundTime() > 1.5;
    }, 60);
    shot("27_replay");
    waitFor("attract: the high score page comes round", [A] { return A().state() == ArcadeState::Attract && A().attractPage() == AttractPage::HighScores; },
            120);
    add("attract: let the table draw", settle(0.5));
    shot("21_high_scores");
    waitFor("attract: this machine's stats come next", [A] { return A().state() == ArcadeState::Attract && A().attractPage() == AttractPage::Stats; },
            15);
    add("attract: the four-station match counted, first pin earned", [this, A] {
        const auto& p = A().progress();
        check(p.stats().matches >= 1, std::format("matches {}", p.stats().matches));
        check(p.stats().needles > 0, "no needles counted");
        check(p.earned(arcade::Achievement::FirstPin), "first pin not earned");
        check(p.earned(arcade::Achievement::OnTheBoard), "on the board not earned (ACE was saved)");
        std::error_code ec;
        check(std::filesystem::exists(app_.paths().progress(), ec), "progress.ini not written");
        return true;
    });
    add("attract: let the stats draw", settle(0.5));
    shot("26_stats");
    add("controllers: unplug all", once([this] {
        for (int s = 1; s < 4; ++s) detach(s);
    }));

    // --- singleplayer with three bots
    add("singleplayer: insert a coin", taps(SDL_SCANCODE_5, 1));
    waitFor("singleplayer: 1 credit", [A] { return A().credits().credits() == 1; });
    add("singleplayer: P1 start", [this] { tapKey(SDL_SCANCODE_1); return true; });
    add("singleplayer: P1 votes battle (start again)", taps(SDL_SCANCODE_1, 1));
    waitFor("singleplayer: 1 human + 3 bots after the join window", [A] {
        const auto& l = A().lineup();
        return A().state() == ArcadeState::Playing && l.mode == players::GameMode::Singleplayer && l.humans == 1 && l.bots == 3 &&
               l.seats[0] == players::SeatKind::Human;
    }, 20);
    waitFor("singleplayer: P1 misses out, the bots play it out", [this, A] {
        drive({P::Spam, P::Idle, P::Idle, P::Idle});
        return A().state() == ArcadeState::GameOver;
    }, 1500);
    add("singleplayer: a bot won", [this, A] {
        const auto& res = A().lastResult();
        check(res && res->winner > 0 && res->lineup.seats[static_cast<std::size_t>(res->winner)] == players::SeatKind::Bot, "no bot winner");
        (void)this;
        return true;
    });
    waitFor("singleplayer: back to attract", [A] { return A().state() == ArcadeState::Attract; }, 10);

    // --- service menu: visit the test pages, change settings, save
    add("service: open with the service key", [this] { tapKey(SDL_SCANCODE_F2); return true; });
    waitFor("service: main page open", [this] { return app_.serviceMenu().isOpen() && app_.serviceMenu().pageTitle() == "service menu"; });
    shot("09_service_menu");
    add("service: select input test", serviceSelect("input test"));
    waitFor("service: input test page", [this] { return app_.serviceMenu().pageTitle() == "input test"; });
    add("service: hold P1 fire", once([] {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.scancode = SDL_SCANCODE_LCTRL;
        e.key.down = true;
        SDL_PushEvent(&e);
    }));
    add("service: let it register", settle(0.1));
    add("service: input test shows P1 fire held", [this] {
        check(app_.inputSystem().stationHeld(0, input::Action::Fire), "P1 fire not shown as held");
        return true;
    });
    shot("10_input_test");
    add("service: release fire, leave with service key", once([this] {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_UP;
        e.key.scancode = SDL_SCANCODE_LCTRL;
        e.key.down = false;
        SDL_PushEvent(&e);
        tapKey(SDL_SCANCODE_F2);
    }));
    waitFor("service: back on main", [this] { return app_.serviceMenu().pageTitle() == "service menu"; });
    add("service: select display test", serviceSelect("display test"));
    waitFor("service: display test page", [this] { return app_.serviceMenu().pageTitle() == "display test"; });
    add("service: let it draw", settle(0.2));
    shot("11_display_test");
    add("service: back", [this] { tapKey(SDL_SCANCODE_BACKSPACE); return true; });
    waitFor("service: main again", [this] { return app_.serviceMenu().pageTitle() == "service menu"; });
    add("service: select bot settings", serviceSelect("bot settings"));
    waitFor("service: bot settings page", [this] { return app_.serviceMenu().pageTitle() == "bot settings"; });
    add("service: bots with one player -> 0", taps(SDL_SCANCODE_LEFT, 3));
    add("service: back to main", [this] { tapKey(SDL_SCANCODE_BACKSPACE); return true; });
    waitFor("service: main page", [this] { return app_.serviceMenu().pageTitle() == "service menu"; });
    add("service: select game settings", serviceSelect("game settings"));
    waitFor("service: game settings page", [this] { return app_.serviceMenu().pageTitle() == "game settings"; });
    add("service: tries -> 1", taps(SDL_SCANCODE_LEFT, 2));
    shot("12_service_game_settings");
    add("service: back", [this] { tapKey(SDL_SCANCODE_BACKSPACE); return true; });
    waitFor("service: main", [this] { return app_.serviceMenu().pageTitle() == "service menu"; });
    add("service: save & exit", serviceSelect("save & exit"));
    waitFor("service: closed", [this] { return !app_.serviceMenu().isOpen(); });
    add("service: settings.ini was written", [this] {
        const auto text = config::readFile(app_.paths().settings()).value_or("");
        check(text.find("singleplayer_bots = 0") != std::string::npos, "settings.ini lacks singleplayer_bots = 0");
        check(text.find("tries = 1") != std::string::npos, "settings.ini lacks tries = 1");
        check(text.find("; spindle") == 0, "settings.ini lost its header comment");
        check(app_.currentSettings().bots.singleplayerBots == 0 && app_.currentSettings().gameplay.tries == 1, "settings not applied");
        return true;
    });

    // --- singleplayer with zero bots (T10)
    add("solo: insert a coin", taps(SDL_SCANCODE_5, 1));
    add("solo: P1 start", [this] { tapKey(SDL_SCANCODE_1); return true; });
    add("solo: P1 votes battle (start again)", taps(SDL_SCANCODE_1, 1));
    waitFor("solo: 1 human, 0 bots, singleplayer", [A] {
        const auto& l = A().lineup();
        return A().state() == ArcadeState::Playing && l.mode == players::GameMode::Singleplayer && l.humans == 1 && l.bots == 0;
    }, 20);
    waitFor("solo: clear round 1, then miss", [this, A, M] {
        if (A().state() != ArcadeState::Playing || !M()) return A().state() == ArcadeState::GameOver;
        drive({M()->round() == 1 ? P::Oracle : P::Spam, P::Idle, P::Idle, P::Idle});
        return false;
    }, 120);
    add("solo: ends with no winner after 1 cleared round (T10)", [this, A, M] {
        const auto& res = A().lastResult();
        check(res && res->winner == -1, "solo match declared a winner");
        check(res && res->roundsCleared[0] == 1, std::format("rounds cleared {}", res ? res->roundsCleared[0] : -1));
        check(M() && M()->lastDecision().rule == gameplay::RuleCase::T10, "solo not decided by T10");
        (void)this;
        return true;
    });
    add("let the screen open (13_solo_game_over)", settle(0.6)); // the iris and any intro animation
    shot("13_solo_game_over");
    waitFor("solo: back to attract", [A] { return A().state() == ArcadeState::Attract; }, 10);

    // --- two humans, no bots: T2 (the later failure wins)
    joinTwoKeyboardPlayers("T2");
    waitFor("T2: P1 starts missing early, P2 later", [this, A, M] {
        if (A().state() != ArcadeState::Playing || !M()) return A().state() == ArcadeState::GameOver;
        const double t = M()->roundTime();
        drive({t > 0.3 ? P::Spam : P::Idle, t > 0.9 ? P::Spam : P::Idle, P::Idle, P::Idle});
        return false;
    }, 120);
    add("T2: P2 wins by failing last", [this, A, M] {
        const auto& res = A().lastResult();
        check(M() && M()->field(1).placed() > 0, "P2 never fired");
        check(res && res->winner == 1, std::format("T2 winner {}", res ? res->winner : -9));
        check(M() && M()->lastDecision().rule == gameplay::RuleCase::T2, "not decided by T2");
        (void)this;
        return true;
    });
    waitFor("T2: back to attract", [A] { return A().state() == ArcadeState::Attract; }, 10);

    // --- identical play: T4 sudden death, then T5
    joinTwoKeyboardPlayers("T4");
    waitFor("T4: both miss on the same tick with the same needles", [this, M] {
        if (!M()) {
            check(false, "match ended before sudden death");
            return true;
        }
        drive({P::Spam, P::Spam, P::Idle, P::Idle});
        return (M()->suddenDeath() && M()->phase() == gameplay::MatchPhase::Ready) || M()->over();
    }, 120);
    add("T4: sudden death between P1 and P2", [this, M] {
        if (!M()) return true;
        check(M()->suddenDeath(), "no sudden death");
        check(M()->lastDecision().rule == gameplay::RuleCase::T4, std::format("rule {}", gameplay::ruleCaseName(M()->lastDecision().rule)));
        check(M()->stationInRound(0) && M()->stationInRound(1), "sudden death players wrong");
        return true;
    });
    shot("14_sudden_death");
    waitFor("T5: P1 clears the sudden-death round, P2 misses", [this, A] {
        if (A().state() != ArcadeState::Playing) return A().state() == ArcadeState::GameOver;
        drive({P::Oracle, P::Spam, P::Idle, P::Idle});
        return false;
    }, 120);
    add("T5: P1 wins the sudden death", [this, A, M] {
        const auto& res = A().lastResult();
        check(res && res->winner == 0, std::format("T5 winner {}", res ? res->winner : -9));
        check(M() && M()->lastDecision().rule == gameplay::RuleCase::T5, "not decided by T5");
        (void)this;
        return true;
    });
    waitFor("T5: back to attract", [A] { return A().state() == ArcadeState::Attract; }, 10);

    // --- touch (phones and tablets; works on any touch screen)
    const auto norm = [this](float px, float py) {
        const auto& l = app_.layout();
        return std::pair<float, float>{px / l.width, py / l.height};
    };
    const auto columnPoint = [this](int station) {
        const auto& l = app_.layout();
        const auto& c = l.columns[static_cast<std::size_t>(station)];
        return std::pair<float, float>{(c.x + c.w * 0.5f) / l.width, (c.y + c.h * 0.6f) / l.height};
    };
    add("touch: hold a finger on the hidden top-left corner", once([this] { fingerEvent(SDL_EVENT_FINGER_DOWN, 900, 0.02f, 0.03f); }));
    waitFor("touch: after 3 seconds the service menu opens with touch controls", [this] {
        return app_.serviceMenu().isOpen() && app_.serviceMenu().touchMode() && app_.serviceMenu().backButton().w > 0.0f;
    }, 900);
    add("touch: lift the finger", once([this] { fingerEvent(SDL_EVENT_FINGER_UP, 900, 0.02f, 0.03f); }));
    add("touch: let the menu draw", settle(0.2));
    add("touch: tap the second row", once([this, norm] {
        if (auto pt = app_.serviceMenu().rowPoint(1)) {
            const auto [x, y] = norm(pt->x, pt->y);
            tapScreen(x, y);
        } else {
            check(false, "second row not on screen");
        }
    }));
    waitFor("touch: the second row is selected", [this] { return app_.serviceMenu().selectedIndex() == 1; }, 5);
    add("touch: tap back", once([this, norm] {
        const auto& b = app_.serviceMenu().backButton();
        const auto [x, y] = norm(b.x + b.w * 0.5f, b.y + b.h * 0.5f);
        tapScreen(x, y);
    }));
    waitFor("touch: back moves to save & exit", [this] { return app_.serviceMenu().selectedLabel() == "save & exit"; }, 5);
    add("touch: let the menu draw again", settle(0.2));
    add("touch: tap save & exit", once([this, norm] {
        if (auto pt = app_.serviceMenu().rowPoint(app_.serviceMenu().selectedIndex())) {
            const auto [x, y] = norm(pt->x, pt->y);
            tapScreen(x, y);
        } else {
            check(false, "save & exit not on screen");
        }
    }));
    waitFor("touch: the menu closes", [this] { return !app_.serviceMenu().isOpen(); }, 5);
    add("touch: start from empty high score tables", once([this] { app_.arcadeMachine().highScores().clearAll(); }));
    add("touch: insert a coin", taps(SDL_SCANCODE_5, 1));
    waitFor("touch: 1 credit", [A] { return A().credits().credits() == 1; });
    add("touch: tap station 2's column", once([this, columnPoint] {
        const auto [x, y] = columnPoint(1);
        tapScreen(x, y);
    }));
    waitFor("touch: station 2 joined", [A] { return A().state() == ArcadeState::Vote && A().joined()[1]; }, 5);
    add("touch: tap the middle of the column to vote battle", once([this, columnPoint] {
        const auto [x, y] = columnPoint(1);
        tapScreen(x, y);
    }));
    waitFor("touch: one human playing at station 2", [A] {
        return A().state() == ArcadeState::Playing && A().lineup().humans == 1 && A().lineup().seats[1] == players::SeatKind::Human &&
               A().currentMode() == gameplay::ModeId::Battle;
    }, 15);
    waitFor("touch: the round starts", [M] { return M() && M()->phase() == gameplay::MatchPhase::Playing; }, 10);
    add("touch: remember station 2's needles", once([this, M] { touchNeedles_ = M()->field(1).needlesLeft(); }));
    add("touch: a tap on the far side of the screen fires station 2's needle", once([this] { tapScreen(0.9f, 0.5f); }));
    waitFor("touch: station 2 launched a needle", [this, M] { return M() && M()->field(1).needlesLeft() < touchNeedles_; }, 5);
    shot("17_touch_play");
    waitFor("touch: keep tapping until the match is over", [this, A] {
        if (A().state() == ArcadeState::Playing && tick_ % 3 == 0) tapScreen(0.5f, 0.5f);
        return A().state() == ArcadeState::GameOver || A().state() == ArcadeState::Initials;
    }, 120);
    add("touch: a high score - the screen splits into left / shoot / right", [this, A] {
        check(A().state() == ArcadeState::Initials && A().entering(1), "station 2 is not shooting initials");
        return true;
    });
    const auto tapSeq = [this](std::vector<float> xs) {
        auto i = std::make_shared<std::size_t>(0);
        return [this, xs, i] {
            if (*i >= xs.size()) return true;
            if (tick_ % 6 == 0) tapScreen(xs[(*i)++], 0.55f);
            return false;
        };
    };
    add("touch: shoot A, tap right, shoot B", tapSeq({0.5f, 0.9f, 0.5f}));
    add("touch: let it draw", settle(0.3));
    add("touch: station 2 typed AB", [this, A] {
        check(A().entry(1) && A().entry(1)->typed() == "AB", std::format("typed '{}'", A().entry(1) ? A().entry(1)->typed() : "-"));
        return true;
    });
    shot("22_touch_initials");
    add("touch: tap left twice to end, shoot it", tapSeq({0.1f, 0.1f, 0.5f}));
    waitFor("touch: saved, then the results", [A] { return A().state() == ArcadeState::GameOver; }, 10);
    waitFor("touch: back to attract", [A] { return A().state() == ArcadeState::Attract; }, 10);

    // --- a tied vote spins the wheel
    add("wheel: P2 uses the keyboard", [this] {
        ctl_[1] = Controller{};
        ctl_[1].kind = CtlKind::Keyboard;
        ctl_[1].fireKey = SDL_SCANCODE_A;
        ctl_[1].startKey = SDL_SCANCODE_2;
        return true;
    });
    add("wheel: insert 2 coins", taps(SDL_SCANCODE_5, 2));
    waitFor("wheel: 2 credits", [A] { return A().credits().credits() == 2; });
    add("wheel: P1 and P2 join", taps(SDL_SCANCODE_1, 1));
    add("wheel: P2 joins", taps(SDL_SCANCODE_2, 1));
    waitFor("wheel: both in", [A] { return A().state() == ArcadeState::Vote && A().joined()[0] && A().joined()[1]; }, 5);
    add("wheel: P1 votes battle, P2 turns right (K) to marathon and votes", taps(SDL_SCANCODE_LCTRL, 1));
    add("wheel: P2 right", taps(SDL_SCANCODE_K, 1));
    add("wheel: P2 votes", taps(SDL_SCANCODE_A, 1));
    waitFor("wheel: one vote each - the wheel spins", [A] { return A().votePhase() == arcade::VotePhase::Spin && A().votePhaseTime() > 1.4; }, 15);
    add("wheel: two slices, battle and marathon", [this, A] {
        check(A().wheel().size() == 2, std::format("{} slices", A().wheel().size()));
        return true;
    });
    shot("24_wheel");
    waitFor("wheel: it stops on one of them and its card shows", [A] {
        return A().votePhase() == arcade::VotePhase::Intro &&
               (A().chosenMode() == gameplay::ModeId::Battle || A().chosenMode() == gameplay::ModeId::Marathon);
    }, 10);
    waitFor("wheel: that mode is played", [A] {
        return A().state() == ArcadeState::Playing && (A().currentMode() == gameplay::ModeId::Battle || A().currentMode() == gameplay::ModeId::Marathon);
    }, 10);
    waitFor("wheel: both crash out quickly", [this, A] {
        if (A().state() == ArcadeState::Playing) drive({P::Spam, P::Spam, P::Idle, P::Idle});
        return A().state() == ArcadeState::GameOver || A().state() == ArcadeState::Attract;
    }, 240);
    waitFor("wheel: back to attract", [A] { return A().state() == ArcadeState::Attract; }, 30);
}

// ------------------------------------------------------------------ runner

void SelfTest::beforeTick() {
    if (done_) return;
    ++tick_;
    for (auto& [ticks, fn] : releases_) {
        if (--ticks <= 0 && fn) {
            fn();
            fn = nullptr;
        }
    }
    std::erase_if(releases_, [](const auto& r) { return !r.second; });

    if (current_ >= steps_.size()) {
        finish(true);
        return;
    }
    Step& s = steps_[current_];
    stepTime_ += kDt;
    failures_.clear();
    const bool complete = s.run();
    if (!failures_.empty()) {
        results_.push_back({s.name, false, stepTime_, failures_});
        log::error("selftest: FAIL {} ({})", s.name, failures_);
        finish(false);
        return;
    }
    if (complete) {
        results_.push_back({s.name, true, stepTime_, {}});
        log::info("selftest: ok   {} ({:.2f} s)", s.name, stepTime_);
        ++current_;
        stepTime_ = 0.0;
    } else if (stepTime_ > s.timeout) {
        const auto& a = app_.arcadeMachine();
        results_.push_back({s.name, false, stepTime_, std::format("timed out after {:.0f} s (state {})", s.timeout, arcade::arcadeStateName(a.state()))});
        log::error("selftest: FAIL {} (timeout)", s.name);
        finish(false);
    }
}

void SelfTest::finish(bool ok) {
    if (done_) return;
    done_ = true;
    std::string report = std::format("spindle self-test: {}\n\n", ok ? "PASS" : "FAIL");
    for (const Result& r : results_)
        report += std::format("{}  {:<70} {:8.2f} s  {}\n", r.ok ? "ok  " : "FAIL", r.name, r.seconds, r.detail);
    if (!ok && current_ + 1 < steps_.size()) report += std::format("\n{} step(s) not run after the failure\n", steps_.size() - current_ - 1);
    report += "\nscreenshots:\n";
    for (const auto& s : shots_) report += "  " + s + "\n";
    std::string err;
    config::writeFileAtomic(out_ / "selftest_report.txt", report, &err);
    log::info("selftest: {} ({} of {} steps passed)", ok ? "PASS" : "FAIL", ok ? results_.size() : results_.size() - 1, steps_.size());
    app_.requestQuit(ok ? 0 : 1);
}

} // namespace spindle
