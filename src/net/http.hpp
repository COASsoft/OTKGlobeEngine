#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ote::net {

struct Response {
    int status = 0;
    std::vector<std::uint8_t> body;
    std::string error;
};

Response get(const std::string& url, const std::string& userAgent);
void setJavaVm(void* vm);

std::string hostKey(const std::string& url);
bool hostAvailable(const std::string& url);
bool admit(const std::string& url);
void report(const std::string& url, int status);
std::uint64_t reachabilityEpoch();
int hostsDown();
void resetReachability();

}
