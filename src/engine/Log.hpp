#pragma once

#include <deque>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace spindle::log {

enum class Level { Error = 0, Warn = 1, Info = 2, Debug = 3 };

// Opens (appends to) the log file. Files above 1 MiB are rotated to <name>.1 first.
bool open(const std::filesystem::path& file);
void close();
void setLevel(Level level);
Level level();
void setEchoToStderr(bool echo);
// Optional extra destination for every line (phones: the system log, i.e. logcat / the Xcode console,
// since stderr goes nowhere there). Called with the log lock held; must not log itself.
using Sink = void (*)(Level level, const char* line);
void setSink(Sink sink);

void write(Level level, std::string_view message);

// Last lines written at Warn or worse, newest last; shown on the service menu.
std::vector<std::string> recentProblems(std::size_t maxLines);

std::optional<Level> parseLevel(std::string_view text);
std::string_view levelName(Level level);

template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
    write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
    if (level() >= Level::Warn) write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
    if (level() >= Level::Info) write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
    if (level() >= Level::Debug) write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace spindle::log
