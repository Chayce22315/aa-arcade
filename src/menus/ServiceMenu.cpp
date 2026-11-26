#include "menus/ServiceMenu.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "engine/Branding.hpp"
#include "engine/Log.hpp"
#include "gameplay/Powerups.hpp"
#include "input/Keybinds.hpp"

namespace spindle::menus {
namespace {

using render::Align;
using render::Color;
namespace colors = render::colors;

constexpr double kCaptureTimeout = 10.0;
constexpr double kConfirmWindow = 3.0;

template <class E>
void cycle(E& value, std::initializer_list<E> options, int dir) {
    std::vector<E> v(options);
    auto it = std::find(v.begin(), v.end(), value);
    int i = it == v.end() ? 0 : static_cast<int>(it - v.begin());
    const int n = static_cast<int>(v.size());
    i = ((i + dir) % n + n) % n;
    value = v[static_cast<std::size_t>(i)];
}

const char* fullscreenName(config::FullscreenMode m) {
    switch (m) {
    case config::FullscreenMode::Borderless: return "borderless";
    case config::FullscreenMode::Exclusive: return "exclusive";
    case config::FullscreenMode::Windowed: return "windowed";
    }
    return "borderless";
}

std::string formatDuration(std::uint64_t seconds) {
    return std::format("{}h {:02}m {:02}s", seconds / 3600, (seconds / 60) % 60, seconds % 60);
}

} // namespace

ServiceMenu::ServiceMenu(ServiceHost& host) : host_(host) {}

void ServiceMenu::open(bool byTouch) {
    touchMode_ = host_.touchUi() || byTouch;
    edit_ = host_.currentSettings();
    open_ = true;
    capture_ = Capture::None;
    host_.arcadeMachine().setServiceOpen(true);
    show(PageId::Main);
    log::info("service: menu opened");
}

void ServiceMenu::openPlayerOptions() {
    open(true);
    playerMode_ = true;
    show(PageId::PlayerOptions);
    log::info("service: player options opened");
}

void ServiceMenu::close(bool save) {
    playerMode_ = false;
    host_.inputSystem().cancelCapture();
    capture_ = Capture::None;
    if (save) {
        host_.commitSettings(edit_);
    } else {
        host_.previewDisplay(host_.currentSettings().display);
        host_.previewVolumes(host_.currentSettings().audio);
    }
    open_ = false;
    host_.arcadeMachine().setServiceOpen(false);
    log::info("service: menu closed ({})", save ? "saved" : "discarded");
}

std::string ServiceMenu::selectedLabel() const {
    if (page_.items().empty()) return {};
    return page_.items()[static_cast<std::size_t>(page_.selected())].label;
}

bool ServiceMenu::confirm(const std::string& key) {
    if (pendingConfirm_ == key && confirmTime_ > 0.0) {
        pendingConfirm_.clear();
        confirmTime_ = 0.0;
        return true;
    }
    pendingConfirm_ = key;
    confirmTime_ = kConfirmWindow;
    return false;
}

// ------------------------------------------------------------------ item builders

MenuItem ServiceMenu::intItem(std::string label, int& ref, int lo, int hi, int step, std::string suffix) {
    MenuItem m;
    m.label = std::move(label);
    m.value = [&ref, suffix] { return std::to_string(ref) + suffix; };
    m.adjust = [&ref, lo, hi, step](int dir) { ref = std::clamp(ref + dir * step, lo, hi); };
    return m;
}

MenuItem ServiceMenu::realItem(std::string label, double& ref, double lo, double hi, double step) {
    MenuItem m;
    m.label = std::move(label);
    m.value = [&ref] { return std::format("{:.2f}", ref); };
    m.adjust = [&ref, lo, hi, step](int dir) { ref = std::clamp(std::round((ref + dir * step) / step) * step, lo, hi); };
    return m;
}

MenuItem ServiceMenu::boolItem(std::string label, bool& ref) {
    MenuItem m;
    m.label = std::move(label);
    m.value = [&ref] { return std::string(ref ? "on" : "off"); };
    m.adjust = [&ref](int) { ref = !ref; };
    return m;
}

MenuItem ServiceMenu::actionItem(std::string label, std::function<void()> fn, std::function<std::string()> value) {
    MenuItem m;
    m.label = std::move(label);
    m.activate = std::move(fn);
    m.value = std::move(value);
    return m;
}

MenuItem ServiceMenu::pageItem(std::string label, PageId id) {
    // Deferred: switching pages rebuilds the item list, which must not happen inside an item callback.
    return actionItem(std::move(label), [this, id] { pendingPage_ = id; });
}

MenuItem ServiceMenu::keyItem(std::string label, std::string& binding) {
    MenuItem m;
    m.label = label;
    m.value = [&binding] { return input::joinNames(input::keyNames(binding)); };
    m.activate = [this, &binding, label] {
        capture_ = Capture::Key;
        captureTarget_ = &binding;
        captureLabel_ = label;
        captureTime_ = kCaptureTimeout;
        host_.inputSystem().beginKeyCapture();
    };
    return m;
}

// ------------------------------------------------------------------ pages

// Vibration (phones) and rumble (gamepads); every change buzzes once so the operator can feel it.
void ServiceMenu::addFeedbackItems(std::vector<MenuItem>& items) {
    auto buzz = [this](MenuItem m) {
        auto inner = m.adjust;
        m.adjust = [this, inner](int d) {
            inner(d);
            host_.previewFeedback(edit_.feedback);
        };
        return m;
    };
    if (host_.touchUi()) {
        MenuItem vib = buzz(boolItem("vibration", edit_.feedback.vibration));
        vib.separatorBefore = true;
        if (!host_.phoneVibrates()) {
            vib.enabled = false;
            vib.value = [] { return std::string("not on this device"); };
        }
        items.push_back(std::move(vib));
    }
    MenuItem rumble = buzz(boolItem("gamepad rumble", edit_.feedback.gamepadRumble));
    rumble.separatorBefore = !host_.touchUi();
    items.push_back(std::move(rumble));
    items.push_back(buzz(intItem(host_.touchUi() ? "vibration strength" : "rumble strength", edit_.feedback.strength, 0, 100, 10, "%")));
}

// The looks achievements unlock. They belong to the cabinet's progress, not to settings.ini, so a change
// is saved at once (save & exit or not).
void ServiceMenu::addLookItems(std::vector<MenuItem>& items, bool unlockedOnly) {
    bool first = true;
    for (std::size_t i = 0; i < arcade::kLookCount; ++i) {
        const auto look = static_cast<arcade::Look>(i);
        const bool open = host_.arcadeMachine().progress().unlocked(look);
        if (unlockedOnly && !open) continue;
        MenuItem m;
        m.label = std::string(arcade::lookName(look));
        m.separatorBefore = first;
        first = false;
        m.enabled = open;
        m.value = [this, look, open] {
            if (open) return std::string(host_.arcadeMachine().progress().lookOn(look) ? "on" : "off");
            for (std::size_t k = 0; k < arcade::kAchievementCount; ++k) {
                const auto info = arcade::achievementInfo(static_cast<arcade::Achievement>(k));
                if (info.unlocks == look) return std::format("locked - {}", info.blurb);
            }
            return std::string("locked");
        };
        m.adjust = [this, look](int) {
            auto& p = host_.arcadeMachine().progress();
            p.setLook(look, !p.lookOn(look));
            host_.saveProgress();
        };
        items.push_back(std::move(m));
    }
}

void ServiceMenu::show(PageId id) {
    pageId_ = id;
    pendingConfirm_.clear();
    backHeld_ = 0.0;
    build(id);
}

void ServiceMenu::build(PageId id) {
    std::vector<MenuItem> items;
    std::string title;
    switch (id) {
    case PageId::Main: {
        title = "service menu";
        items.push_back(pageItem("input test", PageId::InputTest));
        items.push_back(pageItem("controller assignment", PageId::Assignment));
        items.push_back(pageItem("keybinds", PageId::Keybinds));
        items.push_back(pageItem("display test", PageId::DisplayTest));
        items.push_back(pageItem("video", PageId::Video));
        items.push_back(pageItem("sound test", PageId::SoundTest));
        items.push_back(pageItem("volumes", PageId::Volumes));
        items.push_back(pageItem("game settings", PageId::Game));
        items.push_back(pageItem("coin settings", PageId::Coin));
        items.push_back(pageItem("bot settings", PageId::Bots));
        items.push_back(pageItem("game modes", PageId::Modes));
        items.push_back(pageItem("bookkeeping", PageId::Bookkeeping));
        items.push_back(pageItem("high scores", PageId::HighScores));
        items.push_back(pageItem("progress", PageId::Progress));
        MenuItem reset = actionItem("reset defaults", [this] {
            if (confirm("reset")) {
                edit_ = host_.defaultSettings();
                notice_ = "defaults loaded - save & exit to keep them";
                noticeTime_ = 4.0;
            }
        });
        reset.value = [this] { return std::string(pendingConfirm_ == "reset" ? "press again to confirm" : ""); };
        reset.separatorBefore = true;
        items.push_back(std::move(reset));
        items.push_back(actionItem("save & exit", [this] { close(true); }));
        items.push_back(actionItem("exit without saving", [this] { close(false); }));
        MenuItem shut = actionItem("shut down", [this] {
            if (confirm("shutdown")) host_.requestShutdown();
        });
        shut.value = [this] { return std::string(pendingConfirm_ == "shutdown" ? "press again to confirm" : ""); };
        items.push_back(std::move(shut));
        break;
    }
    case PageId::InputTest: title = "input test"; break;
    case PageId::DisplayTest: title = "display test"; break;
    case PageId::Assignment: {
        title = "controller assignment";
        MenuItem mode;
        mode.label = "assignment";
        mode.value = [this] { return std::string(edit_.input.assignment == config::AssignmentMode::Auto ? "auto" : "fixed"); };
        mode.adjust = [this](int d) { cycle(edit_.input.assignment, {config::AssignmentMode::Auto, config::AssignmentMode::Fixed}, d); };
        items.push_back(std::move(mode));
        MenuItem disc;
        disc.label = "on disconnect";
        disc.value = [this] { return std::string(edit_.input.disconnectAction == config::DisconnectAction::Pause ? "pause" : "continue"); };
        disc.adjust = [this](int d) {
            cycle(edit_.input.disconnectAction, {config::DisconnectAction::Pause, config::DisconnectAction::Continue}, d);
        };
        items.push_back(std::move(disc));
        for (int s = 0; s < 4; ++s) {
            MenuItem st;
            st.label = std::format("station {} controller", s + 1);
            st.separatorBefore = s == 0;
            st.value = [this, s] {
                const input::InputDevice* d = host_.inputSystem().devices().controllerForStation(s);
                return d ? std::format("{} ({})", d->name, d->typeLabel) : std::string("-");
            };
            st.activate = [this, s] {
                capture_ = Capture::Controller;
                captureStation_ = s;
                captureTime_ = kCaptureTimeout;
                host_.inputSystem().beginControllerCapture();
            };
            st.adjust = [this, s](int d) {
                if (d < 0) host_.inputSystem().clearStation(s);
            };
            items.push_back(std::move(st));
        }
        items.push_back(actionItem("remember these for fixed mode", [this] {
            for (int s = 0; s < 4; ++s) {
                const input::InputDevice* d = host_.inputSystem().devices().controllerForStation(s);
                edit_.input.gamepadGuid[static_cast<std::size_t>(s)] = d ? d->guid : std::string();
            }
            notice_ = "controller GUIDs stored - save & exit to keep them";
            noticeTime_ = 4.0;
        }));
        break;
    }
    case PageId::Keybinds: {
        title = "keybinds";
        for (int s = 0; s < 4; ++s) {
            auto& k = edit_.stationKeys[static_cast<std::size_t>(s)];
            items.push_back(keyItem(std::format("P{} fire", s + 1), k.fire));
            items.push_back(keyItem(std::format("P{} start", s + 1), k.start));
            items.push_back(keyItem(std::format("P{} left", s + 1), k.left));
            items.push_back(keyItem(std::format("P{} right", s + 1), k.right));
        }
        auto& sk = edit_.systemKeys;
        MenuItem coin = keyItem("coin", sk.coin);
        coin.separatorBefore = true;
        items.push_back(std::move(coin));
        items.push_back(keyItem("service coin", sk.serviceCoin));
        items.push_back(keyItem("service menu", sk.service));
        items.push_back(keyItem("exit (hold)", sk.exit));
        MenuItem per = boolItem("keyboard per station", edit_.input.perKeyboard);
        per.separatorBefore = true;
        items.push_back(std::move(per));
        for (int s = 0; s < 4; ++s) {
            MenuItem kb;
            kb.label = std::format("P{} keyboard", s + 1);
            kb.value = [this, s] {
                const std::string& n = edit_.input.keyboardName[static_cast<std::size_t>(s)];
                return n.empty() ? std::string("any") : n;
            };
            kb.activate = [this, s] {
                capture_ = Capture::Keyboard;
                captureStation_ = s;
                captureTime_ = kCaptureTimeout;
                host_.inputSystem().beginKeyCapture();
            };
            kb.adjust = [this, s](int d) {
                if (d < 0) edit_.input.keyboardName[static_cast<std::size_t>(s)].clear();
            };
            items.push_back(std::move(kb));
        }
        break;
    }
    case PageId::PlayerOptions: {
        title = "options";
        auto preview = [this](MenuItem m) {
            auto inner = m.adjust;
            m.adjust = [this, inner](int d) {
                inner(d);
                host_.previewVolumes(edit_.audio);
                host_.playSound(audio::SoundId::NeedleHit);
            };
            return m;
        };
        items.push_back(preview(intItem("volume", edit_.audio.masterVolume, 0, 100, 5, "%")));
        items.push_back(preview(intItem("game sounds", edit_.audio.sfxVolume, 0, 100, 5, "%")));
        items.push_back(preview(intItem("menu sounds", edit_.audio.uiVolume, 0, 100, 5, "%")));
        items.push_back(preview(intItem("music", edit_.audio.musicVolume, 0, 100, 5, "%")));
        addFeedbackItems(items);
        addLookItems(items, true);
        MenuItem flashes = boolItem("reduce flashes", edit_.accessibility.reduceFlashes);
        flashes.separatorBefore = true;
        items.push_back(std::move(flashes));
        items.push_back(boolItem("player colours", edit_.accessibility.accentColors));
        MenuItem done = actionItem("done", [this] { close(true); });
        done.separatorBefore = true;
        items.push_back(std::move(done));
        break;
    }
    case PageId::Video: {
        title = "video";
        MenuItem res;
        res.label = "resolution";
        res.value = [this] { return config::formatResolution(edit_.display.resolution); };
        res.adjust = [this](int d) {
            std::vector<config::Resolution> options{config::Resolution{}};
            for (auto [w, h] : host_.display().info().resolutions) options.push_back({false, w, h});
            auto it = std::find(options.begin(), options.end(), edit_.display.resolution);
            int i = it == options.end() ? 0 : static_cast<int>(it - options.begin());
            const int n = static_cast<int>(options.size());
            edit_.display.resolution = options[static_cast<std::size_t>(((i + d) % n + n) % n)];
        };
        items.push_back(std::move(res));
        MenuItem fs;
        fs.label = "fullscreen";
        fs.value = [this] { return std::string(fullscreenName(edit_.display.fullscreen)); };
        fs.adjust = [this](int d) {
            cycle(edit_.display.fullscreen, {config::FullscreenMode::Borderless, config::FullscreenMode::Exclusive, config::FullscreenMode::Windowed}, d);
        };
        items.push_back(std::move(fs));
        items.push_back(boolItem("vsync", edit_.display.vsync));
        items.push_back(boolItem("reduce flashes", edit_.accessibility.reduceFlashes));
        items.push_back(boolItem("player colours", edit_.accessibility.accentColors));
        items.push_back(intItem("monitor", edit_.display.displayIndex, 0, std::max(0, host_.display().displayCount() - 1)));
        MenuItem fps;
        fps.label = "frame cap (vsync off)";
        fps.value = [this] { return edit_.display.maxFps == 0 ? std::string("none") : std::to_string(edit_.display.maxFps); };
        fps.adjust = [this](int d) { cycle(edit_.display.maxFps, {0, 30, 60, 75, 120, 144, 165, 240, 360}, d); };
        items.push_back(std::move(fps));
        items.push_back(actionItem("apply now", [this] { host_.previewDisplay(edit_.display); }, [this] { return host_.display().describe(); }));
        break;
    }
    case PageId::SoundTest: {
        title = "sound test";
        for (const auto& info : audio::kSounds) {
            const audio::SoundId sound = info.id;
            items.push_back(actionItem(std::string(info.label), [this, sound] { host_.playSound(sound); }));
        }
        MenuItem dev;
        dev.label = "device";
        dev.enabled = false;
        dev.separatorBefore = true;
        dev.value = [this] { return host_.audioAvailable() ? host_.audioDevice() : std::string("unavailable (silent)"); };
        items.push_back(std::move(dev));
        break;
    }
    case PageId::Volumes: {
        title = "volumes";
        auto preview = [this](MenuItem m) {
            auto inner = m.adjust;
            m.adjust = [this, inner](int d) {
                inner(d);
                host_.previewVolumes(edit_.audio);
                host_.playSound(audio::SoundId::NeedleHit);
            };
            return m;
        };
        items.push_back(preview(intItem("master", edit_.audio.masterVolume, 0, 100, 5, "%")));
        items.push_back(preview(intItem("game sounds", edit_.audio.sfxVolume, 0, 100, 5, "%")));
        items.push_back(preview(intItem("coin and menu sounds", edit_.audio.uiVolume, 0, 100, 5, "%")));
        items.push_back(preview(intItem("music", edit_.audio.musicVolume, 0, 100, 5, "%")));
        items.push_back(boolItem("attract sound", edit_.audio.attractSound));
        addFeedbackItems(items);
        break;
    }
    case PageId::Game: {
        title = "game settings";
        items.push_back(intItem("tries", edit_.gameplay.tries, 1, 9));
        items.push_back(intItem("round time limit", edit_.gameplay.roundTimeLimit, 10, 300, 5, " s"));
        items.push_back(realItem("rotation speed", edit_.gameplay.rotationSpeedScale, 0.5, 2.0, 0.05));
        items.push_back(realItem("needle speed", edit_.gameplay.needleSpeedScale, 0.5, 2.0, 0.05));
        items.push_back(realItem("ready delay", edit_.gameplay.readyDelay, 0.5, 5.0, 0.25));
        items.push_back(intItem("start round", edit_.gameplay.startRound, 1, 50));
        MenuItem seed;
        seed.label = "level seed";
        seed.value = [this] { return edit_.gameplay.seed == 0 ? std::string("random") : std::to_string(edit_.gameplay.seed); };
        seed.adjust = [this](int d) {
            const long long v = static_cast<long long>(edit_.gameplay.seed) + d;
            edit_.gameplay.seed = static_cast<std::uint32_t>(std::clamp(v, 0LL, 4294967295LL));
        };
        items.push_back(std::move(seed));
        MenuItem powers = boolItem("powerups", edit_.gameplay.powerups);
        powers.separatorBefore = true;
        items.push_back(std::move(powers));
        for (gameplay::PowerupKind kind : gameplay::kAllPowerups) {
            MenuItem m;
            m.label = std::format("  {} rings", gameplay::powerupName(kind));
            m.value = [this, kind] {
                return std::string(gameplay::contains(gameplay::parsePowerupList(edit_.gameplay.powerupTypes), kind) ? "on" : "off");
            };
            m.adjust = [this, kind](int) {
                auto kinds = gameplay::parsePowerupList(edit_.gameplay.powerupTypes);
                if (gameplay::contains(kinds, kind))
                    std::erase(kinds, kind);
                else
                    kinds.push_back(kind);
                edit_.gameplay.powerupTypes = gameplay::formatPowerupList(kinds);
            };
            items.push_back(std::move(m));
        }
        MenuItem fire = boolItem("fire meter", edit_.gameplay.fireMeter);
        fire.separatorBefore = true;
        items.push_back(std::move(fire));
        items.push_back(intItem("  combo to catch fire", edit_.gameplay.fireCombo, 4, 30));
        items.push_back(realItem("  seconds on fire", edit_.gameplay.fireSeconds, 0.5, 5.0, 0.25));
        items.push_back(realItem("  combo chain window", edit_.gameplay.fireChainWindow, 0.5, 5.0, 0.25));
        MenuItem tally;
        tally.label = "round end screen";
        tally.separatorBefore = true;
        tally.value = [this] {
            switch (edit_.gameplay.roundEndScreen) {
            case config::RoundEndScreen::Full: return std::string("full");
            case config::RoundEndScreen::Short: return std::string("short");
            case config::RoundEndScreen::Off: return std::string("off");
            }
            return std::string("full");
        };
        tally.adjust = [this](int dir) {
            cycle(edit_.gameplay.roundEndScreen, {config::RoundEndScreen::Full, config::RoundEndScreen::Short, config::RoundEndScreen::Off}, dir);
        };
        items.push_back(std::move(tally));
        break;
    }
    case PageId::Coin: {
        title = "coin settings";
        items.push_back(boolItem("free play", edit_.arcade.freePlay));
        items.push_back(intItem("credits per coin", edit_.arcade.creditsPerCoin, 1, 9));
        items.push_back(intItem("credits per player", edit_.arcade.creditsPerGame, 1, 9));
        items.push_back(intItem("max credits", edit_.arcade.maxCredits, 1, 99));
        items.push_back(intItem("vote time", edit_.arcade.voteSeconds, 10, 60, 1, " s"));
        items.push_back(intItem("game over time", edit_.arcade.gameOverTime, 3, 30, 1, " s"));
        items.push_back(intItem("initials time", edit_.arcade.initialsSeconds, 10, 30, 1, " s"));
        MenuItem hold = intItem("exit hold", edit_.arcade.exitHoldSeconds, 0, 10, 1, " s");
        auto base = hold.value;
        hold.value = [this, base] { return edit_.arcade.exitHoldSeconds == 0 ? std::string("locked") : base(); };
        items.push_back(std::move(hold));
        break;
    }
    case PageId::Bots: {
        title = "bot settings";
        items.push_back(intItem("bots with one player", edit_.bots.singleplayerBots, 0, 3));
        items.push_back(intItem("bots with two or more players", edit_.bots.multiplayerBots, 0, 2));
        for (auto* d : {&edit_.bots.difficulty, &edit_.bots.demoDifficulty}) {
            MenuItem m;
            m.label = d == &edit_.bots.difficulty ? "difficulty" : "demo difficulty";
            m.value = [d] { return std::string(config::difficultyName(*d)); };
            m.adjust = [d](int dir) {
                cycle(*d, {config::Difficulty::Easy, config::Difficulty::Normal, config::Difficulty::Hard, config::Difficulty::Expert}, dir);
            };
            items.push_back(std::move(m));
        }
        break;
    }
    case PageId::Bookkeeping: {
        title = "bookkeeping";
        const auto row = [&](std::string label, std::function<std::string()> value) {
            MenuItem m;
            m.label = std::move(label);
            m.value = std::move(value);
            m.enabled = false;
            items.push_back(std::move(m));
        };
        auto& bk = host_.arcadeMachine().bookkeeping();
        row("coins", [&bk] { return std::to_string(bk.coins); });
        row("service credits", [&bk] { return std::to_string(bk.serviceCredits); });
        row("singleplayer games", [&bk] { return std::to_string(bk.gamesSingleplayer); });
        row("multiplayer games", [&bk] { return std::to_string(bk.gamesMultiplayer); });
        row("won by players", [&bk] { return std::to_string(bk.humanWins); });
        row("won by bots", [&bk] { return std::to_string(bk.botWins); });
        row("play time", [&bk] { return formatDuration(bk.playSeconds); });
        for (std::size_t i = 0; i < bk.recentWinners.size(); ++i) {
            const arcade::WinnerRecord w = bk.recentWinners[i];
            row(i == 0 ? "recent winners" : "", [w] { return std::format("P{} {}  round {}", w.station + 1, w.bot ? "cpu" : "player", w.round); });
        }
        MenuItem clear = actionItem("clear bookkeeping", [this, &bk] {
            if (confirm("clear")) {
                bk.clearCounters();
                host_.saveBookkeeping();
                notice_ = "bookkeeping cleared";
                noticeTime_ = 3.0;
            }
        });
        clear.value = [this] { return std::string(pendingConfirm_ == "clear" ? "press again to confirm" : ""); };
        clear.separatorBefore = true;
        items.push_back(std::move(clear));
        break;
    }
    case PageId::Modes: {
        title = "game modes";
        items.push_back(intItem("vote time", edit_.arcade.voteSeconds, 10, 60, 1, " s"));
        for (std::size_t m = 0; m < gameplay::kModeCount; ++m) {
            MenuItem it = boolItem(std::string(gameplay::modeName(gameplay::kAllModes[m])), edit_.modes.enabled[m]);
            if (m == 0) it.separatorBefore = true;
            items.push_back(std::move(it));
        }
        MenuItem note;
        note.label = "with every mode off, battle stays on";
        note.enabled = false;
        note.separatorBefore = true;
        items.push_back(std::move(note));
        break;
    }
    case PageId::HighScores: {
        title = "high scores";
        auto& hs = host_.arcadeMachine().highScores();
        const auto addRows = [&](std::string_view table, bool today) {
            const auto& t = today ? hs.tables(table).today : hs.tables(table).allTime;
            MenuItem head;
            head.label = std::format("{} - {}", table, today ? "today" : "all time");
            head.value = [&t] { return t.empty() ? std::string("empty") : std::format("{} rows", t.rows().size()); };
            head.enabled = false;
            head.separatorBefore = true;
            items.push_back(std::move(head));
            for (std::size_t i = 0; i < t.rows().size(); ++i) {
                const arcade::ScoreEntry e = t.rows()[i];
                MenuItem m;
                m.label = std::format("  {}  {}", i + 1, e.name);
                m.value = [e] { return std::format("{}   round {}   {}", e.score, e.round, e.date); };
                m.enabled = false;
                items.push_back(std::move(m));
            }
        };
        for (std::string_view table : arcade::ArcadeMachine::kScoreTables) {
            addRows(table, false);
            addRows(table, true);
        }
        MenuItem clear = actionItem("clear high scores", [this, &hs] {
            if (confirm("clear scores")) {
                hs.clearAll();
                host_.saveHighScores();
                notice_ = "high scores cleared";
                noticeTime_ = 3.0;
                rebuildPending_ = true;
            }
        });
        clear.value = [this] { return std::string(pendingConfirm_ == "clear scores" ? "press again to confirm" : ""); };
        clear.separatorBefore = true;
        items.push_back(std::move(clear));
        break;
    }
    case PageId::Progress: {
        title = "progress";
        auto& progress = host_.arcadeMachine().progress();
        const auto& st = progress.stats();
        const auto info = [&items](std::string label, std::string value, bool separator = false) {
            MenuItem m;
            m.label = std::move(label);
            m.value = [value] { return value; };
            m.enabled = false;
            m.separatorBefore = separator;
            items.push_back(std::move(m));
        };
        info("games played", std::to_string(st.matches));
        info("needles placed", std::to_string(st.needles));
        info("rounds cleared", std::to_string(st.roundsCleared));
        info("close calls / fires / crowns", std::format("{} / {} / {}", st.closeCalls, st.fires, st.crowns));
        info("best score / best round", std::format("{} / {}", st.bestScore, st.bestRound));
        for (std::size_t i = 0; i < arcade::kAchievementCount; ++i) {
            const auto a = static_cast<arcade::Achievement>(i);
            const auto ai = arcade::achievementInfo(a);
            info(std::string(ai.name), progress.earned(a) ? std::string("earned") : std::string(ai.blurb), i == 0);
        }
        addLookItems(items, false);
        MenuItem reset = actionItem("reset progress", [this, &progress] {
            if (confirm("reset progress")) {
                progress.reset();
                host_.saveProgress();
                notice_ = "progress reset";
                noticeTime_ = 3.0;
                rebuildPending_ = true;
            }
        });
        reset.value = [this] { return std::string(pendingConfirm_ == "reset progress" ? "press again to confirm" : ""); };
        reset.separatorBefore = true;
        items.push_back(std::move(reset));
        break;
    }
    }
    page_ = MenuPage(std::move(title), std::move(items));
}

// ------------------------------------------------------------------ input

std::optional<render::Vec2> ServiceMenu::rowPoint(int index) const {
    for (const RowHit& row : rowHits_)
        if (row.index == index) return render::Vec2{lastLayout_.area.x + lastLayout_.area.w * 0.3f, (row.y0 + row.y1) * 0.5f};
    return std::nullopt;
}

void ServiceMenu::back() {
    if (!open_) return;
    if (playerMode_) {
        close(true);
        host_.playSound(audio::SoundId::MenuSelect);
        return;
    }
    if (capture_ != Capture::None) {
        host_.inputSystem().cancelCapture();
        capture_ = Capture::None;
        return;
    }
    if (pageId_ != PageId::Main) {
        show(PageId::Main);
    } else {
        for (std::size_t i = 0; i < page_.items().size(); ++i)
            if (page_.items()[i].label == "save & exit") page_.select(static_cast<int>(i));
    }
    host_.playSound(audio::SoundId::MenuMove);
}

void ServiceMenu::handleTap(float nx, float ny) {
    const float x = nx * lastLayout_.width, y = ny * lastLayout_.height;
    const auto inside = [&](const render::Rect& r) { return r.w > 0.0f && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h; };
    if (inside(backButton_)) {
        back();
        return;
    }
    if (capture_ != Capture::None || pageId_ == PageId::InputTest || pageId_ == PageId::DisplayTest) return;
    for (const RowHit& row : rowHits_) {
        if (y < row.y0 || y > row.y1) continue;
        const auto& item = page_.items()[static_cast<std::size_t>(row.index)];
        if (!item.enabled) return;
        if (row.index != page_.selected()) {
            page_.select(row.index);
            host_.playSound(audio::SoundId::MenuMove);
            return;
        }
        // Tapping the selected row: the value end changes it (left half down, right half up), the
        // label end opens or runs it.
        if (item.adjust && (x >= valueZoneX_ || !item.activate)) {
            const int dir = x >= valueZoneX_ && x < (valueZoneX_ + valueZoneEnd_) * 0.5f ? -1 : 1;
            if (page_.adjust(dir)) host_.playSound(audio::SoundId::MenuMove);
        } else if (page_.activate()) {
            host_.playSound(audio::SoundId::MenuSelect);
        }
        return;
    }
}

void ServiceMenu::update(const input::TickInput& in, double dt) {
    if (!open_) return;
    if (rebuildPending_) {
        rebuildPending_ = false;
        build(pageId_);
    }
    if (!in.taps.empty()) touchMode_ = true;
    for (const auto& [tx, ty] : in.taps) {
        handleTap(tx, ty);
        if (!open_) return;
    }
    noticeTime_ = std::max(0.0, noticeTime_ - dt);
    confirmTime_ = std::max(0.0, confirmTime_ - dt);
    if (confirmTime_ <= 0.0) pendingConfirm_.clear();

    if (capture_ != Capture::None) {
        captureTime_ -= dt;
        if ((capture_ == Capture::Key || capture_ == Capture::Keyboard) && in.capturedKey) {
            if (capture_ == Capture::Key && captureTarget_) {
                *captureTarget_ = SDL_GetScancodeName(*in.capturedKey);
                notice_ = std::format("{} = {}", captureLabel_, *captureTarget_);
            } else if (capture_ == Capture::Keyboard) {
                for (const auto& d : host_.inputSystem().devices().devices())
                    if (d->kind == input::DeviceKind::Keyboard && d->id == in.capturedKeyboard)
                        edit_.input.keyboardName[static_cast<std::size_t>(captureStation_)] = d->name;
                notice_ = std::format("P{} keyboard set", captureStation_ + 1);
            }
            noticeTime_ = 3.0;
            capture_ = Capture::None;
            host_.playSound(audio::SoundId::MenuSelect);
        } else if (capture_ == Capture::Controller && in.capturedController) {
            host_.inputSystem().assignController(*in.capturedController, captureStation_);
            notice_ = std::format("controller assigned to station {}", captureStation_ + 1);
            noticeTime_ = 3.0;
            capture_ = Capture::None;
            host_.playSound(audio::SoundId::MenuSelect);
        } else if (captureTime_ <= 0.0) {
            host_.inputSystem().cancelCapture();
            capture_ = Capture::None;
            notice_ = "nothing pressed - cancelled";
            noticeTime_ = 2.0;
        }
        return;
    }

    const auto pressed = [&](input::Action a) { return in.systemPressed[static_cast<std::size_t>(a)]; };
    const bool p1Fire = in.stationPressed[0][static_cast<std::size_t>(input::Action::Fire)];
    const bool p1Start = in.stationPressed[0][static_cast<std::size_t>(input::Action::Start)];

    if (pageId_ == PageId::InputTest || pageId_ == PageId::DisplayTest) {
        backHeld_ = host_.inputSystem().systemHeld(input::Action::MenuBack) ? backHeld_ + dt : 0.0;
        if (pressed(input::Action::Service) || backHeld_ >= 1.0 || (pageId_ == PageId::DisplayTest && pressed(input::Action::MenuBack))) {
            show(PageId::Main);
            host_.playSound(audio::SoundId::MenuSelect);
        }
        return;
    }

    if (playerMode_ && (pressed(input::Action::Service) || pressed(input::Action::MenuBack))) {
        close(true);
        host_.playSound(audio::SoundId::MenuSelect);
        return;
    }
    if (pressed(input::Action::Service)) {
        if (pageId_ != PageId::Main)
            show(PageId::Main);
        else
            close(true);
        host_.playSound(audio::SoundId::MenuSelect);
        return;
    }
    if (pressed(input::Action::MenuBack)) {
        if (pageId_ != PageId::Main) {
            show(PageId::Main);
        } else {
            for (std::size_t i = 0; i < page_.items().size(); ++i)
                if (page_.items()[i].label == "save & exit") page_.select(static_cast<int>(i));
        }
        host_.playSound(audio::SoundId::MenuMove);
        return;
    }
    if (pressed(input::Action::MenuUp)) {
        page_.move(-1);
        host_.playSound(audio::SoundId::MenuMove);
    }
    if (pressed(input::Action::MenuDown) || p1Start) {
        page_.move(1);
        host_.playSound(audio::SoundId::MenuMove);
    }
    if (pressed(input::Action::MenuLeft) && page_.adjust(-1)) host_.playSound(audio::SoundId::MenuMove);
    if (pressed(input::Action::MenuRight) && page_.adjust(1)) host_.playSound(audio::SoundId::MenuMove);
    if (pressed(input::Action::MenuSelect) || p1Fire) {
        if (page_.activate()) host_.playSound(audio::SoundId::MenuSelect);
    }
    if (pendingPage_) {
        const PageId next = *pendingPage_;
        pendingPage_.reset();
        if (open_) show(next);
    }
}

// ------------------------------------------------------------------ drawing

void ServiceMenu::draw(render::Renderer2D& r, const render::ScreenLayout& l, double time) {
    if (!open_) return;
    const float u = l.uiScale;
    const float x0 = l.area.x + 80 * u;
    const float x1 = l.area.x + l.area.w - 80 * u;
    lastLayout_ = l;
    rowHits_.clear();
    backButton_ = {};
    valueZoneX_ = x1 - (x1 - x0) * 0.4f;
    valueZoneEnd_ = x1 + 60 * u;
    const bool touch = touchMode_;
    r.fillRect({0, 0, l.width, l.height}, colors::kBlack);
    r.text(std::format("{} {}", kTitle, kVersion), {x0, l.area.y + 40 * u}, 16 * u, colors::kDim);
    r.text(page_.title(), {x0, l.area.y + 80 * u}, 34 * u, colors::kWhite);
    if (touch) {
        backButton_ = {x1 - 150 * u, l.area.y + 66 * u, 170 * u, 64 * u};
        r.ring({backButton_.x + 32 * u, backButton_.y + 32 * u}, 22 * u, 2.0f * u, colors::kWhite);
        r.line({backButton_.x + 40 * u, backButton_.y + 22 * u}, {backButton_.x + 28 * u, backButton_.y + 32 * u}, 2.4f * u, colors::kWhite);
        r.line({backButton_.x + 28 * u, backButton_.y + 32 * u}, {backButton_.x + 40 * u, backButton_.y + 42 * u}, 2.4f * u, colors::kWhite);
        r.text("back", {backButton_.x + 66 * u, backButton_.y + 20 * u}, 22 * u, colors::kWhite);
    }

    if (pageId_ == PageId::InputTest) {
        drawInputTest(r, l, time);
    } else if (pageId_ == PageId::DisplayTest) {
        drawDisplayTest(r, l, time);
    } else {
        const float rowH = 40 * u;
        const float top = l.area.y + 170 * u;
        const int visible = std::max(4, static_cast<int>((l.area.h - 330 * u) / rowH));
        const int n = static_cast<int>(page_.items().size());
        const int first = std::clamp(page_.selected() - visible / 2, 0, std::max(0, n - visible));
        float y = top;
        for (int i = first; i < std::min(n, first + visible); ++i) {
            const MenuItem& it = page_.items()[static_cast<std::size_t>(i)];
            if (it.separatorBefore && i != first) y += rowH * 0.35f;
            const bool sel = i == page_.selected();
            const Color col = !it.enabled ? colors::kDim : (sel ? colors::kBlack : colors::kWhite);
            if (sel) r.fillRect({x0 - 16 * u, y - 10 * u, x1 - x0 + 32 * u, rowH - 4 * u}, colors::kWhite);
            r.text(it.label, {x0, y}, 18 * u, col);
            if (it.value) r.text(it.value(), {x1, y}, 18 * u, sel ? colors::kBlack : colors::kDim, Align::Right);
            if (touch && sel && it.adjust) {
                // Touch: the value end of the selected row is a  -  /  +  pair.
                r.text("<", {valueZoneX_, y}, 18 * u, colors::kBlack);
                r.text(">", {x1 + 22 * u, y}, 18 * u, colors::kWhite);
            }
            rowHits_.push_back({y - 10 * u, y - 10 * u + rowH, i});
            y += rowH;
        }
    }

    // Footer: help, notices and cabinet health.
    const float fy = l.area.y + l.area.h - 120 * u;
    if (capture_ == Capture::Key) {
        r.text(std::format("press a key for {}", captureLabel_), {x0, fy - 40 * u}, 22 * u, colors::kWhite);
    } else if (capture_ == Capture::Keyboard) {
        r.text(std::format("press any key on the keyboard for P{}", captureStation_ + 1), {x0, fy - 40 * u}, 22 * u, colors::kWhite);
    } else if (capture_ == Capture::Controller) {
        r.text(std::format("press any button on the controller for station {}", captureStation_ + 1), {x0, fy - 40 * u}, 22 * u,
               colors::kWhite);
    } else if (noticeTime_ > 0.0) {
        r.text(notice_, {x0, fy - 40 * u}, 18 * u, colors::kWhite);
    }
    const std::string help = touch ? (pageId_ == PageId::InputTest || pageId_ == PageId::DisplayTest
                                          ? "tap back to return"
                                          : "tap a row to choose it, tap it again to open or change it   tap < or > to change a value   back returns")
                             : (pageId_ == PageId::InputTest)   ? "hold back for 1 second or press service to return"
                             : (pageId_ == PageId::DisplayTest) ? "back or service to return"
                                                                : "up / down move   left / right change   select confirm   back return   service save & exit";
    r.text(help, {x0, fy}, 13 * u, colors::kDim);
    const std::size_t issues = host_.settingsIssueCount();
    r.text(std::format("settings: {}   controller database: {}", issues == 0 ? std::string("ok") : std::format("{} invalid value(s), see log", issues),
                       host_.controllerDbSummary()),
           {x0, fy + 34 * u}, 13 * u, issues == 0 ? colors::kDim : Color{1.0f, 0.6f, 0.6f, 1.0f});
    const auto problems = log::recentProblems(1);
    if (!problems.empty()) r.text(problems.back().substr(0, 140), {x0, fy + 62 * u}, 11 * u, colors::kDim);
}

void ServiceMenu::drawInputTest(render::Renderer2D& r, const render::ScreenLayout& l, double) {
    const float u = l.uiScale;
    input::InputSystem& in = host_.inputSystem();
    for (int s = 0; s < 4; ++s) {
        const render::Rect& col = l.columns[static_cast<std::size_t>(s)];
        const float cx = col.x + col.w * 0.5f;
        r.text(std::format("P{}", s + 1), {cx, l.area.y + 170 * u}, 30 * u, colors::kWhite, Align::Center);
        const bool fire = in.stationHeld(s, input::Action::Fire);
        const bool start = in.stationHeld(s, input::Action::Start);
        const render::Vec2 a{cx - 60 * u, l.area.y + 290 * u}, b{cx + 60 * u, l.area.y + 290 * u};
        if (fire) r.fillCircle(a, 34 * u, colors::kWhite);
        else r.ring(a, 34 * u, 2 * u, colors::kDim);
        if (start) r.fillCircle(b, 34 * u, colors::kWhite);
        else r.ring(b, 34 * u, 2 * u, colors::kDim);
        r.text("fire", {a.x, a.y + 50 * u}, 14 * u, colors::kDim, Align::Center);
        r.text("start", {b.x, b.y + 50 * u}, 14 * u, colors::kDim, Align::Center);
        // Left / right as two small arrows between the buttons.
        const bool left = in.stationHeld(s, input::Action::Left), right = in.stationHeld(s, input::Action::Right);
        const float ay = l.area.y + 236 * u;
        const render::Vec2 la[] = {{cx - 8 * u, ay - 10 * u}, {cx - 22 * u, ay}, {cx - 8 * u, ay + 10 * u}};
        const render::Vec2 ra[] = {{cx + 8 * u, ay - 10 * u}, {cx + 22 * u, ay}, {cx + 8 * u, ay + 10 * u}};
        r.polyline(la, left ? 4 * u : 2 * u, left ? colors::kWhite : colors::kDim);
        r.polyline(ra, right ? 4 * u : 2 * u, right ? colors::kWhite : colors::kDim);
        const input::InputDevice* d = in.devices().controllerForStation(s);
        r.text(d ? d->name : std::string("keyboard only"), {cx, l.area.y + 380 * u}, 12 * u, colors::kDim, Align::Center);
    }
    // System actions.
    const std::pair<input::Action, const char*> sys[] = {
        {input::Action::Coin, "coin"},          {input::Action::ServiceCoin, "svc coin"}, {input::Action::Service, "service"},
        {input::Action::Exit, "exit"},          {input::Action::MenuUp, "up"},           {input::Action::MenuDown, "down"},
        {input::Action::MenuLeft, "left"},      {input::Action::MenuRight, "right"},     {input::Action::MenuSelect, "select"},
        {input::Action::MenuBack, "back"}};
    float x = l.area.x + 100 * u;
    const float step = (l.area.w - 200 * u) / 10.0f;
    for (const auto& [a, name] : sys) {
        const render::Vec2 p{x + step * 0.5f, l.area.y + 470 * u};
        if (in.systemHeld(a)) r.fillCircle(p, 14 * u, colors::kWhite);
        else r.ring(p, 14 * u, 1.6f * u, colors::kDim);
        r.text(name, {p.x, p.y + 26 * u}, 11 * u, colors::kDim, Align::Center);
        x += step;
    }
    // Device list with live controller state.
    float y = l.area.y + 560 * u;
    r.text("devices", {l.area.x + 80 * u, y}, 18 * u, colors::kWhite);
    y += 40 * u;
    int shown = 0;
    for (const auto& d : in.devices().devices()) {
        if (shown++ >= 8) break;
        const std::string station = d->kind == input::DeviceKind::Keyboard ? std::string("") :
                                    d->station >= 0                         ? std::format("station {}", d->station + 1)
                                                                            : std::string("unassigned");
        r.text(std::format("{}  {}  {}", d->typeLabel, d->name, station), {l.area.x + 80 * u, y}, 13 * u, colors::kWhite);
        float bx = l.area.x + l.area.w - 80 * u;
        if (d->kind == input::DeviceKind::Gamepad) {
            for (int b = SDL_GAMEPAD_BUTTON_COUNT - 1; b >= 0; --b) {
                const render::Vec2 p{bx, y + 6 * u};
                if (d->buttons[static_cast<std::size_t>(b)]) r.fillCircle(p, 5 * u, colors::kWhite);
                else r.ring(p, 5 * u, 1.2f * u, colors::kFaint);
                bx -= 14 * u;
            }
            for (int ax = SDL_GAMEPAD_AXIS_COUNT - 1; ax >= 0; --ax) {
                const float v = d->axes[static_cast<std::size_t>(ax)];
                r.fillRect({bx - 30 * u, y + 2 * u, 30 * u, 8 * u}, colors::kFaint);
                r.fillRect({bx - 15 * u + std::min(0.0f, v) * 15 * u, y + 2 * u, std::fabs(v) * 15 * u, 8 * u}, colors::kWhite);
                bx -= 38 * u;
            }
        } else if (d->kind == input::DeviceKind::Joystick) {
            for (std::size_t b = d->rawButtons.size(); b-- > 0;) {
                const render::Vec2 p{bx, y + 6 * u};
                if (d->rawButtons[b]) r.fillCircle(p, 5 * u, colors::kWhite);
                else r.ring(p, 5 * u, 1.2f * u, colors::kFaint);
                bx -= 14 * u;
            }
        }
        y += 30 * u;
    }
    if (shown == 0) r.text("no devices reported", {l.area.x + 80 * u, y}, 13 * u, colors::kDim);
    r.text(std::format("last: {}", in.lastEventText().empty() ? std::string("-") : in.lastEventText()), {l.area.x + 80 * u, l.area.y + 900 * u},
           14 * u, colors::kWhite);
}

void ServiceMenu::drawDisplayTest(render::Renderer2D& r, const render::ScreenLayout& l, double time) {
    const float u = l.uiScale;
    const float grid = 60 * u;
    for (float x = 0; x <= l.width; x += grid) r.line({x, 0}, {x, l.height}, 1.0f, colors::kFaint);
    for (float y = 0; y <= l.height; y += grid) r.line({0, y}, {l.width, y}, 1.0f, colors::kFaint);
    r.line({0, 0}, {l.width, l.height}, 1.0f, colors::kDim);
    r.line({l.width, 0}, {0, l.height}, 1.0f, colors::kDim);
    for (int s = 0; s < 4; ++s) {
        const render::Vec2 c = l.circleCenter[static_cast<std::size_t>(s)];
        r.ring(c, 150 * l.scale, 1.5f, colors::kWhite);
        r.fillCircle(c, 46 * l.scale, colors::kWhite);
        const float a = static_cast<float>(time * 2.0 + s);
        const render::Vec2 p{c.x + 150 * l.scale * std::cos(a), c.y + 150 * l.scale * std::sin(a)};
        r.line(c, p, 2.2f * l.scale, colors::kWhite);
        r.fillCircle(p, 11 * l.scale, colors::kWhite);
    }
    r.fillRect({l.area.x + 80 * u, l.area.y + 760 * u, l.area.w - 160 * u, 150 * u}, colors::kBlack.withAlpha(0.85f));
    r.text(host_.display().describe(), {l.area.x + 100 * u, l.area.y + 780 * u}, 16 * u, colors::kWhite);
    r.text(std::format("frame {:.2f} ms   {} draw call(s), {} vertices", host_.frameMilliseconds(), r.lastDrawCalls(), r.lastVertexCount()),
           {l.area.x + 100 * u, l.area.y + 820 * u}, 16 * u, colors::kWhite);
    r.text("grid squares must look square, circles round and edges smooth", {l.area.x + 100 * u, l.area.y + 860 * u}, 14 * u, colors::kDim);
}

} // namespace spindle::menus
