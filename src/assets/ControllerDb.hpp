#pragma once

#include <filesystem>
#include <string>

#include "config/Settings.hpp"

namespace spindle::assets {

enum class ControllerDbState {
    Present,     // on disk and matching the pin
    Downloaded,  // fetched now, verified and stored
    Missing,     // not on disk and not downloaded (download disabled, offline or unavailable)
    Rejected     // on disk or downloaded but not the pinned file; deleted / not stored
};

struct ControllerDbCheck {
    bool exists = false;
    bool valid = false;
};

struct ControllerDbStatus {
    ControllerDbState state = ControllerDbState::Missing;
    std::string detail;
    int mappingsLoaded = 0;
};

// Boot step 1: is the optional community controller database present and the pinned version?
ControllerDbCheck checkControllerDb(const std::filesystem::path& file);

// Boot steps 2-3: download when missing or damaged (if allowed), verify against the pin, store
// atomically. A damaged file on disk is removed so it is never loaded.
ControllerDbStatus ensureControllerDb(const std::filesystem::path& file, const config::Settings::Assets& settings,
                                      const ControllerDbCheck& check);

// Boot step 4: hands the verified file to SDL. Returns the number of mappings added.
int loadControllerDb(const std::filesystem::path& file);

const char* controllerDbStateName(ControllerDbState s);

} // namespace spindle::assets
