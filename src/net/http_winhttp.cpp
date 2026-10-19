#include "net/http.hpp"

#include <windows.h>
#include <winhttp.h>

#include <mutex>

namespace ote::net {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

HINTERNET session(const std::string& userAgent) {
    static std::once_flag once;
    static HINTERNET handle = nullptr;
    std::call_once(once, [&] {
        handle = WinHttpOpen(widen(userAgent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                             WINHTTP_NO_PROXY_BYPASS, 0);
        if (!handle)
            handle = WinHttpOpen(widen(userAgent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                 WINHTTP_NO_PROXY_BYPASS, 0);
        if (handle) {
            DWORD protocols = WINHTTP_PROTOCOL_FLAG_HTTP2;
            WinHttpSetOption(handle, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &protocols, sizeof(protocols));
            DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
            WinHttpSetOption(handle, WINHTTP_OPTION_DECOMPRESSION, &decompression, sizeof(decompression));
            WinHttpSetTimeouts(handle, 10000, 10000, 15000, 30000);
        }
    });
    return handle;
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

}

Response get(const std::string& url, const std::string& userAgent) {
    Response res;
    HINTERNET s = session(userAgent);
    if (!s) {
        res.error = "WinHttpOpen failed";
        return res;
    }
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}, path[4096]{};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 4096;
    wchar_t extra[4096]{};
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        res.error = "bad url";
        return res;
    }
    Handle conn{WinHttpConnect(s, host, uc.nPort, 0)};
    if (!conn.h) {
        res.error = "connect failed";
        return res;
    }
    std::wstring target = std::wstring(path) + extra;
    DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle req{WinHttpOpenRequest(conn.h, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
    if (!req.h) {
        res.error = "open request failed";
        return res;
    }
    if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req.h, nullptr)) {
        res.error = "request failed " + std::to_string(GetLastError());
        return res;
    }
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    res.status = static_cast<int>(status);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail) || avail == 0) break;
        size_t off = res.body.size();
        res.body.resize(off + avail);
        DWORD read = 0;
        if (!WinHttpReadData(req.h, res.body.data() + off, avail, &read)) {
            res.body.resize(off);
            break;
        }
        res.body.resize(off + read);
    }
    return res;
}

}
