#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "config/IniDocument.hpp"

namespace spindle::config {

enum class FullscreenMode { Borderless, Exclusive, Windowed };
enum class Difficulty { Easy, Normal, Hard, Expert };
enum class AssignmentMode { Auto, Fixed };
enum class DisconnectAction { Pause, Continue };
enum class LogLevelSetting { Error, Warn, Info, Debug };
enum class RoundEndScreen { Full, Short, Off };

struct Resolution {
    bool automatic = true;
    int width = 0;
    int height = 0;
    bool operator==(const Resolution&) const = default;
};

inline constexpr int kStationCount = 4;

struct StationKeys {
    std::string fire;
    std::string start;
    std::string left;  // initials letter / vote cursor
    std::string right;
    bool operator==(const StationKeys&) const = default;
};

struct Settings {
    struct Display {
        Resolution resolution{};
        FullscreenMode fullscreen = FullscreenMode::Borderless;
        bool vsync = true;
        int displayIndex = 0;
        int maxFps = 0;
        bool operator==(const Display&) const = default;
    } display;

    struct Audio {
        int masterVolume = 80;
        int sfxVolume = 100;
        int uiVolume = 80;
        bool attractSound = true;
        bool operator==(const Audio&) const = default;
    } audio;

    struct Gameplay {
        int tries = 3;
        int roundTimeLimit = 45;
        double rotationSpeedScale = 1.0;
        double needleSpeedScale = 1.0;
        double readyDelay = 1.5;
        int startRound = 1;
        std::uint32_t seed = 0;
        bool powerups = true;
        std::string powerupTypes = "shield, skip, slim, mend, heat, purge, swift";
        bool fireMeter = true;
        int fireCombo = 8;
        double fireSeconds = 1.5;
        double fireChainWindow = 1.0;
        RoundEndScreen roundEndScreen = RoundEndScreen::Full;
        bool operator==(const Gameplay&) const = default;
    } gameplay;

    struct Bots {
        int singleplayerBots = 3;
        int multiplayerBots = 0;
        Difficulty difficulty = Difficulty::Normal;
        Difficulty demoDifficulty = Difficulty::Hard;
        bool operator==(const Bots&) const = default;
    } bots;

    struct Arcade {
        bool freePlay = false;
        int creditsPerCoin = 1;
        int creditsPerGame = 1;
        int maxCredits = 99;
        int voteSeconds = 30; // the join + game mode vote screen
        int gameOverTime = 8;
        int exitHoldSeconds = 2;
        int initialsSeconds = 15;
        bool operator==(const Arcade&) const = default;
    } arcade;

    // Game modes players can vote for, in the order of gameplay::kAllModes (battle ... daily).
    struct Modes {
        std::array<bool, 9> enabled{true, true, true, true, true, true, true, true, true};
        bool operator==(const Modes&) const = default;
    } modes;

    std::array<StationKeys, kStationCount> stationKeys{{{"LCtrl", "1", "W", "D"}, {"A", "2", "J", "K"}, {"RCtrl", "3", "Left", "Right"}, {"L", "4", "F", "H"}}};

    struct SystemKeys {
        std::string coin = "5";
        std::string serviceCoin = "9";
        std::string service = "F2";
        std::string exit = "Escape";
        std::string menuUp = "Up";
        std::string menuDown = "Down";
        std::string menuLeft = "Left";
        std::string menuRight = "Right";
        std::string menuSelect = "Return";
        std::string menuBack = "Backspace";
        bool operator==(const SystemKeys&) const = default;
    } systemKeys;

    struct Input {
        AssignmentMode assignment = AssignmentMode::Auto;
        bool perKeyboard = false;
        DisconnectAction disconnectAction = DisconnectAction::Pause;
        std::string gamepadFire = "a,b,x,y,leftshoulder,rightshoulder";
        std::string gamepadStart = "start";
        std::string gamepadCoin = "back";
        std::array<std::string, kStationCount> gamepadGuid{};
        std::array<std::string, kStationCount> keyboardName{};
        bool operator==(const Input&) const = default;
    } input;

    struct Assets {
        bool download = true;
        std::string controllerDbUrl; // filled from the compiled-in pin by defaults()
        int downloadTimeout = 10;
        bool operator==(const Assets&) const = default;
    } assets;

    struct System {
        LogLevelSetting logLevel = LogLevelSetting::Info;
        bool operator==(const System&) const = default;
    } system;

    static Settings defaults();
    // Defaults for phones and tablets: free play (there is no coin slot), the rest as on a cabinet.
    static Settings mobileDefaults();
    bool operator==(const Settings&) const = default;
};

enum class IssueKind { Missing, Invalid };

struct SettingIssue {
    IssueKind kind = IssueKind::Invalid;
    std::string section;
    std::string key;
    std::string value;
    std::string message;
};

struct LoadedSettings {
    Settings settings;
    std::vector<SettingIssue> issues;
    std::size_t invalidCount() const;
};

// Reads every schema key from the document; anything missing, unparsable or out of range keeps its
// default and is reported. Never modifies the document.
// base supplies the defaults (Settings::defaults() on a cabinet, mobileDefaults() on a phone).
LoadedSettings loadSettings(const IniDocument& doc, const Settings& base = Settings::defaults());

// Writes all values into the document, keeping existing comments and order.
void storeSettings(const Settings& settings, IniDocument& doc);

// A complete, commented settings.ini with default values.
std::string defaultSettingsText(const Settings& base = Settings::defaults());

std::string_view difficultyName(Difficulty d);
std::optional<Difficulty> parseDifficulty(std::string_view s);
std::string formatResolution(const Resolution& r);
std::optional<Resolution> parseResolution(std::string_view s);
std::optional<bool> parseBool(std::string_view s);

// Splits "a, b ,c" into trimmed non-empty tokens.
std::vector<std::string> splitList(std::string_view s);

} // namespace spindle::config
