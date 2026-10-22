#include "net/http.hpp"

#include <emscripten/fetch.h>
#include <emscripten/threading.h>

#include <cstring>

namespace otk::net {

Response getBackend(const std::string& url, const std::string&, const Headers& headers) {
    Response res;
    if (emscripten_is_main_runtime_thread()) {
        res.error = "blocking request on the browser main thread";
        return res;
    }
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS | EMSCRIPTEN_FETCH_REPLACE;
    attr.timeoutMSecs = 30000;
    std::vector<const char*> list;
    for (const auto& [name, value] : headers) {
        if (name == "User-Agent" || name == "Referer") continue;
        list.push_back(name.c_str());
        list.push_back(value.c_str());
    }
    list.push_back(nullptr);
    attr.requestHeaders = list.size() > 1 ? list.data() : nullptr;
    emscripten_fetch_t* fetch = emscripten_fetch(&attr, url.c_str());
    if (!fetch) {
        res.error = "fetch failed";
        return res;
    }
    res.status = fetch->status;
    if (fetch->data && fetch->numBytes > 0) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(fetch->data);
        res.body.assign(p, p + fetch->numBytes);
    }
    if (res.status == 0) res.error = fetch->statusText[0] ? fetch->statusText : "network error";
    emscripten_fetch_close(fetch);
    return res;
}

void setJavaVm(void*) {}

}
