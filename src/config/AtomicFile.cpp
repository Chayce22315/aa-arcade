#include "config/AtomicFile.hpp"

#include <cstdio>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace spindle::config {

std::optional<std::string> readFile(const std::filesystem::path& path, std::size_t maxBytes) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) return std::nullopt;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > maxBytes) return std::nullopt;
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::string data(static_cast<std::size_t>(size), '\0');
    in.read(data.data(), static_cast<std::streamsize>(size));
    if (static_cast<std::uintmax_t>(in.gcount()) != size) return std::nullopt;
    return data;
}

bool writeFileAtomic(const std::filesystem::path& path, std::string_view content, std::string* error) {
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);

#if defined(_WIN32)
    FILE* f = _wfopen(tmp.c_str(), L"wb");
#else
    FILE* f = std::fopen(tmp.c_str(), "wb");
#endif
    if (!f) {
        if (error) *error = "cannot create " + tmp.string();
        return false;
    }
    const std::size_t written = std::fwrite(content.data(), 1, content.size(), f);
    bool ok = written == content.size() && std::fflush(f) == 0;
#if defined(_WIN32)
    ok = ok && _commit(_fileno(f)) == 0;
#else
    ok = ok && fsync(fileno(f)) == 0;
#endif
    ok = (std::fclose(f) == 0) && ok;
    if (!ok) {
        std::filesystem::remove(tmp, ec);
        if (error) *error = "write failed for " + tmp.string();
        return false;
    }
#if defined(_WIN32)
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(tmp, ec);
        if (error) *error = "cannot replace " + path.string();
        return false;
    }
#else
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        if (error) *error = "cannot replace " + path.string();
        return false;
    }
#endif
    return true;
}

bool isDirectoryWritable(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path probe = dir / ".spindle_write_probe";
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << "ok";
        if (!out) return false;
    }
    return std::filesystem::remove(probe, ec) && !ec;
}

} // namespace spindle::config
