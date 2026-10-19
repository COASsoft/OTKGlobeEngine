#include "ote/features.hpp"

#include <json.hpp>
#include <miniz.h>
#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <sstream>

namespace ote {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::uint8_t> readAll(const std::filesystem::path& p) {
    std::vector<std::uint8_t> out;
#if defined(_WIN32)
    FILE* f = _wfopen(p.wstring().c_str(), L"rb");
#else
    FILE* f = std::fopen(p.string().c_str(), "rb");
#endif
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        out.resize(static_cast<size_t>(n));
        if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
    }
    std::fclose(f);
    return out;
}

std::uint32_t hexColor(const std::string& s, std::uint8_t alpha = 255) {
    std::string h = s;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    if (h.size() == 3) h = std::string{h[0], h[0], h[1], h[1], h[2], h[2]};
    if (h.size() != 6) return 0xFFFFFFFF;
    unsigned v = 0;
    std::sscanf(h.c_str(), "%x", &v);
    return (static_cast<std::uint32_t>(alpha) << 24) | v;
}

std::uint32_t kmlColor(const std::string& s) {
    std::string t;
    for (char c : s)
        if (!std::isspace(static_cast<unsigned char>(c))) t += c;
    if (t.size() != 8) return 0xFFFFFFFF;
    unsigned v = 0;
    std::sscanf(t.c_str(), "%x", &v);
    std::uint32_t a = (v >> 24) & 0xFF, b = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, r = v & 0xFF;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

std::vector<Coord> kmlCoords(const char* text) {
    std::vector<Coord> out;
    std::string s = text ? text : "";
    for (char& c : s)
        if (c == '\n' || c == '\t' || c == '\r') c = ' ';
    std::istringstream in(s);
    std::string tok;
    while (in >> tok) {
        Coord c;
        int n = std::sscanf(tok.c_str(), "%lf,%lf,%lf", &c.lon, &c.lat, &c.alt);
        if (n >= 2) out.push_back(c);
    }
    return out;
}

struct KmlStyle {
    Style style;
    bool hasLine = false, hasPoly = false, hasIcon = false, hasLabel = false, fill = true, outline = true;
};

void readKmlStyle(const pugi::xml_node& n, KmlStyle& k) {
    if (auto ls = n.child("LineStyle")) {
        k.hasLine = true;
        if (auto c = ls.child("color")) k.style.strokeColor = kmlColor(c.text().as_string());
        if (auto w = ls.child("width")) k.style.strokeWidth = w.text().as_float(2.0f);
    }
    if (auto ps = n.child("PolyStyle")) {
        k.hasPoly = true;
        if (auto c = ps.child("color")) k.style.fillColor = kmlColor(c.text().as_string());
        if (auto f = ps.child("fill")) k.fill = f.text().as_bool(true);
        if (auto o = ps.child("outline")) k.outline = o.text().as_bool(true);
    }
    if (auto is = n.child("IconStyle")) {
        k.hasIcon = true;
        if (auto c = is.child("color")) k.style.iconColor = kmlColor(c.text().as_string());
        if (auto s = is.child("scale")) k.style.iconScale = s.text().as_float(1.0f);
    }
    if (auto lb = n.child("LabelStyle")) {
        k.hasLabel = true;
        if (auto c = lb.child("color")) k.style.labelColor = kmlColor(c.text().as_string());
        if (auto s = lb.child("scale")) {
            k.style.labelScale = s.text().as_float(1.0f);
            if (k.style.labelScale <= 0) k.style.showLabel = false;
        }
    }
}

void applyKmlStyle(const KmlStyle& k, Style& s) {
    if (k.hasLine) {
        s.strokeColor = k.style.strokeColor;
        s.strokeWidth = k.style.strokeWidth;
    }
    if (k.hasPoly) s.fillColor = k.fill ? k.style.fillColor : 0;
    if (k.hasPoly && !k.outline) s.strokeColor = 0;
    if (k.hasIcon) {
        s.iconColor = k.style.iconColor;
        s.iconScale = k.style.iconScale;
    }
    if (k.hasLabel) {
        s.labelColor = k.style.labelColor;
        s.labelScale = k.style.labelScale;
        s.showLabel = k.style.showLabel;
    }
}

AltitudeMode kmlAltitude(const pugi::xml_node& g) {
    std::string m = g.child_value("altitudeMode");
    if (m.empty()) m = g.child_value("gx:altitudeMode");
    if (m == "absolute") return AltitudeMode::Absolute;
    if (m == "relativeToGround" || m == "relativeToSeaFloor") return AltitudeMode::RelativeToGround;
    return AltitudeMode::ClampToGround;
}

void kmlGeometry(const pugi::xml_node& g, Feature& f) {
    std::string name = g.name();
    if (name == "MultiGeometry") {
        for (auto c : g.children()) kmlGeometry(c, f);
        return;
    }
    Geometry geom;
    if (name == "Point") {
        geom.type = GeometryType::Point;
        geom.points = kmlCoords(g.child_value("coordinates"));
    } else if (name == "LineString" || name == "LinearRing") {
        geom.type = GeometryType::LineString;
        geom.points = kmlCoords(g.child_value("coordinates"));
    } else if (name == "Polygon") {
        geom.type = GeometryType::Polygon;
        geom.points = kmlCoords(g.child("outerBoundaryIs").child("LinearRing").child_value("coordinates"));
        for (auto in : g.children("innerBoundaryIs"))
            geom.holes.push_back(kmlCoords(in.child("LinearRing").child_value("coordinates")));
    } else if (name == "gx:Track") {
        geom.type = GeometryType::LineString;
        for (auto c : g.children("gx:coord")) {
            Coord p;
            if (std::sscanf(c.text().as_string(), "%lf %lf %lf", &p.lon, &p.lat, &p.alt) >= 2) geom.points.push_back(p);
        }
    } else if (name == "gx:MultiTrack") {
        for (auto c : g.children("gx:Track")) kmlGeometry(c, f);
        return;
    } else {
        return;
    }
    if (geom.points.empty()) return;
    f.altitudeMode = kmlAltitude(g);
    f.extrude = g.child("extrude").text().as_bool(false);
    f.geometries.push_back(std::move(geom));
}

std::vector<Feature> parseKmlText(const char* text, size_t size) {
    std::vector<Feature> out;
    pugi::xml_document doc;
    if (!doc.load_buffer(text, size)) return out;
    std::map<std::string, KmlStyle> styles;
    std::map<std::string, std::string> maps;
    for (auto n : doc.select_nodes("//Style")) {
        std::string id = n.node().attribute("id").as_string();
        if (id.empty()) continue;
        readKmlStyle(n.node(), styles["#" + id]);
    }
    for (auto n : doc.select_nodes("//StyleMap")) {
        std::string id = n.node().attribute("id").as_string();
        for (auto p : n.node().children("Pair"))
            if (std::string(p.child_value("key")) == "normal") maps["#" + id] = p.child_value("styleUrl");
    }
    for (auto n : doc.select_nodes("//Placemark")) {
        pugi::xml_node pm = n.node();
        Feature f;
        f.name = pm.child_value("name");
        f.description = pm.child_value("description");
        f.visible = pm.child("visibility").text().as_bool(true);
        std::string url = pm.child_value("styleUrl");
        if (maps.count(url)) url = maps[url];
        if (styles.count(url)) applyKmlStyle(styles[url], f.style);
        if (auto s = pm.child("Style")) {
            KmlStyle k;
            readKmlStyle(s, k);
            applyKmlStyle(k, f.style);
        }
        for (auto c : pm.children()) kmlGeometry(c, f);
        if (!f.geometries.empty()) out.push_back(std::move(f));
    }
    return out;
}

std::vector<Feature> parseKmz(const std::vector<std::uint8_t>& bytes) {
    std::vector<Feature> out;
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) return out;
    int index = mz_zip_reader_locate_file(&zip, "doc.kml", nullptr, 0);
    if (index < 0) {
        for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
            char name[512];
            mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
            if (lower(name).size() > 4 && lower(name).substr(lower(name).size() - 4) == ".kml") {
                index = static_cast<int>(i);
                break;
            }
        }
    }
    if (index >= 0) {
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(index), &size, 0);
        if (data) {
            out = parseKmlText(static_cast<const char*>(data), size);
            mz_free(data);
        }
    }
    mz_zip_reader_end(&zip);
    return out;
}

