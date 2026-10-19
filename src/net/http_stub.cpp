#include "net/http.hpp"

namespace ote::net {

Response getBackend(const std::string&, const std::string&) {
    Response res;
    res.error = "no http backend on this platform";
    return res;
}

void setJavaVm(void*) {}

}
