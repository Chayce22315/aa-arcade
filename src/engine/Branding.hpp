#pragma once

#include <string_view>

#include "spindle/Version.hpp"

namespace spindle {

// The one place the product identity lives. The repository is aa-arcade; the game is spindle.
inline constexpr std::string_view kTitle = "spindle";
inline constexpr std::string_view kOrganization = "spindle";
inline constexpr std::string_view kVersion = SPINDLE_VERSION_STRING;
inline constexpr std::string_view kTagline = "a four station needle battle";

} // namespace spindle