Coord jsonCoord(const nlohmann::json& a) {
    Coord c;
    if (a.is_array() && a.size() >= 2) {
        c.lon = a[0].get<double>();
        c.lat = a[1].get<double>();
        if (a.size() >= 3 && a[2].is_number()) c.alt = a[2].get<double>();
    }
    return c;
}

std::vector<Coord> jsonCoords(const nlohmann::json& a) {
    std::vector<Coord> out;
    if (a.is_array())
        for (const auto& p : a) out.push_back(jsonCoord(p));
    return out;
}

void jsonGeometry(const nlohmann::json& g, Feature& f) {
    if (!g.is_object()) return;
    std::string type = g.value("type", "");
    const auto& c = g.contains("coordinates") ? g["coordinates"] : nlohmann::json();
    auto polygon = [&](const nlohmann::json& rings) {
        if (!rings.is_array() || rings.empty()) return;
        Geometry geom;
        geom.type = GeometryType::Polygon;
        geom.points = jsonCoords(rings[0]);
        for (size_t i = 1; i < rings.size(); ++i) geom.holes.push_back(jsonCoords(rings[i]));
        f.geometries.push_back(std::move(geom));
    };
    if (type == "Point") {
        f.geometries.push_back({GeometryType::Point, {jsonCoord(c)}, {}});
    } else if (type == "MultiPoint") {
        for (const auto& p : c) f.geometries.push_back({GeometryType::Point, {jsonCoord(p)}, {}});
    } else if (type == "LineString") {
        f.geometries.push_back({GeometryType::LineString, jsonCoords(c), {}});
    } else if (type == "MultiLineString") {
        for (const auto& l : c) f.geometries.push_back({GeometryType::LineString, jsonCoords(l), {}});
    } else if (type == "Polygon") {
        polygon(c);
    } else if (type == "MultiPolygon") {
        for (const auto& p : c) polygon(p);
    } else if (type == "GeometryCollection" && g.contains("geometries")) {
        for (const auto& s : g["geometries"]) jsonGeometry(s, f);
    }
}

