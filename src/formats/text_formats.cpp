#include "formats/adapters.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>

namespace otk::adapters {

namespace {

std::string lowerCopy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

struct WktReader {
    const std::string& s;
    size_t i = 0;
    bool z = false;

    void ws() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }
    bool eat(char c) {
        ws();
        if (i < s.size() && s[i] == c) {
            ++i;
            return true;
        }
        return false;
    }
    std::string word() {
        ws();
        size_t b = i;
        while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
        return lowerCopy(s.substr(b, i - b));
    }
    bool number(double& v) {
        ws();
        const char* begin = s.c_str() + i;
        char* end = nullptr;
        v = std::strtod(begin, &end);
        if (end == begin) return false;
        i += static_cast<size_t>(end - begin);
        return true;
    }
    bool coord(Coord& c) {
        double a = 0, b = 0, h = 0;
        if (!number(a) || !number(b)) return false;
        size_t save = i;
        ws();
        if (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' || s[i] == '.')) {
            if (!number(h)) i = save;
            double m;
            size_t save2 = i;
            ws();
            if (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '.')) {
                if (!number(m)) i = save2;
            }
        }
        c = {b, a, h};
        return true;
    }
    bool coordList(std::vector<Coord>& out) {
        if (!eat('(')) return false;
        do {
            bool paren = eat('(');
            Coord c;
            if (!coord(c)) return false;
            out.push_back(c);
            if (paren && !eat(')')) return false;
        } while (eat(','));
        return eat(')');
    }
    bool ringList(std::vector<std::vector<Coord>>& out) {
        if (!eat('(')) return false;
        do {
            std::vector<Coord> ring;
            if (!coordList(ring)) return false;
            if (ring.size() > 1 && ring.front().lat == ring.back().lat && ring.front().lon == ring.back().lon) ring.pop_back();
            out.push_back(std::move(ring));
        } while (eat(','));
        return eat(')');
    }
    bool geometry(std::vector<Geometry>& out) {
        std::string kind = word();
        size_t afterKind = i;
        std::string mod = word();
        if (mod == "empty") return true;
        if (mod != "z" && mod != "m" && mod != "zm") i = afterKind;
        ws();
        if (s.compare(i, 5, "EMPTY") == 0 || s.compare(i, 5, "empty") == 0) {
            i += 5;
            return true;
        }
        if (kind == "point") {
            if (!eat('(')) return false;
            Coord c;
            if (!coord(c) || !eat(')')) return false;
            out.push_back({GeometryType::Point, {c}, {}});
            return true;
        }
        if (kind == "linestring") {
            Geometry g{GeometryType::LineString, {}, {}};
            if (!coordList(g.points)) return false;
            out.push_back(std::move(g));
            return true;
        }
        if (kind == "polygon") {
            std::vector<std::vector<Coord>> rings;
            if (!ringList(rings) || rings.empty()) return false;
            Geometry g{GeometryType::Polygon, rings[0], {}};
            for (size_t k = 1; k < rings.size(); ++k) g.holes.push_back(rings[k]);
            out.push_back(std::move(g));
            return true;
        }
        if (kind == "multipoint") {
            std::vector<Coord> pts;
            if (!coordList(pts)) return false;
            for (const Coord& c : pts) out.push_back({GeometryType::Point, {c}, {}});
            return true;
        }
        if (kind == "multilinestring") {
            std::vector<std::vector<Coord>> lines;
            if (!eat('(')) return false;
            do {
                std::vector<Coord> l;
                if (!coordList(l)) return false;
                out.push_back({GeometryType::LineString, l, {}});
            } while (eat(','));
            return eat(')');
        }
        if (kind == "multipolygon") {
            if (!eat('(')) return false;
            do {
                std::vector<std::vector<Coord>> rings;
                if (!ringList(rings) || rings.empty()) return false;
                Geometry g{GeometryType::Polygon, rings[0], {}};
                for (size_t k = 1; k < rings.size(); ++k) g.holes.push_back(rings[k]);
                out.push_back(std::move(g));
            } while (eat(','));
            return eat(')');
        }
        if (kind == "geometrycollection") {
            if (!eat('(')) return false;
            do {
                if (!geometry(out)) return false;
            } while (eat(','));
            return eat(')');
        }
        return false;
    }
};

std::string num(double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    return buf;
}

