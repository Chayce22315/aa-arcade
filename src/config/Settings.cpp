#include "config/Settings.hpp"

#include <charconv>
#include <cmath>
#include <format>
#include <locale>
#include <span>
#include <sstream>

#include "assets/ControllerDbPin.hpp"
#include "engine/Branding.hpp"
#include "gameplay/Modes.hpp"
#include "gameplay/Powerups.hpp"

namespace spindle::config {
namespace {

template <class E>
struct Choice {
    std::string_view name;
    E value;
};

constexpr Choice<FullscreenMode> kFullscreenChoices[] = {
    {"borderless", FullscreenMode::Borderless}, {"exclusive", FullscreenMode::Exclusive}, {"windowed", FullscreenMode::Windowed}};
constexpr Choice<FullscreenMode> kFullscreenAliases[] = {
    {"1", FullscreenMode::Borderless},   {"true", FullscreenMode::Borderless}, {"yes", FullscreenMode::Borderless},
    {"on", FullscreenMode::Borderless},  {"0", FullscreenMode::Windowed},      {"false", FullscreenMode::Windowed},
    {"no", FullscreenMode::Windowed},    {"off", FullscreenMode::Windowed}};
constexpr Choice<Difficulty> kDifficultyChoices[] = {
    {"easy", Difficulty::Easy}, {"normal", Difficulty::Normal}, {"hard", Difficulty::Hard}, {"expert", Difficulty::Expert}};
constexpr Choice<RoundEndScreen> kRoundEndChoices[] = {
    {"full", RoundEndScreen::Full}, {"short", RoundEndScreen::Short}, {"off", RoundEndScreen::Off}};
constexpr Choice<AssignmentMode> kAssignmentChoices[] = {{"auto", AssignmentMode::Auto}, {"fixed", AssignmentMode::Fixed}};
constexpr Choice<DisconnectAction> kDisconnectChoices[] = {{"pause", DisconnectAction::Pause},
                                                           {"continue", DisconnectAction::Continue}};
constexpr Choice<LogLevelSetting> kLogLevelChoices[] = {{"error", LogLevelSetting::Error},
                                                        {"warn", LogLevelSetting::Warn},
                                                        {"info", LogLevelSetting::Info},
                                                        {"debug", LogLevelSetting::Debug}};

std::optional<long long> parseInteger(std::string_view s) {
    s = trim(s);
    if (s.empty()) return std::nullopt;
    if (s.front() == '+') s.remove_prefix(1);
    long long v = 0;
    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || p != s.data() + s.size()) return std::nullopt;
    return v;
}

// Plain decimal numbers only ("1.5", "-2", ".25", "3e-2"): no inf, nan or hex. Parsed in the classic
// locale, so a comma-decimal system locale never changes what settings.ini means. (std::from_chars for
// floating point is missing from the C++ libraries of Android's NDK and older Apple SDKs.)
std::optional<double> parseReal(std::string_view s) {
    s = trim(s);
    if (s.empty()) return std::nullopt;
    if (s.front() == '+') s.remove_prefix(1);
    std::size_t i = 0;
    if (i < s.size() && s[i] == '-') ++i;
    std::size_t digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i, ++digits;
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i, ++digits;
    }
    if (digits == 0) return std::nullopt;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
        std::size_t expDigits = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i, ++expDigits;
        if (expDigits == 0) return std::nullopt;
    }
    if (i != s.size()) return std::nullopt;
    std::istringstream in{std::string(s)};
    in.imbue(std::locale::classic());
    double v = 0.0;
    in >> v;
    if (in.fail() || !std::isfinite(v)) return std::nullopt;
    return v;
}

std::string formatReal(double v) {
    std::string s = std::format("{:.3f}", v);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.push_back('0');
    return s;
}

std::string validPowerups() {
    std::string out;
    for (gameplay::PowerupKind k : gameplay::kAllPowerups) {
        out += gameplay::powerupName(k);
        out += ", ";
    }
    return out + "none";
}

