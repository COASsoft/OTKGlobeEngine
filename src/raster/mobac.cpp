#include "ote/engine.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace ote {

namespace {

std::string trim(std::string s) {
    auto ns = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), ns));
    s.erase(std::find_if(s.rbegin(), s.rend(), ns).base(), s.end());
    return s;
}

std::string child(const pugi::xml_node& n, const char* name) { return trim(n.child_value(name)); }

int childInt(const pugi::xml_node& n, const char* name, int def) {
    std::string v = child(n, name);
    if (v.empty()) return def;
    try {
        return std::stoi(v);
    } catch (...) {
        return def;
    }
}

bool childBool(const pugi::xml_node& n, const char* name) {
    std::string v = child(n, name);
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v == "true" || v == "1";
}

void parseSource(const pugi::xml_node& n, std::vector<ImageryLayer>& out) {
    std::string kind = n.name();
    ImageryLayer l;
    l.name = child(n, "name");
    l.minZoom = childInt(n, "minZoom", 0);
    l.maxZoom = childInt(n, "maxZoom", 19);
    l.url = child(n, "url");
    if (kind == "customMapSource") {
        l.type = SourceType::Url;
        l.tms = childBool(n, "invertYCoordinate");
        std::istringstream parts(child(n, "serverParts"));
        for (std::string p; parts >> p;) l.servers.push_back(p);
        out.push_back(l);
    } else if (kind == "customWmsMapSource") {
        l.type = SourceType::Wms;
        l.wmsLayers = child(n, "layers");
        l.wmsStyles = child(n, "styles");
        std::string version = child(n, "version");
        if (!version.empty()) l.wmsVersion = version;
        std::string tileType = child(n, "tileType");
        std::transform(tileType.begin(), tileType.end(), tileType.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!tileType.empty()) l.wmsFormat = tileType == "jpg" ? "image/jpeg" : "image/" + tileType;
        std::string crs = child(n, "coordinatesystem");
        l.srid = crs.find("4326") != std::string::npos ? 4326 : 3857;
        l.wmsExtra = child(n, "aditionalparameters");
        if (l.wmsExtra.empty()) l.wmsExtra = child(n, "additionalparameters");
        out.push_back(l);
    } else if (kind == "customMultiLayerMapSource") {
        for (auto c : n.child("layers").children()) parseSource(c, out);
    }
}

}

std::vector<ImageryLayer> loadMobac(const std::string& xmlOrPath) {
    pugi::xml_document doc;
    pugi::xml_parse_result ok = xmlOrPath.find('<') != std::string::npos ? doc.load_string(xmlOrPath.c_str())
                                                                         : doc.load_file(xmlOrPath.c_str());
    std::vector<ImageryLayer> out;
    if (!ok) return out;
    for (auto n : doc.children()) parseSource(n, out);
    return out;
}

}
