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

}