template <class E, std::size_t N>
std::span<const Choice<E>> choices(const Choice<E> (&a)[N]) {
    return std::span<const Choice<E>>(a, N);
}

template <class E>
std::string_view choiceName(std::span<const Choice<E>> choices, E value) {
    for (const auto& c : choices)
        if (c.value == value) return c.name;
    return choices.front().name;
}

template <class E>
std::string choiceList(std::span<const Choice<E>> choices) {
    std::string s;
    for (const auto& c : choices) {
        if (!s.empty()) s += " | ";
        s += c.name;
    }
    return s;
}

// The schema: every setting appears exactly once here; loading, saving and the default file all
// walk this list, so they can never disagree about names, ranges or defaults.
template <class V>
void visitAll(Settings& s, V& v) {
    v.section("Display", "Video output. Changes made in the service menu apply immediately.");
    v.resolution("resolution", s.display.resolution, "auto (desktop resolution of display_index) or WIDTHxHEIGHT, e.g. 1920x1080");
    v.choice("fullscreen", s.display.fullscreen, choices(kFullscreenChoices), "borderless desktop, exclusive mode switch, or windowed",
             choices(kFullscreenAliases));
    v.boolean("vsync", s.display.vsync, "1 = sync to the display refresh");
    v.integer("display_index", s.display.displayIndex, 0, 15, "which monitor to use, 0 = primary");
    v.integer("max_fps", s.display.maxFps, 0, 1000, "frame cap when vsync = 0; 0 = uncapped (values 1..29 are raised to 30)");

    v.section("Audio", "Volumes are percentages.");
    v.integer("master_volume", s.audio.masterVolume, 0, 100, "overall volume");
    v.integer("sfx_volume", s.audio.sfxVolume, 0, 100, "gameplay sounds");
    v.integer("ui_volume", s.audio.uiVolume, 0, 100, "coin, join and menu sounds");
    v.boolean("attract_sound", s.audio.attractSound, "1 = play sounds during attract mode");

    v.section("Gameplay", "Round rules shared by every station. Speed values are uncalibrated: tune by feel.");
    v.integer("tries", s.gameplay.tries, 1, 9, "tries per player per match; a failed round costs one");
    v.integer("round_time_limit", s.gameplay.roundTimeLimit, 10, 300, "seconds; a player still playing at the limit fails the round");
    v.real("rotation_speed_scale", s.gameplay.rotationSpeedScale, 0.5, 2.0, "multiplies every circle rotation speed");
    v.real("needle_speed_scale", s.gameplay.needleSpeedScale, 0.5, 2.0, "multiplies needle flight speed");
    v.real("ready_delay", s.gameplay.readyDelay, 0.5, 5.0, "seconds between rounds before every survivor starts together");
    v.integer("start_round", s.gameplay.startRound, 1, 50, "round number a match starts at");
    v.seed("seed", s.gameplay.seed, "0 = new random levels each match; any other number repeats the same levels");
    v.boolean("powerups", s.gameplay.powerups, "1 = powerup rings appear on the circles from round 3 (never in sudden death)");
    v.powerupList("powerup_types", s.gameplay.powerupTypes,
                  "which rings may appear: shield, skip, slim, mend, heat, purge, swift (comma list, or none); heat needs the fire meter");
    v.boolean("fire_meter", s.gameplay.fireMeter, "1 = quick needles fill a combo meter; full = on fire, needles burn through (never in sudden death)");
    v.integer("fire_combo", s.gameplay.fireCombo, 4, 30, "combo points to catch fire; a needle is 1, a close call 2");
    v.real("fire_seconds", s.gameplay.fireSeconds, 0.5, 5.0, "seconds a player stays on fire");
    v.real("fire_chain_window", s.gameplay.fireChainWindow, 0.5, 5.0, "seconds allowed between needles before the combo drops");
    v.choice("round_end_screen", s.gameplay.roundEndScreen, choices(kRoundEndChoices),
             "full = scores are shot into the circles and the crown moves; short = the same, quicker; off = a plain pause");

    v.section("Bots", "Computer players follow exactly the same rules as humans.");
    v.integer("singleplayer_bots", s.bots.singleplayerBots, 0, 3, "bots joining a game with exactly one human");
    v.integer("multiplayer_bots", s.bots.multiplayerBots, 0, 2, "bots joining a game with two or more humans (capped to free stations)");
    v.choice("difficulty", s.bots.difficulty, choices(kDifficultyChoices), "bot skill in real games");
    v.choice("demo_difficulty", s.bots.demoDifficulty, choices(kDifficultyChoices), "bot skill in the attract demo");

    v.section("Arcade", "Coin and cabinet behaviour.");
    v.boolean("free_play", s.arcade.freePlay, "1 = no credits needed");
    v.integer("credits_per_coin", s.arcade.creditsPerCoin, 1, 9, "credits added per coin");
    v.integer("credits_per_game", s.arcade.creditsPerGame, 1, 9, "credits each player pays to join");
    v.integer("max_credits", s.arcade.maxCredits, 1, 99, "coins above this are still counted but add no credits");
    v.integer("vote_seconds", s.arcade.voteSeconds, 10, 60, "seconds of the join and game mode vote screen after the first start");
    v.integer("game_over_time", s.arcade.gameOverTime, 3, 30, "seconds the results stay on screen");
    v.integer("exit_hold_seconds", s.arcade.exitHoldSeconds, 0, 10, "hold the exit key this long to quit; 0 = locked cabinet (service menu only)");
    v.integer("initials_seconds", s.arcade.initialsSeconds, 10, 30, "seconds a player has to shoot their initials into the high score table");

    v.section("Accessibility", "Easier to read, easier on the eyes.");
    v.boolean("accent_colors", s.accessibility.accentColors, "1 = each station gets its own colour (P1 cyan, P2 pink, P3 green, P4 orange)");
    v.boolean("reduce_flashes", s.accessibility.reduceFlashes, "1 = softer fail flashes and no shaking");

    v.section("Modes", "Game modes players can vote for (1 = on). With every mode off, battle stays on.");
    for (std::size_t m = 0; m < gameplay::kModeCount; ++m)
        v.boolean(std::string(gameplay::modeKey(gameplay::kAllModes[m])), s.modes.enabled[m], std::string(gameplay::modeBlurb(gameplay::kAllModes[m])));

    for (int i = 0; i < kStationCount; ++i) {
        v.section(std::format("Keys.Player{}", i + 1), i == 0 ? "Keyboard bindings: SDL scancode names, several keys separated by commas." : "");
        v.text("fire", s.stationKeys[static_cast<std::size_t>(i)].fire, false, "launch a needle");
        v.text("start", s.stationKeys[static_cast<std::size_t>(i)].start, false, "join / start");
        v.text("left", s.stationKeys[static_cast<std::size_t>(i)].left, false, "previous letter / mode");
        v.text("right", s.stationKeys[static_cast<std::size_t>(i)].right, false, "next letter / mode");
    }

    v.section("Keys.System", "Cabinet and service keys.");
    v.text("coin", s.systemKeys.coin, false, "coin mech");
    v.text("service_coin", s.systemKeys.serviceCoin, false, "adds credits without counting a coin");
    v.text("service", s.systemKeys.service, false, "opens the service / test menu");
    v.text("exit", s.systemKeys.exit, false, "hold to quit (see exit_hold_seconds)");
    v.text("menu_up", s.systemKeys.menuUp, false, "service menu navigation");
    v.text("menu_down", s.systemKeys.menuDown, false, "");
    v.text("menu_left", s.systemKeys.menuLeft, false, "");
    v.text("menu_right", s.systemKeys.menuRight, false, "");
    v.text("menu_select", s.systemKeys.menuSelect, false, "");
    v.text("menu_back", s.systemKeys.menuBack, false, "");

    v.section("Input", "Controllers. Button names are SDL gamepad names (a, b, x, y, back, start, leftshoulder, ...).");
    v.choice("assignment", s.input.assignment, choices(kAssignmentChoices),
             "auto: start on a free controller claims the lowest free station; fixed: p1_gamepad.. GUIDs");
    v.boolean("per_keyboard", s.input.perKeyboard, "1 = each station can be tied to its own physical keyboard (Windows raw input)");
    v.choice("disconnect_action", s.input.disconnectAction, choices(kDisconnectChoices),
             "what a match does when a player's controller disconnects");
    v.text("gamepad_fire", s.input.gamepadFire, false, "buttons that launch a needle");
    v.text("gamepad_start", s.input.gamepadStart, false, "button that joins / starts");
    v.text("gamepad_coin", s.input.gamepadCoin, false, "button that inserts a coin, or none");
    for (int i = 0; i < kStationCount; ++i)
        v.text(std::format("p{}_gamepad", i + 1), s.input.gamepadGuid[static_cast<std::size_t>(i)], true,
               i == 0 ? "controller GUID per station, used when assignment = fixed" : "");
    for (int i = 0; i < kStationCount; ++i)
        v.text(std::format("p{}_keyboard", i + 1), s.input.keyboardName[static_cast<std::size_t>(i)], true,
               i == 0 ? "keyboard device name per station, used when per_keyboard = 1" : "");

    v.section("Assets", "Everything the game needs is built in. The only download is the optional controller database.");
    v.boolean("download", s.assets.download, "1 = fetch gamecontrollerdb.txt when it is missing or damaged");
    v.text("controller_db_url", s.assets.controllerDbUrl, false, "must serve the exact file pinned inside spindle.exe");
    v.integer("download_timeout", s.assets.downloadTimeout, 1, 60, "seconds");

    v.section("System", "");
    v.choice("log_level", s.system.logLevel, choices(kLogLevelChoices), "detail written to logs/spindle.log");
}