void jsonFeature(const nlohmann::json& j, std::vector<Feature>& out) {
    Feature f;
    if (j.contains("properties") && j["properties"].is_object()) {
        const auto& p = j["properties"];
        for (const char* k : {"name", "title", "NAME", "Name", "label"})
            if (p.contains(k) && p[k].is_string()) {
                f.name = p[k].get<std::string>();
                break;
            }
        if (p.contains("description") && p["description"].is_string()) f.description = p["description"].get<std::string>();
        auto opacity = [&](const char* k) {
            return static_cast<std::uint8_t>(std::clamp(p.contains(k) && p[k].is_number() ? p[k].get<double>() : 1.0, 0.0, 1.0) * 255);
        };
        if (p.contains("stroke") && p["stroke"].is_string()) f.style.strokeColor = hexColor(p["stroke"].get<std::string>(), opacity("stroke-opacity"));
        if (p.contains("stroke-width") && p["stroke-width"].is_number()) f.style.strokeWidth = p["stroke-width"].get<float>();
        if (p.contains("fill") && p["fill"].is_string()) f.style.fillColor = hexColor(p["fill"].get<std::string>(), opacity("fill-opacity"));
        else f.style.fillColor = 0x40FFFFFF;
        if (p.contains("marker-color") && p["marker-color"].is_string()) f.style.iconColor = hexColor(p["marker-color"].get<std::string>());
    }
    if (j.contains("geometry")) jsonGeometry(j["geometry"], f);
    if (!f.geometries.empty()) out.push_back(std::move(f));
}

std::vector<Feature> parseGeoJson(const std::vector<std::uint8_t>& bytes) {
    std::vector<Feature> out;
    nlohmann::json j = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (j.is_discarded()) return out;
    std::string type = j.value("type", "");
    if (type == "FeatureCollection" && j.contains("features")) {
        for (const auto& f : j["features"]) jsonFeature(f, out);
    } else if (type == "Feature") {
        jsonFeature(j, out);
    } else {
        Feature f;
        jsonGeometry(j, f);
        if (!f.geometries.empty()) out.push_back(std::move(f));
    }
    return out;
}

std::vector<Feature> parseGpx(const std::vector<std::uint8_t>& bytes) {
    std::vector<Feature> out;
    pugi::xml_document doc;
    if (!doc.load_buffer(bytes.data(), bytes.size())) return out;
    pugi::xml_node gpx = doc.child("gpx");
    auto pt = [](const pugi::xml_node& n) {
        return Coord{n.attribute("lat").as_double(), n.attribute("lon").as_double(), n.child("ele").text().as_double(0)};
    };
    for (auto w : gpx.children("wpt")) {
        Feature f;
        f.name = w.child_value("name");
        f.geometries.push_back({GeometryType::Point, {pt(w)}, {}});
        out.push_back(std::move(f));
    }
    for (auto r : gpx.children("rte")) {
        Feature f;
        f.name = r.child_value("name");
        Geometry g{GeometryType::LineString, {}, {}};
        for (auto p : r.children("rtept")) g.points.push_back(pt(p));
        f.geometries.push_back(std::move(g));
        f.style.strokeColor = 0xFFFF4040;
        f.style.strokeWidth = 3;
        out.push_back(std::move(f));
    }
    for (auto t : gpx.children("trk")) {
        Feature f;
        f.name = t.child_value("name");
        for (auto s : t.children("trkseg")) {
            Geometry g{GeometryType::LineString, {}, {}};
            for (auto p : s.children("trkpt")) g.points.push_back(pt(p));
            if (!g.points.empty()) f.geometries.push_back(std::move(g));
        }
        f.style.strokeColor = 0xFFFFD000;
        f.style.strokeWidth = 3;
        if (!f.geometries.empty()) out.push_back(std::move(f));
    }
    return out;
}

