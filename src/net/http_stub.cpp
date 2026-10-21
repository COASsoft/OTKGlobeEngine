#include "net/http.hpp"

namespace otk::net {

Response getBackend(const std::string&, const std::string&, const Headers&) {
    Response res;
    res.error = "no http backend on this platform";
    return res;
}

void setJavaVm(void*) {}

}