class LoadVisitor {
public:
    LoadVisitor(const IniDocument& doc, LoadedSettings& out) : doc_(doc), out_(out) {}

    void section(std::string_view name, std::string_view) { section_ = std::string(name); }

    void integer(std::string_view key, int& ref, int lo, int hi, std::string_view) {
        auto raw = fetch(key);
        if (!raw) return;
        auto v = parseInteger(*raw);
        if (!v || *v < lo || *v > hi) return invalid(key, *raw, std::format("expected a whole number {}..{}", lo, hi));
        ref = static_cast<int>(*v);
    }
    void real(std::string_view key, double& ref, double lo, double hi, std::string_view) {
        auto raw = fetch(key);
        if (!raw) return;
        auto v = parseReal(*raw);
        if (!v || *v < lo || *v > hi) return invalid(key, *raw, std::format("expected a number {}..{}", formatReal(lo), formatReal(hi)));
        ref = *v;
    }
    void boolean(std::string_view key, bool& ref, std::string_view) {
        auto raw = fetch(key);
        if (!raw) return;
        auto v = parseBool(*raw);
        if (!v) return invalid(key, *raw, "expected 0 or 1");
        ref = *v;
    }
    template <class E>
    void choice(std::string_view key, E& ref, std::span<const Choice<E>> choices, std::string_view,
                std::span<const Choice<E>> aliases = {}) {
        auto raw = fetch(key);
        if (!raw) return;
        const std::string lower = toLowerAscii(trim(*raw));
        for (const auto& c : choices)
            if (lower == c.name) return void(ref = c.value);
        for (const auto& c : aliases)
            if (lower == c.name) return void(ref = c.value);
        invalid(key, *raw, "expected " + choiceList(choices));
    }
    void text(std::string_view key, std::string& ref, bool allowEmpty, std::string_view) {
        auto raw = fetch(key);
        if (!raw) return;
        std::string v(trim(*raw));
        if (v.empty() && !allowEmpty) return invalid(key, *raw, "must not be empty");
        ref = std::move(v);
    }
    void resolution(std::string_view key, Resolution& ref, std::string_view) {
        auto raw = fetch(key);
        if (!raw) return;
        auto v = parseResolution(*raw);
        if (!v) return invalid(key, *raw, "expected auto or WIDTHxHEIGHT (640x360 .. 16384x16384)");
        ref = *v;
    }
    void powerupList(std::string_view key, std::string& ref, std::string_view) {
        auto raw = fetch(key);
        if (!raw) return;
        std::vector<std::string> unknown;
        const auto kinds = gameplay::parsePowerupList(*raw, &unknown);
        if (!unknown.empty()) return invalid(key, *raw, "unknown powerup '" + unknown.front() + "' (valid: " + validPowerups() + ")");
        ref = gameplay::formatPowerupList(kinds);
    }
    void seed(std::string_view key, std::uint32_t& ref, std::string_view) {
        auto raw = fetch(key);
        if (!raw) return;
        auto v = parseInteger(*raw);
        if (!v || *v < 0 || *v > 4294967295LL) return invalid(key, *raw, "expected 0..4294967295");
        ref = static_cast<std::uint32_t>(*v);
    }

private:
    std::optional<std::string> fetch(std::string_view key) {
        auto v = doc_.get(section_, key);
        if (!v) out_.issues.push_back({IssueKind::Missing, section_, std::string(key), "", "missing; using default"});
        return v;
    }
    void invalid(std::string_view key, std::string_view raw, std::string message) {
        out_.issues.push_back({IssueKind::Invalid, section_, std::string(key), std::string(raw), message + "; using default"});
    }

