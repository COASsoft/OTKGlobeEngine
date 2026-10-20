#include "ote/features.hpp"

#include "gdal/gdal_sources.hpp"

#include <json.hpp>
#include "ote/image.hpp"
#include <miniz.h>
#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
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

using IconLoader = std::function<std::shared_ptr<const Image>(const std::string&)>;

void readKmlStyle(const pugi::xml_node& n, KmlStyle& k, const IconLoader& icons) {
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
        bool hasColor = static_cast<bool>(is.child("color"));
        if (hasColor) k.style.iconColor = kmlColor(is.child("color").text().as_string());
        if (auto s = is.child("scale")) k.style.iconScale = s.text().as_float(1.0f);
        std::string href = is.child("Icon").child_value("href");
        if (!href.empty() && icons) k.style.iconImage = icons(href);
        if (k.style.iconImage && !hasColor) k.style.iconColor = 0xFFFFFFFF;
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
        s.iconImage = k.style.iconImage;
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

std::vector<Coord> openRing(std::vector<Coord> r) {
    if (r.size() >= 2 && std::abs(r.front().lat - r.back().lat) < 1e-12 && std::abs(r.front().lon - r.back().lon) < 1e-12) r.pop_back();
    return r;
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
        geom.points = openRing(kmlCoords(g.child("outerBoundaryIs").child("LinearRing").child_value("coordinates")));
        for (auto in : g.children("innerBoundaryIs"))
            geom.holes.push_back(openRing(kmlCoords(in.child("LinearRing").child_value("coordinates"))));
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

using Resolver = std::function<std::vector<std::uint8_t>(const std::string&)>;

void kmlGroundOverlays(const pugi::xml_document& doc, const Resolver& resolve, std::vector<GroundOverlay>& out) {
    for (auto n : doc.select_nodes("//GroundOverlay")) {
        pugi::xml_node g = n.node();
        pugi::xml_node box = g.child("LatLonBox");
        std::string href = g.child("Icon").child_value("href");
        if (!box || href.empty() || !resolve) continue;
        std::vector<std::uint8_t> bytes = resolve(href);
        if (bytes.empty()) continue;
        Image img;
        if (!decodeImage(bytes.data(), bytes.size(), img)) continue;
        GroundOverlay o;
        o.name = g.child_value("name");
        o.north = box.child("north").text().as_double();
        o.south = box.child("south").text().as_double();
        o.east = box.child("east").text().as_double();
        o.west = box.child("west").text().as_double();
        o.width = img.width;
        o.height = img.height;
        o.rgba = std::move(img.rgba);
        std::string color = g.child_value("color");
        if (color.size() == 8) o.opacity = static_cast<float>(std::stoul(color.substr(0, 2), nullptr, 16)) / 255.0f;
        if (o.north > o.south && o.east > o.west) out.push_back(std::move(o));
    }
}

std::vector<Feature> parseKmlText(const char* text, size_t size, const Resolver& resolve = {},
                                  std::vector<GroundOverlay>* overlays = nullptr) {
    std::vector<Feature> out;
    pugi::xml_document doc;
    if (!doc.load_buffer(text, size)) return out;
    if (overlays) kmlGroundOverlays(doc, resolve, *overlays);
    std::map<std::string, KmlStyle> styles;
    std::map<std::string, std::string> maps;
    std::map<std::string, std::shared_ptr<const Image>> iconCache;
    IconLoader icons = [&](const std::string& href) -> std::shared_ptr<const Image> {
        auto it = iconCache.find(href);
        if (it != iconCache.end()) return it->second;
        std::shared_ptr<const Image> img;
        if (resolve) {
            std::vector<std::uint8_t> bytes = resolve(href);
            auto im = std::make_shared<Image>();
            if (decodeImage(bytes.data(), bytes.size(), *im)) img = im;
        }
        iconCache[href] = img;
        return img;
    };
    for (auto n : doc.select_nodes("//Style")) {
        std::string id = n.node().attribute("id").as_string();
        if (id.empty()) continue;
        readKmlStyle(n.node(), styles["#" + id], icons);
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
            readKmlStyle(s, k, icons);
            applyKmlStyle(k, f.style);
        }
        f.uid = pm.attribute("id").as_string();
        if (auto ext = pm.child("ExtendedData")) {
            for (auto d : ext.children("Data")) {
                std::string key = d.attribute("name").as_string();
                if (key == "cot_type") f.type = d.child_value("value");
                else setAttribute(f, key, d.child_value("value"));
            }
            for (auto sd : ext.children("SchemaData"))
                for (auto v : sd.children("SimpleData")) setAttribute(f, v.attribute("name").as_string(), v.text().as_string());
        }
        if (auto ts = pm.child("TimeStamp")) f.time = parseIsoTime(ts.child_value("when"));
        if (auto span = pm.child("TimeSpan")) {
            f.start = parseIsoTime(span.child_value("begin"));
            f.stale = parseIsoTime(span.child_value("end"));
        }
        for (auto c : pm.children()) kmlGeometry(c, f);
        if (!f.geometries.empty()) out.push_back(std::move(f));
    }
    return out;
}

std::vector<Feature> parseKmz(const std::vector<std::uint8_t>& bytes, std::vector<GroundOverlay>* overlays = nullptr) {
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
            Resolver resolve = [&zip](const std::string& href) {
                std::vector<std::uint8_t> r;
                int i = mz_zip_reader_locate_file(&zip, href.c_str(), nullptr, 0);
                if (i < 0) return r;
                size_t n = 0;
                void* p = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(i), &n, 0);
                if (p) {
                    const auto* b = static_cast<const std::uint8_t*>(p);
                    r.assign(b, b + n);
                    mz_free(p);
                }
                return r;
            };
            out = parseKmlText(static_cast<const char*>(data), size, resolve, overlays);
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
        geom.points = openRing(jsonCoords(rings[0]));
        for (size_t i = 1; i < rings.size(); ++i) geom.holes.push_back(openRing(jsonCoords(rings[i])));
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
    if (j.contains("id")) f.uid = j["id"].is_string() ? j["id"].get<std::string>() : j["id"].dump();
    if (j.contains("properties") && j["properties"].is_object()) {
        const auto& p = j["properties"];
        static const char* const known[] = {"name", "description", "cot_type", "time", "start", "stale", "altitude_mode", "stroke",
                                            "stroke-width", "stroke-opacity", "fill", "fill-opacity", "marker-color", "marker-size", "marker-symbol"};
        for (auto it = p.begin(); it != p.end(); ++it) {
            if (std::find(std::begin(known), std::end(known), it.key()) != std::end(known)) continue;
            setAttribute(f, it.key(), it.value().is_string() ? it.value().get<std::string>() : it.value().dump());
        }
        auto str = [&](const char* k) { return p.contains(k) && p[k].is_string() ? p[k].get<std::string>() : std::string(); };
        f.type = str("cot_type");
        f.time = parseIsoTime(str("time"));
        f.start = parseIsoTime(str("start"));
        f.stale = parseIsoTime(str("stale"));
        if (str("altitude_mode") == "absolute") f.altitudeMode = AltitudeMode::Absolute;
        if (str("altitude_mode") == "relativeToGround") f.altitudeMode = AltitudeMode::RelativeToGround;
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
    auto common = [](const pugi::xml_node& n, Feature& f) {
        f.name = n.child_value("name");
        f.description = n.child_value("desc");
        if (n.child("type")) setAttribute(f, "type", n.child_value("type"));
        if (n.child("sym")) setAttribute(f, "sym", n.child_value("sym"));
        if (n.child("cmt")) setAttribute(f, "cmt", n.child_value("cmt"));
    };
    for (auto w : gpx.children("wpt")) {
        Feature f;
        common(w, f);
        f.time = parseIsoTime(w.child_value("time"));
        f.geometries.push_back({GeometryType::Point, {pt(w)}, {}});
        out.push_back(std::move(f));
    }
    for (auto r : gpx.children("rte")) {
        Feature f;
        common(r, f);
        Geometry g{GeometryType::LineString, {}, {}};
        for (auto p : r.children("rtept")) g.points.push_back(pt(p));
        f.geometries.push_back(std::move(g));
        f.style.strokeColor = 0xFFFF4040;
        f.style.strokeWidth = 3;
        out.push_back(std::move(f));
    }
    for (auto t : gpx.children("trk")) {
        Feature f;
        common(t, f);
        for (auto s : t.children("trkseg")) {
            Geometry g{GeometryType::LineString, {}, {}};
            for (auto p : s.children("trkpt")) {
                g.points.push_back(pt(p));
                double when = parseIsoTime(p.child_value("time"));
                if (when > 0) {
                    if (f.start == 0) f.start = when;
                    f.stale = when;
                }
            }
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

struct DbfTable {
    std::vector<std::vector<std::pair<std::string, std::string>>> rows;
    int nameField = -1;
};

DbfTable readDbf(const std::filesystem::path& path) {
    DbfTable out;
    std::vector<std::uint8_t> b = readAll(path);
    if (b.size() < 32) return out;
    std::uint32_t count = static_cast<std::uint32_t>(le32(&b[4]));
    std::uint16_t header = static_cast<std::uint16_t>(b[8] | (b[9] << 8)), record = static_cast<std::uint16_t>(b[10] | (b[11] << 8));
    struct Field {
        std::string name;
        int offset = 0;
        int length = 0;
    };
    std::vector<Field> fields;
    int cursor = 1;
    for (size_t p = 32; p + 32 <= b.size() && b[p] != 0x0D; p += 32) {
        std::string name(reinterpret_cast<const char*>(&b[p]), strnlen(reinterpret_cast<const char*>(&b[p]), 11));
        int len = b[p + 16];
        std::string l = lower(name);
        if (out.nameField < 0 && b[p + 11] == 'C' && (l == "name" || l == "label" || l == "title" || l == "id"))
            out.nameField = static_cast<int>(fields.size());
        fields.push_back({name, cursor, len});
        cursor += len;
    }
    for (std::uint32_t i = 0; i < count && i < 10000000u; ++i) {
        size_t base = header + static_cast<size_t>(i) * record;
        if (base + record > b.size()) break;
        std::vector<std::pair<std::string, std::string>> row;
        for (const Field& f : fields) {
            std::string v(reinterpret_cast<const char*>(&b[base + f.offset]), f.length);
            v.erase(v.find_last_not_of(' ') + 1);
            v.erase(0, std::min(v.find_first_not_of(' '), v.size()));
            row.emplace_back(f.name, v);
        }
        out.rows.push_back(std::move(row));
    }
    return out;
}

std::vector<Feature> parseShapefile(const std::filesystem::path& path) {
    std::vector<Feature> out;
    std::vector<std::uint8_t> b = readAll(path);
    if (b.size() < 100 || be32(&b[0]) != 9994) return out;
    std::filesystem::path dbf = path;
    dbf.replace_extension(".dbf");
    DbfTable table = readDbf(dbf);
    size_t p = 100;
    size_t rec = 0;
    while (p + 12 <= b.size()) {
        std::int32_t len = be32(&b[p + 4]) * 2;
        const std::uint8_t* c = &b[p + 8];
        if (p + 8 + static_cast<size_t>(len) > b.size()) break;
        int type = le32(c);
        Feature f;
        if (rec < table.rows.size()) {
            f.attributes = table.rows[rec];
            if (table.nameField >= 0) f.name = table.rows[rec][static_cast<size_t>(table.nameField)].second;
        }
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

ParsedFile parseBytes(const std::vector<std::uint8_t>& bytes, const std::string& extension, const std::string& baseDirectory) {
    ParsedFile out;
    std::string ext = lower(extension);
    if (!ext.empty() && ext[0] != '.') ext = "." + ext;
    if (ext == ".kmz") {
        out.features = parseKmz(bytes, &out.overlays);
    } else if (ext == ".kml") {
        std::filesystem::path dir = std::filesystem::u8path(baseDirectory);
        Resolver resolve;
        if (!baseDirectory.empty()) resolve = [dir](const std::string& href) { return readAll(dir / std::filesystem::u8path(href)); };
        out.features = parseKmlText(reinterpret_cast<const char*>(bytes.data()), bytes.size(), resolve, &out.overlays);
    } else if (ext == ".gpx") {
        out.features = parseGpx(bytes);
    } else if (ext == ".json" || ext == ".geojson") {
        out.features = parseGeoJson(bytes);
    }
    return out;
}

ParsedFile parseFile(const std::string& path) {
    ParsedFile out;
    std::filesystem::path p = std::filesystem::u8path(path);
    std::string ext = lower(p.extension().string());
    if (ext == ".shp") {
        out.features = parseShapefile(p);
        return out;
    }
    std::vector<std::uint8_t> bytes = readAll(p);
    if (bytes.empty()) {
        out.features = parseWithOgr(path);
    } else if (ext == ".kmz" || ext == ".kml" || ext == ".gpx" || ext == ".json" || ext == ".geojson") {
        std::u8string dir = p.parent_path().u8string();
        out = parseBytes(bytes, ext, std::string(dir.begin(), dir.end()));
    } else {
        out.features = parseWithOgr(path);
    }
    return out;
}

namespace {

std::int64_t daysFromCivil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

}

double parseIsoTime(const std::string& text) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    double sec = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%d", &y, &mo, &d) != 3 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
    size_t t = text.find_first_of("Tt ");
    double offset = 0;
    if (t != std::string::npos) {
        std::sscanf(text.c_str() + t + 1, "%d:%d:%lf", &h, &mi, &sec);
        size_t z = text.find_first_of("Zz+-", t + 1);
        if (z != std::string::npos && (text[z] == '+' || text[z] == '-')) {
            int oh = 0, om = 0;
            if (std::sscanf(text.c_str() + z + 1, "%d:%d", &oh, &om) >= 1) offset = (text[z] == '-' ? -1.0 : 1.0) * (oh * 3600.0 + om * 60.0);
        }
    }
    return static_cast<double>(daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d))) * 86400.0 + h * 3600.0 +
           mi * 60.0 + sec - offset;
}

std::string formatIsoTime(double unixSeconds) {
    std::int64_t total = static_cast<std::int64_t>(std::floor(unixSeconds));
    int millis = static_cast<int>(std::lround((unixSeconds - static_cast<double>(total)) * 1000.0));
    if (millis >= 1000) {
        ++total;
        millis -= 1000;
    }
    std::int64_t days = total >= 0 ? total / 86400 : (total - 86399) / 86400;
    std::int64_t rem = total - days * 86400;
    std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04lld-%02u-%02uT%02d:%02d:%02d.%03dZ", static_cast<long long>(y), m, d, static_cast<int>(rem / 3600),
                  static_cast<int>(rem % 3600 / 60), static_cast<int>(rem % 60), millis);
    return buf;
}

const std::string* findAttribute(const Feature& feature, const std::string& key) {
    for (const auto& kv : feature.attributes)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

void setAttribute(Feature& feature, const std::string& key, const std::string& value) {
    if (key.empty()) return;
    for (auto& kv : feature.attributes)
        if (kv.first == key) {
            kv.second = value;
            return;
        }
    feature.attributes.emplace_back(key, value);
}

std::vector<Feature> parseFeatures(const std::string& path) { return parseFile(path).features; }

}
