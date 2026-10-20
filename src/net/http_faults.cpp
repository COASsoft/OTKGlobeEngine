#include "net/http.hpp"

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <random>
#include <thread>

namespace ote::net {

Response getBackend(const std::string& url, const std::string& userAgent, const Headers& headers);

namespace {

struct Faults {
    double delayMs = 0;
    double jitterMs = 0;
    double failRate = 0;
    double timeoutRate = 0;
    double corruptRate = 0;
    double missingRate = 0;
    bool unreachable = false;
    bool any = false;
};

double envNumber(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::atof(v) : 0.0;
}

const Faults& faults() {
    static const Faults f = [] {
        Faults r;
        r.delayMs = envNumber("OTE_NET_DELAY_MS");
        r.jitterMs = envNumber("OTE_NET_JITTER_MS");
        r.failRate = envNumber("OTE_NET_FAIL_RATE");
        r.timeoutRate = envNumber("OTE_NET_TIMEOUT_RATE");
        r.corruptRate = envNumber("OTE_NET_CORRUPT_RATE");
        r.missingRate = envNumber("OTE_NET_404_RATE");
        r.unreachable = envNumber("OTE_NET_UNREACHABLE") > 0;
        r.any = r.delayMs > 0 || r.jitterMs > 0 || r.failRate > 0 || r.timeoutRate > 0 || r.corruptRate > 0 || r.missingRate > 0 ||
                r.unreachable;
        return r;
    }();
    return f;
}

double roll() {
    static std::mutex m;
    static std::mt19937_64 rng(0x07e5eedull);
    std::lock_guard lock(m);
    return std::uniform_real_distribution<double>(0.0, 1.0)(rng);
}

}

Response faulted(const std::string& url, const std::string& userAgent, const Headers& headers) {
    const Faults& f = faults();
    if (!f.any) return getBackend(url, userAgent, headers);
    double wait = f.delayMs + f.jitterMs * roll();
    if (wait > 0) std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long long>(wait * 1000.0)));
    Response out;
    if (f.unreachable) {
        out.error = "injected unreachable network";
        return out;
    }
    double r = roll();
    if (r < f.failRate) {
        out.status = 503;
        out.error = "injected server error";
        return out;
    }
    r -= f.failRate;
    if (r < f.timeoutRate) {
        out.status = 0;
        out.error = "injected timeout";
        return out;
    }
    r -= f.timeoutRate;
    if (r < f.missingRate) {
        out.status = 404;
        out.error = "injected missing tile";
        return out;
    }
    r -= f.missingRate;
    out = getBackend(url, userAgent, headers);
    if (r < f.corruptRate && out.status == 200) {
        for (size_t i = 0; i < out.body.size(); ++i) out.body[i] = static_cast<std::uint8_t>(roll() * 256.0);
    }
    return out;
}

Response get(const std::string& url, const std::string& userAgent, const Headers& headers) {
    if (!admit(url)) {
        Response out;
        out.error = "host unreachable";
        return out;
    }
    Response out = faulted(url, userAgent, headers);
    report(url, out.status);
    return out;
}

}