    const IniDocument& doc_;
    LoadedSettings& out_;
    std::string section_;
};

// Produces (section, key, value, comment) rows; used by both the store and the default-file writer.
class RowVisitor {
public:
    struct Row {
        std::string section;
        std::string sectionComment;
        std::string key;
        std::string value;
        std::string comment;
    };
    std::vector<Row> rows;

    void section(std::string_view name, std::string_view comment) {
        section_ = std::string(name);
        sectionComment_ = std::string(comment);
    }
    void integer(std::string_view key, int& ref, int lo, int hi, std::string_view c) {
        add(key, std::to_string(ref), withInfo(c, std::format("{}..{}", lo, hi)));
    }
    void real(std::string_view key, double& ref, double lo, double hi, std::string_view c) {
        add(key, formatReal(ref), withInfo(c, std::format("{}..{}", formatReal(lo), formatReal(hi))));
    }
    void boolean(std::string_view key, bool& ref, std::string_view c) { add(key, ref ? "1" : "0", withInfo(c, "0 | 1")); }
    template <class E>
    void choice(std::string_view key, E& ref, std::span<const Choice<E>> choices, std::string_view c,
                std::span<const Choice<E>> = {}) {
        add(key, std::string(choiceName(choices, ref)), withInfo(c, choiceList(choices)));
    }
    void text(std::string_view key, std::string& ref, bool, std::string_view c) { add(key, ref, std::string(c)); }
    void resolution(std::string_view key, Resolution& ref, std::string_view c) { add(key, formatResolution(ref), std::string(c)); }
    void seed(std::string_view key, std::uint32_t& ref, std::string_view c) { add(key, std::to_string(ref), std::string(c)); }
    void powerupList(std::string_view key, std::string& ref, std::string_view c) { add(key, ref, std::string(c)); }

private:
    static std::string withInfo(std::string_view comment, std::string_view range) {
        if (comment.empty()) return std::format("valid: {}", range);
        return std::format("{} (valid: {})", comment, range);
    }
    void add(std::string_view key, std::string value, std::string comment) {
        rows.push_back({section_, sectionComment_, std::string(key), std::move(value), std::move(comment)});
        sectionComment_.clear();
    }
    std::string section_;
    std::string sectionComment_;
};

} // namespace

