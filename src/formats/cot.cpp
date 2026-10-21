#include "formats/adapters.hpp"
#include "otk/navigation.hpp"

#include <pugixml.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace otk::adapters {

namespace {

constexpr double kUnknown = 9999999.0;

std::uint32_t cotColor(const pugi::xml_node& n, const char* attr) {
    long long v = n.attribute(attr).as_llong(0);
    return static_cast<std::uint32_t>(v & 0xFFFFFFFFll);
}

std::string nodeXml(const pugi::xml_node& n) {
    std::ostringstream os;
    n.print(os, "", pugi::format_raw);
    return os.str();
}

std::vector<Coord> linkPoints(const pugi::xml_node& detail) {
    std::vector<Coord> pts;
    for (auto link : detail.children("link")) {
        const char* p = link.attribute("point").as_string("");
        double lat = 0, lon = 0, alt = 0;
        int n = std::sscanf(p, "%lf,%lf,%lf", &lat, &lon, &alt);
        if (n >= 2) pts.push_back({lat, lon, n == 3 ? alt : 0.0});
    }
    return pts;
}

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

}

std::vector<Feature> parseCot(const std::vector<std::uint8_t>& bytes, std::vector<std::string>& warnings) {
    std::vector<Feature> out;
    pugi::xml_document doc;
    if (!doc.load_buffer(bytes.data(), bytes.size())) {
        warnings.push_back("not well-formed CoT XML");
        return out;
    }
    for (auto sel : doc.select_nodes("//event")) {
        pugi::xml_node ev = sel.node();
        pugi::xml_node point = ev.child("point");
        pugi::xml_node detail = ev.child("detail");
        Feature f;
        f.uid = ev.attribute("uid").as_string();
        f.type = ev.attribute("type").as_string();
        f.time = parseIsoTime(ev.attribute("time").as_string());
        f.start = parseIsoTime(ev.attribute("start").as_string());
        f.stale = parseIsoTime(ev.attribute("stale").as_string());
        if (ev.attribute("how")) setAttribute(f, "cot:how", ev.attribute("how").as_string());
        if (point.attribute("ce")) setAttribute(f, "cot:ce", point.attribute("ce").as_string());
        if (point.attribute("le")) setAttribute(f, "cot:le", point.attribute("le").as_string());
        double hae = point.attribute("hae").as_double(kUnknown);
        Coord c{point.attribute("lat").as_double(), point.attribute("lon").as_double(), hae >= kUnknown ? 0.0 : hae};
        f.altitudeMode = hae >= kUnknown ? AltitudeMode::ClampToGround : AltitudeMode::Absolute;
        if (detail) {
            if (auto contact = detail.child("contact")) f.name = contact.attribute("callsign").as_string();
            f.description = detail.child_value("remarks");
            if (auto s = detail.child("strokeColor")) f.style.strokeColor = cotColor(s, "value");
            if (auto s = detail.child("strokeWeight")) f.style.strokeWidth = s.attribute("value").as_float(2.0f);
            if (auto s = detail.child("fillColor")) f.style.fillColor = cotColor(s, "value");
            if (auto s = detail.child("color")) {
                if (s.attribute("argb")) f.style.iconColor = cotColor(s, "argb");
                else if (s.attribute("value")) {
                    f.style.iconColor = cotColor(s, "value");
                    if (!detail.child("strokeColor")) f.style.strokeColor = f.style.iconColor;
                }
            }
            if (auto s = detail.child("strokeStyle")) {
                std::string style = s.attribute("value").as_string();
                if (style == "dashed") {
                    f.style.dash = 12;
                    f.style.dashGap = 8;
                } else if (style == "dotted") {
                    f.style.dash = 2;
                    f.style.dashGap = 6;
                }
            }
            if (auto s = detail.child("usericon"))
                if (auto image = iconsetImage(s.attribute("iconsetpath").as_string())) f.style.iconImage = image;
            if (auto s = detail.child("labels_on")) f.style.showLabel = s.attribute("value").as_bool(true);
            if (auto attrs = detail.child("__otk_attributes") ? detail.child("__otk_attributes") : detail.child("__ote_attributes"))
                for (auto a : attrs.children("a")) setAttribute(f, a.attribute("k").as_string(), a.attribute("v").as_string());
            setAttribute(f, "cot:detail", nodeXml(detail));
        }
        if (f.name.empty()) f.name = f.uid;
        std::vector<Coord> pts = detail ? linkPoints(detail) : std::vector<Coord>{};
        pugi::xml_node ellipse = detail ? detail.child("shape").child("ellipse") : pugi::xml_node();
        const double range = detail ? detail.child("range").attribute("value").as_double(-1.0) : -1.0;
        if (f.type.rfind("u-rb", 0) == 0 && range > 0 && detail.child("bearing")) {
            Coord end = geodesicDirect(c, detail.child("bearing").attribute("value").as_double(), range);
            end.alt = c.alt;
            f.geometries.push_back({GeometryType::LineString, {c, end}, {}});
        } else if (ellipse) {
            double major = ellipse.attribute("major").as_double(), minor = ellipse.attribute("minor").as_double();
            double angle = ellipse.attribute("angle").as_double() * 3.14159265358979323846 / 180.0;
            Geometry g{GeometryType::Polygon, {}, {}};
            for (int i = 0; i < 72; ++i) {
                double t = i * 2.0 * 3.14159265358979323846 / 72.0;
                double x = major * std::cos(t), y = minor * std::sin(t);
                double e = x * std::cos(angle) - y * std::sin(angle), n = x * std::sin(angle) + y * std::cos(angle);
                g.points.push_back({c.lat + n / 111320.0, c.lon + e / (111320.0 * std::max(std::cos(c.lat * 3.14159265358979323846 / 180.0), 1e-6)), c.alt});
            }
            f.geometries.push_back(std::move(g));
        } else if (pts.size() >= 2) {
            bool closed = f.type.rfind("u-d-r", 0) == 0 ||
                          (pts.size() >= 4 && std::abs(pts.front().lat - pts.back().lat) < 1e-9 && std::abs(pts.front().lon - pts.back().lon) < 1e-9) ||
                          (detail && detail.child("closed") && detail.child("closed").attribute("value").as_bool());
            if (closed && pts.size() >= 4 && std::abs(pts.front().lat - pts.back().lat) < 1e-9 && std::abs(pts.front().lon - pts.back().lon) < 1e-9)
                pts.pop_back();
            f.geometries.push_back({closed ? GeometryType::Polygon : GeometryType::LineString, pts, {}});
            if (closed && f.style.fillColor == 0) f.style.fillColor = 0x40FFFFFF;
        } else {
            f.geometries.push_back({GeometryType::Point, {c}, {}});
        }
        out.push_back(std::move(f));
    }
    if (out.empty()) warnings.push_back("no CoT events found");
    return out;
}

