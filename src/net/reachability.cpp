#include "net/http.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ote::net {

namespace {

constexpr int kTripFailures = 3;
constexpr double kQuietSeconds = 2.0;
constexpr double kFirstBackoff = 10.0;
constexpr double kMaxBackoff = 120.0;

struct Host {
    int failures = 0;
    bool down = false;
    bool probing = false;
    double backoff = 0;
    double downUntil = 0;
    double lastSuccess = -1e9;
    std::string probeUrl;
};

struct State {
    std::mutex mutex;
    std::unordered_map<std::string, Host> hosts;
    std::atomic<std::uint64_t> epoch{0};
    std::atomic<int> down{0};
};

State& state() {
    static State s;
    return s;
}

double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

}

std::string hostKey(const std::string& url) {
    size_t scheme = url.find("://");
    if (scheme == std::string::npos) return {};
    size_t start = scheme + 3;
    size_t end = url.find_first_of("/?#", start);
    std::string key = url.substr(0, end == std::string::npos ? url.size() : end);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return key;
}

bool hostAvailable(const std::string& url) {
    State& s = state();
    if (s.down.load() == 0) return true;
    std::string key = hostKey(url);
    std::lock_guard lock(s.mutex);
    auto it = s.hosts.find(key);
    if (it == s.hosts.end() || !it->second.down) return true;
    return !it->second.probing && seconds() >= it->second.downUntil;
}

bool admit(const std::string& url) {
    State& s = state();
    if (s.down.load() == 0) return true;
    std::string key = hostKey(url);
    std::lock_guard lock(s.mutex);
    auto it = s.hosts.find(key);
    if (it == s.hosts.end() || !it->second.down) return true;
    Host& h = it->second;
    if (h.probing || seconds() < h.downUntil) return false;
    h.probing = true;
    return true;
}

void report(const std::string& url, int status) {
    State& s = state();
    std::string key = hostKey(url);
    if (key.empty()) return;
    double t = seconds();
    std::lock_guard lock(s.mutex);
    Host& h = s.hosts[key];
    if (status > 0) {
        if (h.down) {
            s.down.fetch_sub(1);
            s.epoch.fetch_add(1);
        }
        h = Host{};
        h.lastSuccess = t;
        return;
    }
    h.probeUrl = url;
    if (h.down) {
        if (h.probing) {
            h.probing = false;
            h.backoff = std::min(h.backoff * 2.0, kMaxBackoff);
            h.downUntil = t + h.backoff;
        }
        return;
    }
    ++h.failures;
    if (h.failures >= kTripFailures && t - h.lastSuccess >= kQuietSeconds) {
        h.down = true;
        h.backoff = kFirstBackoff;
        h.downUntil = t + h.backoff;
        s.down.fetch_add(1);
    }
}

std::uint64_t reachabilityEpoch() { return state().epoch.load(); }

void probeDueHosts(const std::string& userAgent) {
    State& s = state();
    if (s.down.load() == 0) return;
    std::vector<std::string> due;
    {
        std::lock_guard lock(s.mutex);
        double t = seconds();
        for (const auto& [key, h] : s.hosts)
            if (h.down && !h.probing && t >= h.downUntil && !h.probeUrl.empty()) due.push_back(h.probeUrl);
    }
    for (const std::string& url : due) get(url, userAgent);
}

int hostsDown() { return state().down.load(); }

void resetReachability() {
    State& s = state();
    std::lock_guard lock(s.mutex);
    bool any = s.down.load() > 0;
    s.hosts.clear();
    s.down.store(0);
    if (any) s.epoch.fetch_add(1);
}

}