Settings Settings::defaults() {
    Settings s;
    s.assets.controllerDbUrl = std::string(assets::controller_db_pin::kUrl);
    return s;
}

Settings Settings::mobileDefaults() {
    Settings s = defaults();
    s.arcade.freePlay = true;
    return s;
}

std::size_t LoadedSettings::invalidCount() const {
    std::size_t n = 0;
    for (const auto& i : issues)
        if (i.kind == IssueKind::Invalid) ++n;
    return n;
}

LoadedSettings loadSettings(const IniDocument& doc, const Settings& base) {
    LoadedSettings out;
    out.settings = base;
    LoadVisitor v(doc, out);
    visitAll(out.settings, v);
    return out;
}

void storeSettings(const Settings& settings, IniDocument& doc) {
    Settings copy = settings;
    RowVisitor v;
    visitAll(copy, v);
    for (const auto& row : v.rows) {
        // Only brand-new keys get a comment; existing ones keep whatever the operator wrote.
        doc.set(row.section, row.key, row.value, row.comment);
    }
}

std::string defaultSettingsText(const Settings& base) {
    Settings s = base;
    RowVisitor v;
    visitAll(s, v);
    std::string out = std::format(
        "; {} {} settings\n"
        "; Missing or invalid values fall back to their defaults and are reported in logs/spindle.log.\n"
        "; Operators can change most of these from the service menu (default key F2).\n",
        kTitle, kVersion);
    std::string current;
    for (const auto& row : v.rows) {
        if (row.section != current) {
            current = row.section;
            out += std::format("\n[{}]\n", current);
            if (!row.sectionComment.empty()) out += std::format("; {}\n", row.sectionComment);
        }
        if (!row.comment.empty()) out += std::format("; {}: {}\n", row.key, row.comment);
        out += std::format("{} = {}\n", row.key, row.value);
    }
    return out;
}