std::string writeCotEvent(const Feature& f, size_t index, ExportResult& r) {
    if (f.geometries.empty()) return {};
    const Geometry& g = f.geometries.front();
    if (f.geometries.size() > 1) r.lossy.push_back("CoT holds one geometry per event; extra parts of '" + f.name + "' dropped");
    if (!g.holes.empty()) r.lossy.push_back("CoT shapes have no holes; holes of '" + f.name + "' dropped");
    Coord anchor = g.points.empty() ? Coord{} : g.points.front();
    if (g.type != GeometryType::Point && !g.points.empty()) {
        anchor = {};
        for (const Coord& c : g.points) {
            anchor.lat += c.lat;
            anchor.lon += c.lon;
            anchor.alt += c.alt;
        }
        anchor.lat /= static_cast<double>(g.points.size());
        anchor.lon /= static_cast<double>(g.points.size());
        anchor.alt /= static_cast<double>(g.points.size());
    }
    double now = nowSeconds();
    double time = f.time > 0 ? f.time : now;
    double start = f.start > 0 ? f.start : time;
    double stale = f.stale > 0 ? f.stale : start + 86400.0;
    std::string type = !f.type.empty() ? f.type : g.type == GeometryType::Point ? "b-m-p-s-m" : "u-d-f";
    const std::string* how = findAttribute(f, "cot:how");
    const std::string* ce = findAttribute(f, "cot:ce");
    const std::string* le = findAttribute(f, "cot:le");
    char pointBuf[256];
    double hae = f.altitudeMode == AltitudeMode::Absolute ? anchor.alt : kUnknown;
    std::snprintf(pointBuf, sizeof(pointBuf), "<point lat=\"%.8f\" lon=\"%.8f\" hae=\"%.3f\" ce=\"%s\" le=\"%s\"/>", anchor.lat, anchor.lon, hae,
                  ce ? xmlEscape(*ce).c_str() : "9999999.0", le ? xmlEscape(*le).c_str() : "9999999.0");
    std::string xml = "<event version=\"2.0\" uid=\"" + xmlEscape(uniqueId(f, index)) + "\" type=\"" + xmlEscape(type) + "\" how=\"" +
                      xmlEscape(how ? *how : std::string("h-e")) + "\" time=\"" + formatIsoTime(time) + "\" start=\"" + formatIsoTime(start) +
                      "\" stale=\"" + formatIsoTime(stale) + "\">" + pointBuf;
    const std::string* raw = findAttribute(f, "cot:detail");
    if (raw && !raw->empty()) {
        xml += *raw;
    } else {
        xml += "<detail><contact callsign=\"" + xmlEscape(f.name) + "\"/>";
        if (!f.description.empty()) xml += "<remarks>" + xmlEscape(f.description) + "</remarks>";
        if (g.type != GeometryType::Point) {
            std::vector<Coord> pts = g.points;
            if (g.type == GeometryType::Polygon && !pts.empty()) pts.push_back(pts.front());
            for (const Coord& c : pts) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "<link point=\"%.8f,%.8f,%.3f\"/>", c.lat, c.lon, c.alt);
                xml += buf;
            }
            xml += "<strokeColor value=\"" + std::to_string(static_cast<std::int32_t>(f.style.strokeColor)) + "\"/>";
            xml += "<strokeWeight value=\"" + std::to_string(f.style.strokeWidth) + "\"/>";
            if (g.type == GeometryType::Polygon) xml += "<fillColor value=\"" + std::to_string(static_cast<std::int32_t>(f.style.fillColor)) + "\"/>";
        } else {
            xml += "<color argb=\"" + std::to_string(static_cast<std::int32_t>(f.style.iconColor)) + "\"/>";
        }
        xml += std::string("<labels_on value=\"") + (f.style.showLabel ? "true" : "false") + "\"/>";
        std::string extra;
        for (const auto& kv : f.attributes)
            if (kv.first.rfind("cot:", 0) != 0) extra += "<a k=\"" + xmlEscape(kv.first) + "\" v=\"" + xmlEscape(kv.second) + "\"/>";
        if (!extra.empty()) xml += "<__otk_attributes>" + extra + "</__otk_attributes>";
        xml += "</detail>";
    }
    xml += "</event>";
    return xml;
}

std::string writeCot(const std::vector<Feature>& fs, ExportResult& r) {
    std::string body;
    size_t count = 0;
    for (size_t i = 0; i < fs.size(); ++i) {
        std::string e = writeCotEvent(fs[i], i, r);
        if (e.empty()) continue;
        body += e;
        body += "\n";
        ++count;
    }
    std::string head = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
    if (count == 1) return head + body;
    r.lossy.push_back("several CoT events written under an <events> root; TAK clients expect one event per message or a data package");
    return head + "<events>\n" + body + "</events>\n";
}

}
