#include "gameplay/Powerups.hpp"

#include <algorithm>

#include "config/IniDocument.hpp"

namespace spindle::gameplay {

std::string_view powerupName(PowerupKind kind) {
    switch (kind) {
    case PowerupKind::Shield: return "shield";
    case PowerupKind::Skip: return "skip";
    case PowerupKind::Slim: return "slim";
    case PowerupKind::Mend: return "mend";
    }
    return "shield";
}

std::optional<PowerupKind> parsePowerup(std::string_view name) {
    const std::string lower = config::toLowerAscii(config::trim(name));
    for (PowerupKind k : kAllPowerups)
        if (lower == powerupName(k)) return k;
    return std::nullopt;
}

bool contains(const std::vector<PowerupKind>& kinds, PowerupKind kind) {
    return std::find(kinds.begin(), kinds.end(), kind) != kinds.end();
}

std::vector<PowerupKind> parsePowerupList(std::string_view list, std::vector<std::string>* unknown) {
    std::vector<PowerupKind> found;
    std::size_t pos = 0;
    while (pos <= list.size()) {
        std::size_t end = list.find(',', pos);
        if (end == std::string_view::npos) end = list.size();
        const std::string_view token = config::trim(list.substr(pos, end - pos));
        if (!token.empty() && config::toLowerAscii(token) != "none") {
            if (auto k = parsePowerup(token)) {
                if (!contains(found, *k)) found.push_back(*k);
            } else if (unknown) {
                unknown->emplace_back(token);
            }
        }
        if (end >= list.size()) break;
        pos = end + 1;
    }
    std::vector<PowerupKind> ordered;
    for (PowerupKind k : kAllPowerups)
        if (contains(found, k)) ordered.push_back(k);
    return ordered;
}

std::string formatPowerupList(const std::vector<PowerupKind>& kinds) {
    std::string out;
    for (PowerupKind k : kAllPowerups) {
        if (!contains(kinds, k)) continue;
        if (!out.empty()) out += ", ";
        out += powerupName(k);
    }
    return out.empty() ? std::string("none") : out;
}

} // namespace spindle::gameplay