std::string_view difficultyName(Difficulty d) { return choiceName(choices(kDifficultyChoices), d); }

std::optional<Difficulty> parseDifficulty(std::string_view s) {
    const std::string lower = toLowerAscii(trim(s));
    for (const auto& c : kDifficultyChoices)
        if (lower == c.name) return c.value;
    return std::nullopt;
}

std::string formatResolution(const Resolution& r) {
    if (r.automatic) return "auto";
    return std::format("{}x{}", r.width, r.height);
}

std::optional<Resolution> parseResolution(std::string_view s) {
    const std::string lower = toLowerAscii(trim(s));
    if (lower == "auto") return Resolution{};
    const std::size_t x = lower.find('x');
    if (x == std::string::npos) return std::nullopt;
    auto w = parseInteger(std::string_view(lower).substr(0, x));
    auto h = parseInteger(std::string_view(lower).substr(x + 1));
    if (!w || !h || *w < 640 || *h < 360 || *w > 16384 || *h > 16384) return std::nullopt;
    return Resolution{false, static_cast<int>(*w), static_cast<int>(*h)};
}

std::optional<bool> parseBool(std::string_view s) {
    const std::string lower = toLowerAscii(trim(s));
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") return true;
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") return false;
    return std::nullopt;
}

std::vector<std::string> splitList(std::string_view s) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos <= s.size()) {
        std::size_t e = s.find(',', pos);
        if (e == std::string_view::npos) e = s.size();
        std::string_view tok = trim(s.substr(pos, e - pos));
        if (!tok.empty()) out.emplace_back(tok);
        if (e >= s.size()) break;
        pos = e + 1;
    }
    return out;
}

} // namespace spindle::config
