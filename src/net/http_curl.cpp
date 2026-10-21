#include "net/http.hpp"

#include <curl/curl.h>

#include <mutex>

namespace otk::net {

namespace {

size_t onData(char* ptr, size_t size, size_t n, void* user) {
    auto* body = static_cast<std::vector<std::uint8_t>*>(user);
    body->insert(body->end(), ptr, ptr + size * n);
    return size * n;
}

}

Response getBackend(const std::string& url, const std::string& userAgent, const Headers& headers) {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    thread_local CURL* curl = curl_easy_init();
    Response res;
    if (!curl) {
        res.error = "curl init failed";
        return res;
    }
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res.body);
    curl_slist* list = nullptr;
    for (const auto& [name, value] : headers) list = curl_slist_append(list, (name + ": " + value).c_str());
    if (list) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    CURLcode rc = curl_easy_perform(curl);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, nullptr);
    curl_slist_free_all(list);
    if (rc != CURLE_OK) {
        res.error = curl_easy_strerror(rc);
        return res;
    }
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    res.status = static_cast<int>(status);
    return res;
}

void setJavaVm(void*) {}

}
