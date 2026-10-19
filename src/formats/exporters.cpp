#include "formats/adapters.hpp"

#include <json.hpp>

#include <cstdio>

namespace ote::formats {

std::string xmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&apos;";
            break;
        default:
            out += c;
        }
    }
    return out;
}

std::string colorHex(std::uint32_t argb) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%06x", argb & 0xFFFFFFu);
    return buf;
}

std::string uniqueId(const Feature& f, size_t index) {
    if (!f.uid.empty()) return f.uid;
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](const std::string& s) {
        for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    };
    mix(f.name);
    for (const Geometry& g : f.geometries)
        for (const Coord& c : g.points) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.7f,%.7f", c.lat, c.lon);
            mix(buf);
        }
    char buf[48];
    std::snprintf(buf, sizeof(buf), "OTE-%016llx-%zu", static_cast<unsigned long long>(h), index);
    return buf;
}

namespace {

nlohmann::json jsonPos(const Coord& c, bool z) {
    return z ? nlohmann::json::array({c.lon, c.lat, c.alt}) : nlohmann::json::array({c.lon, c.lat});
}

nlohmann::json jsonRing(const std::vector<Coord>& pts, bool close, bool z) {
    nlohmann::json a = nlohmann::json::array();
    for (const Coord& c : pts) a.push_back(jsonPos(c, z));
    if (close && !pts.empty()) a.push_back(jsonPos(pts.front(), z));
    return a;
}

nlohmann::json jsonGeometry(const Geometry& g, bool z) {
    nlohmann::json j;
    if (g.type == GeometryType::Point) {
        j["type"] = "Point";
        j["coordinates"] = jsonPos(g.points.empty() ? Coord{} : g.points[0], z);
    } else if (g.type == GeometryType::LineString) {
        j["type"] = "LineString";
        j["coordinates"] = jsonRing(g.points, false, z);
    } else {
        j["type"] = "Polygon";
        nlohmann::json rings = nlohmann::json::array();
        rings.push_back(jsonRing(g.points, true, z));
        for (const auto& h : g.holes) rings.push_back(jsonRing(h, true, z));
        j["coordinates"] = rings;
    }
    return j;
}

std::string kmlColor(std::uint32_t argb) {
    char buf[12];
    std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x", (argb >> 24) & 0xFF, argb & 0xFF, (argb >> 8) & 0xFF, (argb >> 16) & 0xFF);
    return buf;
}

std::string kmlCoords(const std::vector<Coord>& pts, bool close) {
    std::string s;
    auto add = [&](const Coord& c) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%.9g,%.9g,%.3f ", c.lon, c.lat, c.alt);
        s += buf;
    };
    for (const Coord& c : pts) add(c);
    if (close && !pts.empty()) add(pts.front());
    return s;
}

}

