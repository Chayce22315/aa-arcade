#include "assets/ControllerDb.hpp"

#include <format>
#include <system_error>

#include <SDL3/SDL.h>

#include "assets/ControllerDbPin.hpp"
#include "assets/Downloader.hpp"
#include "config/AtomicFile.hpp"
#include "engine/Log.hpp"
#include "engine/Paths.hpp"

namespace spindle::assets {

const char* controllerDbStateName(ControllerDbState s) {
    switch (s) {
    case ControllerDbState::Present: return "present";
    case ControllerDbState::Downloaded: return "downloaded";
    case ControllerDbState::Missing: return "missing";
    case ControllerDbState::Rejected: return "rejected";
    }
    return "missing";
}

ControllerDbCheck checkControllerDb(const std::filesystem::path& file) {
    ControllerDbCheck c;
    std::error_code ec;
    c.exists = std::filesystem::is_regular_file(file, ec);
    if (!c.exists) return c;
    auto data = config::readFile(file, 8u * 1024u * 1024u);
    c.valid = data && controller_db_pin::matches(*data);
    return c;
}

ControllerDbStatus ensureControllerDb(const std::filesystem::path& file, const config::Settings::Assets& settings,
                                      const ControllerDbCheck& check) {
    ControllerDbStatus st;
    if (check.valid) {
        st.state = ControllerDbState::Present;
        st.detail = "pinned version present";
        return st;
    }
    std::error_code ec;
    if (check.exists) {
        log::warn("assets: {} is not the pinned version; removing it", pathToUtf8(file));
        std::filesystem::remove(file, ec);
    }
    if (!settings.download) {
        st.state = check.exists ? ControllerDbState::Rejected : ControllerDbState::Missing;
        st.detail = "download disabled in settings";
        return st;
    }
    if (!downloaderAvailable()) {
        st.state = check.exists ? ControllerDbState::Rejected : ControllerDbState::Missing;
        st.detail = "no downloader in this build";
        return st;
    }
    log::info("assets: downloading {} (commit {})", controller_db_pin::kFileName, controller_db_pin::kCommit);
    const DownloadResult r = httpsGet(settings.controllerDbUrl, settings.downloadTimeout, 8u * 1024u * 1024u);
    if (!r.ok) {
        st.state = ControllerDbState::Missing;
        st.detail = "download failed: " + r.error;
        return st;
    }
    if (!controller_db_pin::matches(r.body)) {
        st.state = ControllerDbState::Rejected;
        st.detail = std::format("downloaded file ({} bytes, sha256 {}) does not match the pin", r.body.size(), sha256Hex(r.body));
        return st;
    }
    std::string err;
    if (!config::writeFileAtomic(file, r.body, &err)) {
        st.state = ControllerDbState::Missing;
        st.detail = "verified but could not be stored: " + err;
        return st;
    }
    st.state = ControllerDbState::Downloaded;
    st.detail = std::format("downloaded and verified ({} bytes)", r.body.size());
    return st;
}

int loadControllerDb(const std::filesystem::path& file) {
    const std::string path = pathToUtf8(file);
    const int n = SDL_AddGamepadMappingsFromFile(path.c_str());
    if (n < 0) log::warn("assets: SDL could not read {}: {}", path, SDL_GetError());
    return n < 0 ? 0 : n;
}

} // namespace spindle::assets