std::int32_t be32(const std::uint8_t* p) { return static_cast<std::int32_t>((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]); }
std::int32_t le32(const std::uint8_t* p) { return static_cast<std::int32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24)); }
double le64(const std::uint8_t* p) {
    double d;
    std::memcpy(&d, p, 8);
    return d;
}

std::vector<std::string> dbfNames(const std::filesystem::path& path) {
    std::vector<std::string> out;
    std::vector<std::uint8_t> b = readAll(path);
    if (b.size() < 32) return out;
    std::uint32_t count = static_cast<std::uint32_t>(le32(&b[4]));
    std::uint16_t header = static_cast<std::uint16_t>(b[8] | (b[9] << 8)), record = static_cast<std::uint16_t>(b[10] | (b[11] << 8));
    int field = -1, offset = 1, cursor = 1;
    for (size_t p = 32; p + 32 <= b.size() && b[p] != 0x0D; p += 32) {
        std::string name(reinterpret_cast<const char*>(&b[p]), strnlen(reinterpret_cast<const char*>(&b[p]), 11));
        int len = b[p + 16];
        std::string l = lower(name);
        if (field < 0 && b[p + 11] == 'C' && (l == "name" || l == "label" || l == "title" || l == "id")) {
            field = 1;
            offset = cursor;
            field = len;
        }
        cursor += len;
    }
    if (field < 0) return out;
    for (std::uint32_t i = 0; i < count; ++i) {
        size_t at = header + static_cast<size_t>(i) * record + offset;
        if (at + field > b.size()) break;
        std::string v(reinterpret_cast<const char*>(&b[at]), field);
        v.erase(v.find_last_not_of(' ') + 1);
        out.push_back(v);
    }
    return out;
}

std::vector<Feature> parseShapefile(const std::filesystem::path& path) {
    std::vector<Feature> out;
    std::vector<std::uint8_t> b = readAll(path);
    if (b.size() < 100 || be32(&b[0]) != 9994) return out;
    std::filesystem::path dbf = path;
    dbf.replace_extension(".dbf");
    std::vector<std::string> names = dbfNames(dbf);
    size_t p = 100;
    size_t rec = 0;
    while (p + 12 <= b.size()) {
        std::int32_t len = be32(&b[p + 4]) * 2;
        const std::uint8_t* c = &b[p + 8];
        if (p + 8 + static_cast<size_t>(len) > b.size()) break;
        int type = le32(c);
        Feature f;
        if (rec < names.size()) f.name = names[rec];
        int base = type % 10;
        if (type != 0 && (base == 1)) {
            f.geometries.push_back({GeometryType::Point, {Coord{le64(c + 12), le64(c + 4), 0}}, {}});
        } else if (type != 0 && base == 8) {
            int n = le32(c + 36);
            for (int i = 0; i < n; ++i)
                f.geometries.push_back({GeometryType::Point, {Coord{le64(c + 40 + i * 16 + 8), le64(c + 40 + i * 16), 0}}, {}});
        } else if (base == 3 || base == 5) {
            int parts = le32(c + 36), points = le32(c + 40);
            const std::uint8_t* pp = c + 44;
            const std::uint8_t* pts = pp + parts * 4;
            for (int i = 0; i < parts; ++i) {
                int s = le32(pp + i * 4), e = i + 1 < parts ? le32(pp + (i + 1) * 4) : points;
                std::vector<Coord> ring;
                double area = 0;
                for (int k = s; k < e; ++k) {
                    ring.push_back({le64(pts + k * 16 + 8), le64(pts + k * 16), 0});
                    if (k > s) area += (ring[k - s].lon - ring[k - s - 1].lon) * (ring[k - s].lat + ring[k - s - 1].lat);
                }
                if (base == 3) {
                    f.geometries.push_back({GeometryType::LineString, ring, {}});
                } else if (area >= 0 || f.geometries.empty()) {
                    f.geometries.push_back({GeometryType::Polygon, ring, {}});
                } else {
                    f.geometries.back().holes.push_back(ring);
                }
            }
            if (base == 5) f.style.fillColor = 0x40FFFFFF;
        }
        if (!f.geometries.empty()) out.push_back(std::move(f));
        p += 8 + len;
        ++rec;
    }
    return out;
}

}

std::vector<Feature> parseFeatures(const std::string& path) {
    std::filesystem::path p = std::filesystem::u8path(path);
    std::string ext = lower(p.extension().string());
    if (ext == ".shp") return parseShapefile(p);
    std::vector<std::uint8_t> bytes = readAll(p);
    if (bytes.empty()) return {};
    if (ext == ".kmz") return parseKmz(bytes);
    if (ext == ".kml") return parseKmlText(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (ext == ".gpx") return parseGpx(bytes);
    if (ext == ".json" || ext == ".geojson") return parseGeoJson(bytes);
    return {};
}

}