std::string writeGeoJson(const std::vector<Feature>& fs, ExportResult& r) {
    nlohmann::json fc;
    fc["type"] = "FeatureCollection";
    fc["features"] = nlohmann::json::array();
    bool styleLoss = false;
    for (size_t i = 0; i < fs.size(); ++i) {
        const Feature& f = fs[i];
        bool z = f.altitudeMode != AltitudeMode::ClampToGround;
        nlohmann::json j;
        j["type"] = "Feature";
        if (!f.uid.empty()) j["id"] = f.uid;
        nlohmann::json props = nlohmann::json::object();
        for (const auto& kv : f.attributes) {
            nlohmann::json v = nlohmann::json::parse(kv.second, nullptr, false);
            props[kv.first] = (!v.is_discarded() && !v.is_string() && !kv.second.empty() && kv.second[0] != '"') ? v : nlohmann::json(kv.second);
        }
        if (!f.name.empty()) props["name"] = f.name;
        if (!f.description.empty()) props["description"] = f.description;
        if (!f.type.empty()) props["cot_type"] = f.type;
        if (f.time > 0) props["time"] = formatIsoTime(f.time);
        if (f.start > 0) props["start"] = formatIsoTime(f.start);
        if (f.stale > 0) props["stale"] = formatIsoTime(f.stale);
        if (f.altitudeMode == AltitudeMode::RelativeToGround) props["altitude_mode"] = "relativeToGround";
        if (f.altitudeMode == AltitudeMode::Absolute) props["altitude_mode"] = "absolute";
        props["stroke"] = colorHex(f.style.strokeColor);
        props["stroke-opacity"] = ((f.style.strokeColor >> 24) & 0xFF) / 255.0;
        props["stroke-width"] = f.style.strokeWidth;
        if (f.style.fillColor >> 24) {
            props["fill"] = colorHex(f.style.fillColor);
            props["fill-opacity"] = ((f.style.fillColor >> 24) & 0xFF) / 255.0;
        }
        props["marker-color"] = colorHex(f.style.iconColor);
        styleLoss = styleLoss || f.style.iconImage != nullptr;
        j["properties"] = props;
        if (f.geometries.size() == 1) {
            j["geometry"] = jsonGeometry(f.geometries[0], z);
        } else {
            nlohmann::json gc;
            gc["type"] = "GeometryCollection";
            gc["geometries"] = nlohmann::json::array();
            for (const Geometry& g : f.geometries) gc["geometries"].push_back(jsonGeometry(g, z));
            j["geometry"] = gc;
        }
        fc["features"].push_back(j);
    }
    if (styleLoss) r.lossy.push_back("GeoJSON has no icon images; custom icons dropped");
    return fc.dump(2) + "\n";
}

std::string writeKml(const std::vector<Feature>& fs, ExportResult& r) {
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n<Document>\n";
    bool iconLoss = false;
    for (size_t i = 0; i < fs.size(); ++i) {
        const Feature& f = fs[i];
        std::string sid = "s" + std::to_string(i);
        out += "<Style id=\"" + sid + "\"><IconStyle><color>" + kmlColor(f.style.iconColor) + "</color><scale>" + std::to_string(f.style.iconScale) +
               "</scale></IconStyle><LabelStyle><color>" + kmlColor(f.style.labelColor) + "</color><scale>" +
               std::to_string(f.style.showLabel ? f.style.labelScale : 0.0f) + "</scale></LabelStyle><LineStyle><color>" + kmlColor(f.style.strokeColor) +
               "</color><width>" + std::to_string(f.style.strokeWidth) + "</width></LineStyle><PolyStyle><color>" + kmlColor(f.style.fillColor) +
               "</color></PolyStyle></Style>\n";
        iconLoss = iconLoss || f.style.iconImage != nullptr;
        out += "<Placemark" + (f.uid.empty() ? std::string() : " id=\"" + xmlEscape(f.uid) + "\"") + "><name>" + xmlEscape(f.name) + "</name>";
        if (!f.description.empty()) out += "<description>" + xmlEscape(f.description) + "</description>";
        if (!f.visible) out += "<visibility>0</visibility>";
        out += "<styleUrl>#" + sid + "</styleUrl>";
        if (f.time > 0) out += "<TimeStamp><when>" + formatIsoTime(f.time) + "</when></TimeStamp>";
        if (f.start > 0 || f.stale > 0) {
            out += "<TimeSpan>";
            if (f.start > 0) out += "<begin>" + formatIsoTime(f.start) + "</begin>";
            if (f.stale > 0) out += "<end>" + formatIsoTime(f.stale) + "</end>";
            out += "</TimeSpan>";
        }
        if (!f.attributes.empty() || !f.type.empty()) {
            out += "<ExtendedData>";
            if (!f.type.empty()) out += "<Data name=\"cot_type\"><value>" + xmlEscape(f.type) + "</value></Data>";
            for (const auto& kv : f.attributes) out += "<Data name=\"" + xmlEscape(kv.first) + "\"><value>" + xmlEscape(kv.second) + "</value></Data>";
            out += "</ExtendedData>";
        }
        std::string mode = f.altitudeMode == AltitudeMode::Absolute ? "absolute" : f.altitudeMode == AltitudeMode::RelativeToGround ? "relativeToGround" : "clampToGround";
        std::string am = "<altitudeMode>" + mode + "</altitudeMode>";
        std::string ex = f.extrude ? "<extrude>1</extrude>" : "";
        if (f.geometries.size() > 1) out += "<MultiGeometry>";
        for (const Geometry& g : f.geometries) {
            if (g.type == GeometryType::Point) out += "<Point>" + ex + am + "<coordinates>" + kmlCoords({g.points.empty() ? Coord{} : g.points[0]}, false) + "</coordinates></Point>";
            else if (g.type == GeometryType::LineString) out += "<LineString>" + ex + am + "<coordinates>" + kmlCoords(g.points, false) + "</coordinates></LineString>";
            else {
                out += "<Polygon>" + ex + am + "<outerBoundaryIs><LinearRing><coordinates>" + kmlCoords(g.points, true) +
                       "</coordinates></LinearRing></outerBoundaryIs>";
                for (const auto& h : g.holes)
                    out += "<innerBoundaryIs><LinearRing><coordinates>" + kmlCoords(h, true) + "</coordinates></LinearRing></innerBoundaryIs>";
                out += "</Polygon>";
            }
        }
        if (f.geometries.size() > 1) out += "</MultiGeometry>";
        out += "</Placemark>\n";
    }
    out += "</Document>\n</kml>\n";
    if (iconLoss) r.lossy.push_back("embedded icon images are not written to plain KML; export KMZ to keep them");
    return out;
}

