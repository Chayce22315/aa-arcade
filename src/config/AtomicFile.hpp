#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace spindle::config {

// Reads a whole file; nullopt if it does not exist or cannot be read. Files above maxBytes fail.
std::optional<std::string> readFile(const std::filesystem::path& path, std::size_t maxBytes = 16u * 1024u * 1024u);

// Writes to <path>.tmp, flushes it to disk, then atomically replaces <path>. A power cut leaves
// either the old or the new file, never a torn one.
bool writeFileAtomic(const std::filesystem::path& path, std::string_view content, std::string* error = nullptr);

// True if a file can be created and removed inside dir.
bool isDirectoryWritable(const std::filesystem::path& dir);

} // namespace spindle::config
