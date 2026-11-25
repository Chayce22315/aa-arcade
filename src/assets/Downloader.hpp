#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace spindle::assets {

struct DownloadResult {
    bool ok = false;
    int httpStatus = 0;
    std::string body;
    std::string error;
};

// HTTPS GET into memory. Windows builds use WinHTTP (system proxy settings, TLS from the OS); other
// platforms report the downloader as unavailable. Only https:// URLs are accepted; the body is
// capped at maxBytes and the whole transfer at timeoutSeconds.
bool downloaderAvailable();
DownloadResult httpsGet(std::string_view url, int timeoutSeconds, std::size_t maxBytes);

} // namespace spindle::assets
