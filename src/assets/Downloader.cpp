#include "assets/Downloader.hpp"

#include <chrono>
#include <format>
#include <iterator>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#endif

#include "engine/Branding.hpp"

namespace spindle::assets {

#if defined(_WIN32)

namespace {

struct HInternet {
    HINTERNET h = nullptr;
    HInternet() = default;
    explicit HInternet(HINTERNET x) : h(x) {}
    ~HInternet() {
        if (h) WinHttpCloseHandle(h);
    }
    HInternet(const HInternet&) = delete;
    HInternet& operator=(const HInternet&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string lastError(const char* what) { return std::format("{} failed (Windows error {})", what, GetLastError()); }

} // namespace

bool downloaderAvailable() { return true; }

DownloadResult httpsGet(std::string_view url, int timeoutSeconds, std::size_t maxBytes) {
    DownloadResult r;
    if (url.substr(0, 8) != "https://") {
        r.error = "only https:// URLs are allowed";
        return r;
    }
    std::wstring wurl = widen(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof parts;
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    parts.lpszHostName = host;
    parts.dwHostNameLength = static_cast<DWORD>(std::size(host));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts)) {
        r.error = lastError("WinHttpCrackUrl");
        return r;
    }
    const std::wstring agent = widen(std::format("{}/{}", kTitle, kVersion));
#if defined(WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY)
    const DWORD access = WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
#else
    const DWORD access = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
#endif
    HInternet session(WinHttpOpen(agent.c_str(), access, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        r.error = lastError("WinHttpOpen");
        return r;
    }
    const int ms = timeoutSeconds * 1000;
    WinHttpSetTimeouts(session.h, ms, ms, ms, ms);
    HInternet connect(WinHttpConnect(session.h, host, parts.nPort, 0));
    if (!connect) {
        r.error = lastError("WinHttpConnect");
        return r;
    }
    HInternet request(WinHttpOpenRequest(connect.h, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE));
    if (!request) {
        r.error = lastError("WinHttpOpenRequest");
        return r;
    }
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        r.error = lastError("HTTPS request");
        return r;
    }
    DWORD status = 0;
    DWORD size = sizeof status;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                        &size, WINHTTP_NO_HEADER_INDEX);
    r.httpStatus = static_cast<int>(status);
    if (status != 200) {
        r.error = std::format("server answered HTTP {}", status);
        return r;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    while (true) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available)) {
            r.error = lastError("WinHttpQueryDataAvailable");
            return r;
        }
        if (available == 0) break;
        if (r.body.size() + available > maxBytes) {
            r.error = std::format("response larger than {} bytes", maxBytes);
            return r;
        }
        const std::size_t offset = r.body.size();
        r.body.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, r.body.data() + offset, available, &read)) {
            r.error = lastError("WinHttpReadData");
            return r;
        }
        r.body.resize(offset + read);
        if (std::chrono::steady_clock::now() > deadline) {
            r.error = "download timed out";
            return r;
        }
    }
    r.ok = true;
    return r;
}

#else

bool downloaderAvailable() { return false; }

DownloadResult httpsGet(std::string_view, int, std::size_t) {
    DownloadResult r;
    r.error = "this build has no downloader (Windows builds use WinHTTP)";
    return r;
}

#endif

} // namespace spindle::assets
