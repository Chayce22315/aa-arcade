#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace spindle {

// Where spindle reads and writes. Portable cabinet layout: everything next to spindle.exe; if that
// folder is not writable (e.g. Program Files) the writable files move to the user pref path.
struct Paths {
    std::filesystem::path base;     // folder of the executable
    std::filesystem::path writable; // settings, bookkeeping, logs, downloaded controller database
    bool portable = true;

    std::filesystem::path settings() const { return writable / "settings.ini"; }
    std::filesystem::path bookkeeping() const { return writable / "bookkeeping.ini"; }
    std::filesystem::path highscores() const { return writable / "highscores.ini"; }
    std::filesystem::path logFile() const { return writable / "logs" / "spindle.log"; }
    std::filesystem::path controllerDb() const { return writable / "gamecontrollerdb.txt"; }
    std::filesystem::path screenshots() const { return writable / "screenshots"; }
    std::filesystem::path progress() const { return writable / "progress.ini"; }
    std::filesystem::path replay(std::string_view modeKey) const { return writable / "replays" / (std::string(modeKey) + ".txt"); }
    // Optional operator overrides of the embedded scripts.
    std::filesystem::path levelScriptOverride() const { return base / "data" / "scripts" / "levels.lua"; }
    std::filesystem::path botScriptOverride() const { return base / "data" / "scripts" / "bots" / "bot.lua"; }
    // The same overrides in the writable folder (the only place a phone user can put files).
    std::filesystem::path levelScriptUser() const { return writable / "scripts" / "levels.lua"; }
    std::filesystem::path botScriptUser() const { return writable / "scripts" / "bot.lua"; }
};

// root, when given, replaces both folders (used by --root and the self-test).
Paths resolvePaths(const std::optional<std::filesystem::path>& root);

std::filesystem::path pathFromUtf8(const char* utf8);
std::string pathToUtf8(const std::filesystem::path& p);

} // namespace spindle
