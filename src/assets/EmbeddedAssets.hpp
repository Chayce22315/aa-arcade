#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace spindle::assets {

struct EmbeddedFile {
    const char* path;           // relative to data/, forward slashes
    const unsigned char* data;  // followed by one NUL byte not counted in size
    std::size_t size;
};

namespace detail {
extern const EmbeddedFile kEmbeddedFiles[];
extern const std::size_t kEmbeddedFileCount;
} // namespace detail

std::span<const EmbeddedFile> embeddedFiles();
std::optional<std::span<const unsigned char>> findEmbedded(std::string_view path);
// Text view of an embedded file; empty optional if missing.
std::optional<std::string_view> embeddedText(std::string_view path);

} // namespace spindle::assets
