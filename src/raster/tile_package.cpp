#include "raster/tile_package.hpp"

#include "db/sqlite.hpp"

#include "ote/image.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace ote {

namespace {

bool endsWith(std::string s, const std::string& suffix) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool tableExists(db::Database& db, const char* name) {
    auto st = db.prepare("SELECT 1 FROM sqlite_master WHERE type='table' AND name=?");
    if (!st.valid()) return false;
    st.bind(1, std::string(name));
    return st.step();
}

std::string fmt(double v) {
    char b[64];
    std::snprintf(b, sizeof(b), "%.10g", v);
    return b;
}

void matrixExtent(const TileMatrix& m, double& minX, double& minY, double& maxX, double& maxY) {
    minX = m.originX;
    maxY = m.originY;
    maxX = m.originX;
    minY = m.originY;
    for (int z = 0; z < static_cast<int>(m.levels.size()); ++z) {
        if (!m.hasLevel(z)) continue;
        maxX = std::max(maxX, m.originX + m.levels[z].matrixWidth * m.spanX(z));
        minY = std::min(minY, m.originY - m.levels[z].matrixHeight * m.spanY(z));
    }
    if (m.srid == 3857) {
        minY = std::max(minY, -m.originY);
        maxX = std::min(maxX, -m.originX);
    } else {
        minY = std::max(minY, -90.0);
        maxX = std::min(maxX, 180.0);
    }
}

}