std::string writeGpx(const std::vector<Feature>& fs, ExportResult& r) {
    std::string wpt, trk;
    bool polyLoss = false, attrLoss = false;
    auto pt = [](const char* tag, const Coord& c) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "<%s lat=\"%.9f\" lon=\"%.9f\"><ele>%.3f</ele>", tag, c.lat, c.lon, c.alt);
        return std::string(buf);
    };
    for (const Feature& f : fs) {
        attrLoss = attrLoss || !f.attributes.empty();
        std::string common = "<name>" + xmlEscape(f.name) + "</name>";
        if (!f.description.empty()) common += "<desc>" + xmlEscape(f.description) + "</desc>";
        if (!f.type.empty()) common += "<type>" + xmlEscape(f.type) + "</type>";
        std::vector<const Geometry*> lines;
        for (const Geometry& g : f.geometries) {
            if (g.type == GeometryType::Point) {
                wpt += pt("wpt", g.points.empty() ? Coord{} : g.points[0]);
                if (f.time > 0) wpt += "<time>" + formatIsoTime(f.time) + "</time>";
                wpt += common + "</wpt>\n";
            } else {
                if (g.type == GeometryType::Polygon) polyLoss = true;
                lines.push_back(&g);
            }
        }
        if (!lines.empty()) {
            trk += "<trk>" + common;
            for (const Geometry* g : lines) {
                trk += "<trkseg>";
                std::vector<Coord> p = g->points;
                if (g->type == GeometryType::Polygon && !p.empty()) p.push_back(p.front());
                for (const Coord& c : p) trk += pt("trkpt", c) + "</trkpt>";
                trk += "</trkseg>";
            }
            trk += "</trk>\n";
        }
    }
    if (polyLoss) r.lossy.push_back("GPX has no polygons; polygon outlines written as closed tracks and holes dropped");
    if (attrLoss) r.lossy.push_back("GPX has no generic attributes; attributes dropped");
    r.lossy.push_back("GPX has no styles");
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gpx version=\"1.1\" creator=\"OpenTAKEngine\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n" +
           wpt + trk + "</gpx>\n";
}

}
