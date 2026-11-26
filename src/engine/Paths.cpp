#include "engine/Paths.hpp"

#include <string>

#include <SDL3/SDL.h>

#include "config/AtomicFile.hpp"
#include "engine/Branding.hpp"
#include "engine/Platform.hpp"

namespace spindle {

std::filesystem::path pathFromUtf8(const char* utf8) {
    if (!utf8) return {};
    const std::string s(utf8);
    return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

std::string pathToUtf8(const std::filesystem::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

Paths resolvePaths(const std::optional<std::filesystem::path>& root) {
    Paths p;
    if (root) {
        p.base = *root;
        p.writable = *root;
        p.portable = true;
        std::error_code ec;
        std::filesystem::create_directories(*root, ec);
        return p;
    }
    p.base = pathFromUtf8(SDL_GetBasePath());
    if (p.base.empty()) p.base = std::filesystem::current_path();
    // Phones keep the app read-only; its files live in the app's private data folder.
    if (!platform::kMobile && config::isDirectoryWritable(p.base)) {
        p.writable = p.base;
        p.portable = true;
        return p;
    }
    const std::string org(kOrganization), app(kTitle);
    char* pref = SDL_GetPrefPath(org.c_str(), app.c_str());
    if (pref) {
        p.writable = pathFromUtf8(pref);
        SDL_free(pref);
        p.portable = false;
    } else {
        p.writable = p.base;
    }
    return p;
}

} // namespace spindle