std::string tileFormat(const std::vector<std::uint8_t>& d) {
    if (d.size() >= 4 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return "png";
    if (d.size() >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return "jpg";
    if (d.size() >= 12 && d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' && d[8] == 'W' && d[9] == 'E' && d[10] == 'B' && d[11] == 'P')
        return "webp";
    if (d.size() >= 6 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F') return "gif";
    return "pbf";
}

bool sameTiling(const TileMatrix& a, const TileMatrix& b) {
    if (a.srid != b.srid) return false;
    double tol = a.srid == 4326 ? 1e-7 : 1e-2;
    if (std::abs(a.originX - b.originX) > tol || std::abs(a.originY - b.originY) > tol) return false;
    for (int z = 0; z < static_cast<int>(std::min(a.levels.size(), b.levels.size())); ++z) {
        if (!a.hasLevel(z) || !b.hasLevel(z)) continue;
        const TileLevel& la = a.levels[z];
        const TileLevel& lb = b.levels[z];
        if (la.tileWidth != lb.tileWidth || la.tileHeight != lb.tileHeight) return false;
        if (std::abs(la.resolutionX - lb.resolutionX) > la.resolutionX * 1e-6) return false;
    }
    return true;
}

struct TilePackageWriter::Impl {
    db::Database db;
    db::Statement insert;
    bool mbtiles = false;
    bool flipY = false;
    TileMatrix matrix;
    std::string name;
    std::string format;
    double west = 180, south = 90, east = -180, north = -90;
    int pending = 0;
};

TilePackageWriter::TilePackageWriter() : impl_(std::make_unique<Impl>()) {}

TilePackageWriter::~TilePackageWriter() { close(); }

bool TilePackageWriter::open(const std::string& path, const TileMatrix& matrix, const std::string& name, std::string& error) {
    Impl& d = *impl_;
    d.matrix = matrix;
    d.name = name;
    if (d.name.empty()) {
        size_t slash = path.find_last_of("/\\");
        d.name = path.substr(slash == std::string::npos ? 0 : slash + 1);
        d.name = d.name.substr(0, d.name.find_last_of('.'));
    }
    d.mbtiles = endsWith(path, ".mbtiles");
    if (!d.mbtiles && !endsWith(path, ".gpkg")) {
        error = "unsupported package extension (use .mbtiles or .gpkg)";
        return false;
    }
    if (d.mbtiles) {
        TileMatrix wm = TileMatrix::webMercator(0, static_cast<int>(matrix.levels.size()) - 1,
                                                matrix.levels.empty() ? 256 : matrix.levels.back().tileWidth);
        if (matrix.srid != 3857 || !sameTiling(matrix, wm)) {
            error = "MBTiles holds only Web Mercator tiles; export this layer as .gpkg";
            return false;
        }
    }
    bool existed = std::filesystem::exists(std::filesystem::u8path(path));
    if (!d.db.open(path, false)) {
        error = "cannot open " + path;
        return false;
    }
    d.db.exec("PRAGMA journal_mode=DELETE");
    if (d.mbtiles) {
        if (existed && tableExists(d.db, "tiles") && !tableExists(d.db, "metadata")) {
            error = path + " is not an MBTiles file";
            return false;
        }
        d.db.exec("CREATE TABLE IF NOT EXISTS metadata (name TEXT, value TEXT)");
        d.db.exec("CREATE UNIQUE INDEX IF NOT EXISTS metadata_name ON metadata (name)");
        d.db.exec("CREATE TABLE IF NOT EXISTS tiles (zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB)");
        d.db.exec("CREATE UNIQUE INDEX IF NOT EXISTS tile_index ON tiles (zoom_level, tile_column, tile_row)");
        auto b = d.db.prepare("SELECT value FROM metadata WHERE name='bounds'");
        if (b.valid() && b.step()) {
            double w, s, e, n;
            if (std::sscanf(b.text(0).c_str(), "%lf,%lf,%lf,%lf", &w, &s, &e, &n) == 4) extend(w, s, e, n);
        }
        d.insert = d.db.prepare("INSERT OR REPLACE INTO tiles (zoom_level, tile_column, tile_row, tile_data) VALUES (?,?,?,?)");
        d.flipY = true;
    } else {
        const std::string table = "imagery";
        if (existed && tableExists(d.db, "gpkg_contents")) {
            auto st = d.db.prepare("SELECT srs_id FROM gpkg_tile_matrix_set WHERE table_name='imagery'");
            if (!st.valid() || !st.step()) {
                error = path + " has no 'imagery' tile table to append to";
                return false;
            }
            if (st.integer(0) != matrix.srid) {
                error = path + " uses a different tile matrix";
                return false;
            }
        } else {
            d.db.exec("PRAGMA application_id=1196444487");
            d.db.exec("PRAGMA user_version=10200");
            d.db.exec("CREATE TABLE gpkg_spatial_ref_sys (srs_name TEXT NOT NULL, srs_id INTEGER PRIMARY KEY, organization TEXT NOT NULL, "
                      "organization_coordsys_id INTEGER NOT NULL, definition TEXT NOT NULL, description TEXT)");
            d.db.exec("INSERT INTO gpkg_spatial_ref_sys VALUES ('Undefined cartesian SRS', -1, 'NONE', -1, 'undefined', NULL)");
            d.db.exec("INSERT INTO gpkg_spatial_ref_sys VALUES ('Undefined geographic SRS', 0, 'NONE', 0, 'undefined', NULL)");
            d.db.exec("INSERT INTO gpkg_spatial_ref_sys VALUES ('WGS 84 geodetic', 4326, 'EPSG', 4326, 'GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\","
                      "SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]', NULL)");
            if (matrix.srid == 3857)
                d.db.exec("INSERT INTO gpkg_spatial_ref_sys VALUES ('WGS 84 / Pseudo-Mercator', 3857, 'EPSG', 3857, 'PROJCS[\"WGS 84 / "
                          "Pseudo-Mercator\",GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],"
                          "PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]],PROJECTION[\"Mercator_1SP\"],"
                          "PARAMETER[\"central_meridian\",0],PARAMETER[\"scale_factor\",1],PARAMETER[\"false_easting\",0],"
                          "PARAMETER[\"false_northing\",0],UNIT[\"metre\",1]]', NULL)");
            d.db.exec("CREATE TABLE gpkg_contents (table_name TEXT NOT NULL PRIMARY KEY, data_type TEXT NOT NULL, identifier TEXT UNIQUE, "
                      "description TEXT DEFAULT '', last_change DATETIME NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')), "
                      "min_x DOUBLE, min_y DOUBLE, max_x DOUBLE, max_y DOUBLE, srs_id INTEGER)");
            d.db.exec("CREATE TABLE gpkg_tile_matrix_set (table_name TEXT NOT NULL PRIMARY KEY, srs_id INTEGER NOT NULL, "
                      "min_x DOUBLE NOT NULL, min_y DOUBLE NOT NULL, max_x DOUBLE NOT NULL, max_y DOUBLE NOT NULL)");
            d.db.exec("CREATE TABLE gpkg_tile_matrix (table_name TEXT NOT NULL, zoom_level INTEGER NOT NULL, matrix_width INTEGER NOT NULL, "
                      "matrix_height INTEGER NOT NULL, tile_width INTEGER NOT NULL, tile_height INTEGER NOT NULL, pixel_x_size DOUBLE NOT NULL, "
                      "pixel_y_size DOUBLE NOT NULL, CONSTRAINT pk_ttm PRIMARY KEY (table_name, zoom_level))");
            d.db.exec("CREATE TABLE imagery (id INTEGER PRIMARY KEY AUTOINCREMENT, zoom_level INTEGER NOT NULL, tile_column INTEGER NOT NULL, "
                      "tile_row INTEGER NOT NULL, tile_data BLOB NOT NULL, UNIQUE (zoom_level, tile_column, tile_row))");
            double minX, minY, maxX, maxY;
            matrixExtent(matrix, minX, minY, maxX, maxY);
            std::string srs = std::to_string(matrix.srid);
            d.db.exec(("INSERT INTO gpkg_tile_matrix_set VALUES ('" + table + "', " + srs + ", " + fmt(minX) + ", " + fmt(minY) + ", " +
                       fmt(maxX) + ", " + fmt(maxY) + ")")
                          .c_str());
            auto c = d.db.prepare("INSERT INTO gpkg_contents (table_name, data_type, identifier, min_x, min_y, max_x, max_y, srs_id) "
                                  "VALUES ('imagery', 'tiles', ?, ?, ?, ?, ?, ?)");
            c.bind(1, d.name);
            c.bind(2, minX);
            c.bind(3, minY);
            c.bind(4, maxX);
            c.bind(5, maxY);
            c.bind(6, matrix.srid);
            c.exec();
            auto lv = d.db.prepare("INSERT INTO gpkg_tile_matrix VALUES ('imagery', ?, ?, ?, ?, ?, ?, ?)");
            for (int z = 0; z < static_cast<int>(matrix.levels.size()); ++z) {
                if (!matrix.hasLevel(z)) continue;
                const TileLevel& l = matrix.levels[z];
                lv.reset();
                lv.bind(1, z);
                lv.bind(2, l.matrixWidth);
                lv.bind(3, l.matrixHeight);
                lv.bind(4, l.tileWidth);
                lv.bind(5, l.tileHeight);
                lv.bind(6, l.resolutionX);
                lv.bind(7, l.resolutionY);
                lv.exec();
            }
        }
        d.insert = d.db.prepare("INSERT OR REPLACE INTO imagery (zoom_level, tile_column, tile_row, tile_data) VALUES (?,?,?,?)");
    }
    if (!d.insert.valid()) {
        error = "cannot write tiles to " + path;
        return false;
    }
    d.db.exec("BEGIN");
    return true;
}

bool TilePackageWriter::put(int z, int x, int y, const std::vector<std::uint8_t>& data) {
    Impl& d = *impl_;
    if (!d.insert.valid() || data.empty()) return false;
    if (d.format.empty()) d.format = tileFormat(data);
    d.insert.reset();
    d.insert.bind(1, z);
    d.insert.bind(2, x);
    d.insert.bind(3, d.flipY ? (1 << z) - 1 - y : y);
    d.insert.bind(4, data);
    bool ok = d.insert.exec();
    d.insert.reset();
    if (++d.pending >= 2000) {
        d.db.exec("COMMIT");
        d.db.exec("BEGIN");
        d.pending = 0;
    }
    return ok;
}

void TilePackageWriter::extend(double west, double south, double east, double north) {
    Impl& d = *impl_;
    d.west = std::min(d.west, west);
    d.south = std::min(d.south, south);
    d.east = std::max(d.east, east);
    d.north = std::max(d.north, north);
}

bool TilePackageWriter::close() {
    Impl& d = *impl_;
    if (!d.db.isOpen()) return false;
    d.insert = {};
    d.db.exec("COMMIT");
    if (d.mbtiles) {
        int minZ = 0, maxZ = 0;
        auto zs = d.db.prepare("SELECT MIN(zoom_level), MAX(zoom_level) FROM tiles");
        if (zs.valid() && zs.step() && !zs.isNull(0)) {
            minZ = zs.integer(0);
            maxZ = zs.integer(1);
        }
        zs = {};
        auto md = d.db.prepare("INSERT OR REPLACE INTO metadata (name, value) VALUES (?, ?)");
        auto set = [&](const std::string& k, const std::string& v) {
            md.reset();
            md.bind(1, k);
            md.bind(2, v);
            md.exec();
        };
        set("name", d.name);
        if (!d.format.empty()) set("format", d.format);
        set("type", "baselayer");
        set("version", "1.0");
        set("minzoom", std::to_string(minZ));
        set("maxzoom", std::to_string(maxZ));
        if (d.west <= d.east && d.south <= d.north)
            set("bounds", fmt(d.west) + "," + fmt(d.south) + "," + fmt(d.east) + "," + fmt(d.north));
    }
    d.db.close();
    return true;
}

bool readTilePackage(const std::string& path, TileMatrix& matrix, const TileVisitor& visit, std::string& error) {
    db::Database db;
    if (!db.open(path, true)) {
        error = "cannot open " + path;
        return false;
    }
    if (tableExists(db, "gpkg_contents")) {
        std::string table;
        {
            auto st = db.prepare("SELECT table_name FROM gpkg_contents WHERE data_type='tiles' ORDER BY table_name LIMIT 1");
            if (st.valid() && st.step()) table = st.text(0);
        }
        if (table.empty()) {
            error = path + " has no tile table";
            return false;
        }
        auto ms = db.prepare("SELECT s.organization_coordsys_id, m.min_x, m.max_y FROM gpkg_tile_matrix_set m "
                             "LEFT JOIN gpkg_spatial_ref_sys s ON s.srs_id = m.srs_id WHERE m.table_name=?");
        ms.bind(1, table);
        if (!ms.valid() || !ms.step()) {
            error = path + " has no tile matrix set";
            return false;
        }
        int srid = ms.integer(0);
        matrix = TileMatrix{};
        matrix.srid = srid == 900913 || srid == 3395 ? 3857 : srid;
        matrix.originX = ms.real(1);
        matrix.originY = ms.real(2);
        matrix.minZoom = 1 << 30;
        matrix.maxZoom = -1;
        auto lv = db.prepare("SELECT zoom_level, matrix_width, matrix_height, tile_width, tile_height, pixel_x_size, pixel_y_size "
                             "FROM gpkg_tile_matrix WHERE table_name=? ORDER BY zoom_level");
        lv.bind(1, table);
        while (lv.valid() && lv.step()) {
            int z = lv.integer(0);
            if (z < 0 || z > 30) continue;
            if (static_cast<int>(matrix.levels.size()) <= z) matrix.levels.resize(z + 1);
            TileLevel& l = matrix.levels[z];
            l.valid = true;
            l.matrixWidth = lv.integer(1);
            l.matrixHeight = lv.integer(2);
            l.tileWidth = lv.integer(3);
            l.tileHeight = lv.integer(4);
            l.resolutionX = lv.real(5);
            l.resolutionY = lv.real(6);
            matrix.minZoom = std::min(matrix.minZoom, z);
            matrix.maxZoom = std::max(matrix.maxZoom, z);
        }
        auto rows = db.prepare(("SELECT zoom_level, tile_column, tile_row, tile_data FROM \"" + table + "\"").c_str());
        while (rows.valid() && rows.step())
            if (!visit(rows.integer(0), rows.integer(1), rows.integer(2), rows.blob(3))) break;
        return true;
    }
    if (tableExists(db, "tiles") && tableExists(db, "metadata")) {
        int minZ = 0, maxZ = 0, size = 256;
        {
            auto st = db.prepare("SELECT MIN(zoom_level), MAX(zoom_level) FROM tiles");
            if (st.valid() && st.step() && !st.isNull(0)) {
                minZ = st.integer(0);
                maxZ = st.integer(1);
            }
            auto first = db.prepare("SELECT tile_data FROM tiles LIMIT 1");
            if (first.valid() && first.step()) {
                std::vector<std::uint8_t> b = first.blob(0);
                int w = 0, h = 0;
                if (!b.empty() && imageSize(b.data(), b.size(), w, h) && w == h && w > 0) size = w;
            }
        }
        matrix = TileMatrix::webMercator(minZ, maxZ, size);
        auto rows = db.prepare("SELECT zoom_level, tile_column, tile_row, tile_data FROM tiles");
        while (rows.valid() && rows.step()) {
            int z = rows.integer(0);
            if (z < 0 || z > 30) continue;
            if (!visit(z, rows.integer(1), (1 << z) - 1 - rows.integer(2), rows.blob(3))) break;
        }
        return true;
    }
    if (tableExists(db, "tiles")) {
        auto probe = db.prepare("SELECT key, tile FROM tiles LIMIT 1");
        if (!probe.valid()) {
            error = path + " is not a recognised tile package";
            return false;
        }
        probe = {};
        matrix = TileMatrix::webMercator(0, 22, 256);
        auto rows = db.prepare("SELECT key, tile FROM tiles");
        while (rows.valid() && rows.step()) {
            std::int64_t key = rows.int64(0);
            int z = 0;
            while (z < 29 && (static_cast<std::int64_t>(z + 1) << (2 * (z + 1))) <= key) ++z;
            std::int64_t rest = key - (static_cast<std::int64_t>(z) << (2 * z));
            int x = static_cast<int>(rest >> z), y = static_cast<int>(rest & ((std::int64_t(1) << z) - 1));
            if (!visit(z, x, y, rows.blob(1))) break;
        }
        return true;
    }
    error = path + " is not a recognised tile package";
    return false;
}

}