std::string wktCoords(const std::vector<Coord>& pts, bool closeRing, bool z) {
    std::string out = "(";
    std::vector<Coord> p = pts;
    if (closeRing && !p.empty()) p.push_back(p.front());
    for (size_t k = 0; k < p.size(); ++k) {
        if (k) out += ", ";
        out += num(p[k].lon) + " " + num(p[k].lat);
        if (z) out += " " + num(p[k].alt);
    }
    return out + ")";
}

std::vector<std::string> splitCsv(const std::string& line, char delim) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (size_t k = 0; k < line.size(); ++k) {
        char c = line[k];
        if (quoted) {
            if (c == '"' && k + 1 < line.size() && line[k + 1] == '"') {
                cur += '"';
                ++k;
            } else if (c == '"') {
                quoted = false;
            } else {
                cur += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == delim) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> csvRecords(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : text) {
        if (c == '"') quoted = !quoted;
        if ((c == '\n') && !quoted) {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        if (cur.back() == '\r') cur.pop_back();
        out.push_back(cur);
    }
    return out;
}

std::string csvField(const std::string& v) {
    if (v.find_first_of(",\"\n\r") == std::string::npos) return v;
    std::string out = "\"";
    for (char c : v) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    return out + "\"";
}

std::string localName(const char* name) {
    const char* colon = std::strrchr(name, ':');
    return colon ? colon + 1 : name;
}

bool latLonOrder(const std::string& srs) {
    std::string s = lowerCopy(srs);
    if (s.empty()) return false;
    if (s.find("crs84") != std::string::npos) return false;
    if (s.find("epsg.xml#4326") != std::string::npos) return false;
    return s.find("4326") != std::string::npos;
}

std::vector<Coord> gmlCoords(const pugi::xml_node& n, bool latFirst, int dim) {
    std::vector<Coord> out;
    std::string ln = localName(n.name());
    if (ln == "coordinates") {
        std::istringstream is(n.text().as_string());
        std::string tuple;
        while (is >> tuple) {
            double a = 0, b = 0, c = 0;
            int k = std::sscanf(tuple.c_str(), "%lf,%lf,%lf", &a, &b, &c);
            if (k >= 2) out.push_back(latFirst ? Coord{a, b, k == 3 ? c : 0.0} : Coord{b, a, k == 3 ? c : 0.0});
        }
        return out;
    }
    std::istringstream is(n.text().as_string());
    std::vector<double> v;
    double d;
    while (is >> d) v.push_back(d);
    int step = std::max(dim, 2);
    for (size_t k = 0; k + static_cast<size_t>(step) - 1 < v.size(); k += static_cast<size_t>(step)) {
        double a = v[k], b = v[k + 1], h = step >= 3 ? v[k + 2] : 0.0;
        out.push_back(latFirst ? Coord{a, b, h} : Coord{b, a, h});
    }
    return out;
}

std::vector<Coord> gmlRing(const pugi::xml_node& ring, bool latFirst, int dim) {
    std::vector<Coord> pts;
    for (auto c : ring.children()) {
        std::string ln = localName(c.name());
        if (ln == "posList" || ln == "coordinates") {
            int d = c.attribute("srsDimension").as_int(dim);
            auto p = gmlCoords(c, latFirst, d);
            pts.insert(pts.end(), p.begin(), p.end());
        } else if (ln == "pos") {
            auto p = gmlCoords(c, latFirst, c.attribute("srsDimension").as_int(dim));
            pts.insert(pts.end(), p.begin(), p.end());
        }
    }
    if (pts.size() > 1 && pts.front().lat == pts.back().lat && pts.front().lon == pts.back().lon) pts.pop_back();
    return pts;
}

void gmlGeometry(const pugi::xml_node& g, const std::string& inheritedSrs, std::vector<Geometry>& out, std::vector<std::string>& warnings) {
    std::string srs = g.attribute("srsName").as_string(inheritedSrs.c_str());
    int dim = g.attribute("srsDimension").as_int(2);
    std::string ls = lowerCopy(srs);
    if (!srs.empty() && ls.find("4326") == std::string::npos && ls.find("crs84") == std::string::npos && ls.find("4979") == std::string::npos)
        warnings.push_back("GML geometry in " + srs + " was read as WGS84; build with GDAL to reproject other CRSs");
    bool latFirst = latLonOrder(srs);
    std::string ln = localName(g.name());
    if (ln == "Point") {
        for (auto c : g.children()) {
            std::string cl = localName(c.name());
            if (cl == "pos" || cl == "coordinates") {
                auto p = gmlCoords(c, latFirst, c.attribute("srsDimension").as_int(dim));
                if (!p.empty()) out.push_back({GeometryType::Point, {p[0]}, {}});
            }
        }
    } else if (ln == "LineString" || ln == "Curve") {
        std::vector<Coord> pts;
        for (auto c : g.select_nodes(".//*")) {
            std::string cl = localName(c.node().name());
            if (cl == "posList" || cl == "coordinates" || cl == "pos") {
                auto p = gmlCoords(c.node(), latFirst, c.node().attribute("srsDimension").as_int(dim));
                pts.insert(pts.end(), p.begin(), p.end());
            }
        }
        if (pts.size() >= 2) out.push_back({GeometryType::LineString, pts, {}});
    } else if (ln == "Polygon" || ln == "Surface") {
        Geometry poly{GeometryType::Polygon, {}, {}};
        for (auto c : g.select_nodes(".//*")) {
            std::string cl = localName(c.node().name());
            if (cl != "LinearRing") continue;
            std::string parent = localName(c.node().parent().name());
            auto ring = gmlRing(c.node(), latFirst, dim);
            if (ring.size() < 3) continue;
            if ((parent == "exterior" || parent == "outerBoundaryIs") && poly.points.empty()) poly.points = ring;
            else poly.holes.push_back(ring);
        }
        if (!poly.points.empty()) out.push_back(std::move(poly));
    } else {
        for (auto c : g.children())
            if (c.type() == pugi::node_element) gmlGeometry(c, srs, out, warnings);
    }
}

bool isGeometryElement(const std::string& ln) {
    static const std::set<std::string> names{"Point", "LineString", "Curve", "Polygon", "Surface", "MultiPoint", "MultiLineString",
                                             "MultiCurve", "MultiPolygon", "MultiSurface", "MultiGeometry"};
    return names.count(ln) > 0;
}

}

bool parseWkt(const std::string& text, std::vector<Geometry>& out) {
    WktReader r{text};
    size_t semi = text.find(';');
    if (semi != std::string::npos && lowerCopy(text.substr(0, 5)) == "srid=") r.i = semi + 1;
    std::vector<Geometry> g;
    if (!r.geometry(g)) return false;
    out.insert(out.end(), g.begin(), g.end());
    return true;
}

std::string writeWkt(const std::vector<Geometry>& geometries) {
    bool z = false;
    for (const Geometry& g : geometries)
        for (const Coord& c : g.points) z = z || c.alt != 0;
    auto one = [&](const Geometry& g) -> std::string {
        std::string zs = z ? " Z " : " ";
        if (g.type == GeometryType::Point) return "POINT" + zs + wktCoords({g.points.empty() ? Coord{} : g.points[0]}, false, z);
        if (g.type == GeometryType::LineString) return "LINESTRING" + zs + wktCoords(g.points, false, z);
        std::string out = "POLYGON" + zs + "(" + wktCoords(g.points, true, z);
        for (const auto& h : g.holes) out += ", " + wktCoords(h, true, z);
        return out + ")";
    };
    if (geometries.empty()) return "GEOMETRYCOLLECTION EMPTY";
    if (geometries.size() == 1) return one(geometries[0]);
    bool same = std::all_of(geometries.begin(), geometries.end(), [&](const Geometry& g) { return g.type == geometries[0].type; });
    std::string zs = z ? " Z " : " ";
    if (same && geometries[0].type == GeometryType::Point) {
        std::string out = "MULTIPOINT" + zs + "(";
        for (size_t k = 0; k < geometries.size(); ++k) out += (k ? ", " : "") + wktCoords({geometries[k].points[0]}, false, z);
        return out + ")";
    }
    if (same && geometries[0].type == GeometryType::LineString) {
        std::string out = "MULTILINESTRING" + zs + "(";
        for (size_t k = 0; k < geometries.size(); ++k) out += (k ? ", " : "") + wktCoords(geometries[k].points, false, z);
        return out + ")";
    }
    if (same && geometries[0].type == GeometryType::Polygon) {
        std::string out = "MULTIPOLYGON" + zs + "(";
        for (size_t k = 0; k < geometries.size(); ++k) {
            out += (k ? ", (" : "(") + wktCoords(geometries[k].points, true, z);
            for (const auto& h : geometries[k].holes) out += ", " + wktCoords(h, true, z);
            out += ")";
        }
        return out + ")";
    }
    std::string out = "GEOMETRYCOLLECTION (";
    for (size_t k = 0; k < geometries.size(); ++k) out += (k ? ", " : "") + one(geometries[k]);
    return out + ")";
}

std::vector<Feature> parseWktText(const std::vector<std::uint8_t>& bytes, std::vector<std::string>& warnings) {
    std::vector<Feature> out;
    std::istringstream is(std::string(bytes.begin(), bytes.end()));
    std::string line;
    size_t n = 0;
    while (std::getline(is, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        ++n;
        Feature f;
        if (!parseWkt(line, f.geometries)) {
            warnings.push_back("line " + std::to_string(n) + " is not valid WKT");
            continue;
        }
        f.name = "WKT " + std::to_string(n);
        if (!f.geometries.empty()) out.push_back(std::move(f));
    }
    return out;
}

std::string writeWktText(const std::vector<Feature>& fs, ExportResult& r) {
    std::string out;
    for (const Feature& f : fs) out += writeWkt(f.geometries) + "\n";
    r.lossy.push_back("WKT keeps geometry only; names, attributes, times and styles are dropped");
    return out;
}

std::vector<Feature> parseCsv(const std::vector<std::uint8_t>& bytes, std::vector<std::string>& warnings) {
    std::vector<Feature> out;
    std::string text(bytes.begin(), bytes.end());
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF) text = text.substr(3);
    std::vector<std::string> rows = csvRecords(text);
    if (rows.empty()) return out;
    char delim = ',';
    size_t best = 0;
    for (char d : {',', ';', '\t', '|'}) {
        size_t c = static_cast<size_t>(std::count(rows[0].begin(), rows[0].end(), d));
        if (c > best) {
            best = c;
            delim = d;
        }
    }
    std::vector<std::string> header = splitCsv(rows[0], delim);
    int latCol = -1, lonCol = -1, altCol = -1, wktCol = -1, nameCol = -1, descCol = -1, uidCol = -1, typeCol = -1, timeCol = -1;
    for (int k = 0; k < static_cast<int>(header.size()); ++k) {
        std::string h = lowerCopy(trim(header[static_cast<size_t>(k)]));
        if (latCol < 0 && (h == "lat" || h == "latitude" || h == "y" || h == "lat_dd" || h == "point_y")) latCol = k;
        else if (lonCol < 0 && (h == "lon" || h == "lng" || h == "long" || h == "longitude" || h == "x" || h == "lon_dd" || h == "point_x")) lonCol = k;
        else if (altCol < 0 && (h == "alt" || h == "altitude" || h == "elevation" || h == "hae" || h == "z" || h == "height")) altCol = k;
        else if (wktCol < 0 && (h == "wkt" || h == "geometry" || h == "geom" || h == "the_geom" || h == "shape")) wktCol = k;
        else if (nameCol < 0 && (h == "name" || h == "title" || h == "label" || h == "callsign")) nameCol = k;
        else if (descCol < 0 && (h == "description" || h == "desc" || h == "remarks" || h == "notes")) descCol = k;
        else if (uidCol < 0 && (h == "uid" || h == "id" || h == "guid")) uidCol = k;
        else if (typeCol < 0 && (h == "type" || h == "cot_type" || h == "cottype")) typeCol = k;
        else if (timeCol < 0 && (h == "time" || h == "timestamp" || h == "datetime" || h == "date")) timeCol = k;
    }
    if (wktCol < 0 && (latCol < 0 || lonCol < 0)) {
        warnings.push_back("CSV has no lat/lon or WKT column");
        return out;
    }
    for (size_t row = 1; row < rows.size(); ++row) {
        if (trim(rows[row]).empty()) continue;
        std::vector<std::string> cells = splitCsv(rows[row], delim);
        auto cell = [&](int k) { return k >= 0 && static_cast<size_t>(k) < cells.size() ? trim(cells[static_cast<size_t>(k)]) : std::string(); };
        Feature f;
        if (wktCol >= 0 && !cell(wktCol).empty()) {
            if (!parseWkt(cell(wktCol), f.geometries)) warnings.push_back("row " + std::to_string(row) + " has invalid WKT");
        } else {
            char* e1 = nullptr;
            char* e2 = nullptr;
            std::string la = cell(latCol), lo = cell(lonCol);
            double lat = std::strtod(la.c_str(), &e1), lon = std::strtod(lo.c_str(), &e2);
            if (la.empty() || lo.empty() || *e1 || *e2 || std::abs(lat) > 90 || std::abs(lon) > 180) {
                warnings.push_back("row " + std::to_string(row) + " has no valid coordinates");
                continue;
            }
            double alt = altCol >= 0 ? std::atof(cell(altCol).c_str()) : 0.0;
            f.geometries.push_back({GeometryType::Point, {{lat, lon, alt}}, {}});
            if (altCol >= 0) f.altitudeMode = AltitudeMode::Absolute;
        }
        if (f.geometries.empty()) continue;
        f.name = cell(nameCol);
        f.description = cell(descCol);
        f.uid = cell(uidCol);
        f.type = cell(typeCol);
        if (timeCol >= 0) f.time = parseIsoTime(cell(timeCol));
        for (int k = 0; k < static_cast<int>(header.size()); ++k) {
            if (k == latCol || k == lonCol || k == altCol || k == wktCol || k == nameCol || k == descCol || k == uidCol || k == typeCol || k == timeCol)
                continue;
            setAttribute(f, trim(header[static_cast<size_t>(k)]), cell(k));
        }
        if (f.name.empty()) f.name = "Row " + std::to_string(row);
        out.push_back(std::move(f));
    }
    return out;
}

std::string writeCsv(const std::vector<Feature>& fs, ExportResult& r) {
    std::vector<std::string> keys;
    std::set<std::string> seen;
    for (const Feature& f : fs)
        for (const auto& kv : f.attributes)
            if (seen.insert(kv.first).second) keys.push_back(kv.first);
    std::string out = "name,description,uid,type,time,wkt";
    for (const auto& k : keys) out += "," + csvField(k);
    out += "\n";
    for (const Feature& f : fs) {
        out += csvField(f.name) + "," + csvField(f.description) + "," + csvField(f.uid) + "," + csvField(f.type) + "," +
               (f.time > 0 ? formatIsoTime(f.time) : std::string()) + "," + csvField(writeWkt(f.geometries));
        for (const auto& k : keys) {
            const std::string* v = findAttribute(f, k);
            out += "," + (v ? csvField(*v) : std::string());
        }
        out += "\n";
    }
    r.lossy.push_back("CSV drops styles, start/stale times and altitude mode");
    return out;
}

std::vector<Feature> parseGml(const std::vector<std::uint8_t>& bytes, std::vector<std::string>& warnings) {
    std::vector<Feature> out;
    pugi::xml_document doc;
    if (!doc.load_buffer(bytes.data(), bytes.size())) {
        warnings.push_back("not well-formed GML");
        return out;
    }
    pugi::xml_node root = doc.document_element();
    std::string rootSrs;
    for (auto env : root.select_nodes(".//*[local-name()='Envelope']")) {
        rootSrs = env.node().attribute("srsName").as_string();
        break;
    }
    std::vector<pugi::xml_node> members;
    for (auto m : root.select_nodes(".//*[local-name()='featureMember' or local-name()='member' or local-name()='featureMembers']")) {
        pugi::xml_node mn = m.node();
        if (localName(mn.name()) == std::string("featureMembers")) {
            for (auto c : mn.children())
                if (c.type() == pugi::node_element) members.push_back(c);
        } else if (mn.first_child().type() == pugi::node_element) {
            members.push_back(mn.first_child());
        }
    }
    if (members.empty() && isGeometryElement(localName(root.name()))) members.push_back(root.parent());
    for (const pugi::xml_node& m : members) {
        Feature f;
        f.uid = m.attribute("gml:id").as_string(m.attribute("fid").as_string());
        for (auto c : m.children()) {
            if (c.type() != pugi::node_element) continue;
            std::string ln = localName(c.name());
            pugi::xml_node geom;
            if (isGeometryElement(ln)) geom = c;
            else
                for (auto gc : c.children())
                    if (gc.type() == pugi::node_element && isGeometryElement(localName(gc.name()))) {
                        geom = gc;
                        break;
                    }
            if (geom) {
                gmlGeometry(geom, rootSrs, f.geometries, warnings);
                continue;
            }
            if (ln == "boundedBy") continue;
            std::string value = c.text().as_string();
            std::string ns = c.name();
            bool own = ns.rfind("otk:", 0) == 0 || ns.rfind("ote:", 0) == 0;
            if (ln == "name" && f.name.empty()) f.name = value;
            else if (ln == "description" && f.description.empty()) f.description = value;
            else if (own && ln == "uid") f.uid = value;
            else if (own && ln == "type") f.type = value;
            else if (own && ln == "time") f.time = parseIsoTime(value);
            else setAttribute(f, ln, value);
        }
        if (f.name.empty()) f.name = !f.uid.empty() ? f.uid : localName(m.name());
        if (!f.geometries.empty()) out.push_back(std::move(f));
        if (out.size() >= 5000000) break;
    }
    if (out.empty()) warnings.push_back("no GML features with geometry found");
    return out;
}

std::string writeGml(const std::vector<Feature>& fs, ExportResult& r) {
    std::string out =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<gml:FeatureCollection xmlns:gml=\"http://www.opengis.net/gml/3.2\" "
        "xmlns:otk=\"https://github.com/grangedevgroup-code/OTKGlobeEngine\" gml:id=\"otk\">\n";
    auto poslist = [](const std::vector<Coord>& pts, bool close) {
        std::string s;
        std::vector<Coord> p = pts;
        if (close && !p.empty()) p.push_back(p.front());
        for (const Coord& c : p) s += (s.empty() ? "" : " ") + num(c.lat) + " " + num(c.lon) + " " + num(c.alt);
        return s;
    };
    const char* srs = " srsName=\"urn:ogc:def:crs:EPSG::4979\" srsDimension=\"3\"";
    for (size_t i = 0; i < fs.size(); ++i) {
        const Feature& f = fs[i];
        std::string id = xmlEscape(uniqueId(f, i));
        out += "<gml:featureMember><otk:Feature gml:id=\"f" + std::to_string(i) + "\">";
        out += "<otk:uid>" + id + "</otk:uid><otk:name>" + xmlEscape(f.name) + "</otk:name>";
        if (!f.description.empty()) out += "<otk:description>" + xmlEscape(f.description) + "</otk:description>";
        if (!f.type.empty()) out += "<otk:type>" + xmlEscape(f.type) + "</otk:type>";
        if (f.time > 0) out += "<otk:time>" + formatIsoTime(f.time) + "</otk:time>";
        for (const auto& kv : f.attributes) {
            std::string key;
            for (char c : kv.first) key += std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? c : '_';
            if (key.empty() || std::isdigit(static_cast<unsigned char>(key[0]))) key = "a_" + key;
            out += "<otk:" + key + ">" + xmlEscape(kv.second) + "</otk:" + key + ">";
        }
        for (size_t g = 0; g < f.geometries.size(); ++g) {
            const Geometry& geo = f.geometries[g];
            std::string gid = "g" + std::to_string(i) + "_" + std::to_string(g);
            out += "<otk:geometry>";
            if (geo.type == GeometryType::Point && !geo.points.empty())
                out += "<gml:Point gml:id=\"" + gid + "\"" + srs + "><gml:pos>" + poslist({geo.points[0]}, false) + "</gml:pos></gml:Point>";
            else if (geo.type == GeometryType::LineString)
                out += "<gml:LineString gml:id=\"" + gid + "\"" + srs + "><gml:posList>" + poslist(geo.points, false) + "</gml:posList></gml:LineString>";
            else if (geo.type == GeometryType::Polygon) {
                out += "<gml:Polygon gml:id=\"" + gid + "\"" + srs + "><gml:exterior><gml:LinearRing><gml:posList>" + poslist(geo.points, true) +
                       "</gml:posList></gml:LinearRing></gml:exterior>";
                for (const auto& h : geo.holes)
                    out += "<gml:interior><gml:LinearRing><gml:posList>" + poslist(h, true) + "</gml:posList></gml:LinearRing></gml:interior>";
                out += "</gml:Polygon>";
            }
            out += "</otk:geometry>";
        }
        out += "</otk:Feature></gml:featureMember>\n";
    }
    out += "</gml:FeatureCollection>\n";
    r.lossy.push_back("GML drops styles and start/stale times; attribute names are sanitised to XML element names");
    return out;
}

}
