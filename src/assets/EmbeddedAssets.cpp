#include "assets/EmbeddedAssets.hpp"

namespace spindle::assets {

std::span<const EmbeddedFile> embeddedFiles() {
    return {detail::kEmbeddedFiles, detail::kEmbeddedFileCount};
}

std::optional<std::span<const unsigned char>> findEmbedded(std::string_view path) {
    for (const EmbeddedFile& f : embeddedFiles()) {
        if (path == f.path) return std::span<const unsigned char>(f.data, f.size);
    }
    return std::nullopt;
}

std::optional<std::string_view> embeddedText(std::string_view path) {
    auto bytes = findEmbedded(path);
    if (!bytes) return std::nullopt;
    return std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size());
}

} // namespace spindle::assets
