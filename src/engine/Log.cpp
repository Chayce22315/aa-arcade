#include "engine/Log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <system_error>

namespace spindle::log {
namespace {

struct State {
    std::mutex mutex;
    std::ofstream file;
    bool echo = true;
    std::deque<std::string> problems;
};

State& state() {
    static State s;
    return s;
}

std::atomic<int> g_level{static_cast<int>(Level::Info)};

constexpr std::uintmax_t kRotateBytes = 1024u * 1024u;
constexpr std::size_t kProblemHistory = 64;

} // namespace

bool open(const std::filesystem::path& file) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (std::filesystem::exists(file, ec) && std::filesystem::file_size(file, ec) > kRotateBytes) {
        std::filesystem::path rotated = file;
        rotated += ".1";
        std::filesystem::remove(rotated, ec);
        std::filesystem::rename(file, rotated, ec);
    }
    if (s.file.is_open()) s.file.close();
    s.file.open(file, std::ios::out | std::ios::app);
    return s.file.is_open();
}

void close() {
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (s.file.is_open()) s.file.close();
}

void setLevel(Level lvl) { g_level.store(static_cast<int>(lvl)); }
Level level() { return static_cast<Level>(g_level.load()); }

void setEchoToStderr(bool echo) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.echo = echo;
}

void write(Level lvl, std::string_view message) {
    if (static_cast<int>(lvl) > g_level.load()) return;
    const auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    std::string line = std::format("{:%Y-%m-%d %H:%M:%S}Z [{}] {}", now, levelName(lvl), message);
    State& s = state();
    std::lock_guard lock(s.mutex);
    if (s.file.is_open()) {
        s.file << line << '\n';
        s.file.flush();
    }
    if (s.echo) {
        std::fputs(line.c_str(), stderr);
        std::fputc('\n', stderr);
    }
    if (lvl <= Level::Warn) {
        s.problems.push_back(std::move(line));
        while (s.problems.size() > kProblemHistory) s.problems.pop_front();
    }
}

std::vector<std::string> recentProblems(std::size_t maxLines) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    const std::size_t n = std::min(maxLines, s.problems.size());
    return {s.problems.end() - static_cast<std::ptrdiff_t>(n), s.problems.end()};
}

std::optional<Level> parseLevel(std::string_view text) {
    if (text == "error") return Level::Error;
    if (text == "warn") return Level::Warn;
    if (text == "info") return Level::Info;
    if (text == "debug") return Level::Debug;
    return std::nullopt;
}

std::string_view levelName(Level lvl) {
    switch (lvl) {
    case Level::Error: return "error";
    case Level::Warn: return "warn";
    case Level::Info: return "info";
    case Level::Debug: return "debug";
    }
    return "info";
}

} // namespace spindle::log
