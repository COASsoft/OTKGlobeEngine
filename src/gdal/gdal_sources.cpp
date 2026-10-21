#include "gdal/gdal_sources.hpp"

#include "core/geoid.hpp"

#include <cpl_conv.h>
#include <gdal.h>
#include <gdalwarper.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

namespace ote {

namespace {

constexpr double kMercHalf = 20037508.342789244;

void registerOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
        GDALAllRegister();
        CPLSetConfigOption("GDAL_NUM_THREADS", "ALL_CPUS");
    });
}

std::string wktFor(int epsg) {
    OGRSpatialReferenceH srs = OSRNewSpatialReference(nullptr);
    OSRImportFromEPSG(srs, epsg);
    OSRSetAxisMappingStrategy(srs, OAMS_TRADITIONAL_GIS_ORDER);
    char* wkt = nullptr;
    OSRExportToWkt(srs, &wkt);
    std::string out = wkt ? wkt : "";
    CPLFree(wkt);
    OSRDestroySpatialReference(srs);
    return out;
}

struct Warped {
    GDALDatasetH source = nullptr;
    GDALDatasetH warped = nullptr;
    double gt[6]{};
    int width = 0, height = 0;
    std::mutex mutex;

    bool open(const std::string& path, int epsg, GDALResampleAlg alg) {
        registerOnce();
        source = GDALOpen(path.c_str(), GA_ReadOnly);
        if (!source) return false;
        const char* srcWkt = GDALGetProjectionRef(source);
        if ((!srcWkt || !*srcWkt) && GDALGetGCPCount(source) > 0) srcWkt = GDALGetGCPProjection(source);
        std::string dst = wktFor(epsg);
        warped = GDALAutoCreateWarpedVRT(source, srcWkt && *srcWkt ? srcWkt : nullptr, dst.c_str(), alg, 0.125, nullptr);
        if (!warped) return false;
        GDALGetGeoTransform(warped, gt);
        width = GDALGetRasterXSize(warped);
        height = GDALGetRasterYSize(warped);
        return width > 0 && height > 0;
    }

    ~Warped() {
        if (warped) GDALClose(warped);
        if (source) GDALClose(source);
    }

    bool window(double minX, double minY, double maxX, double maxY, int size, int& x0, int& y0, int& w, int& h, int& dx,
                int& dy, int& dw, int& dh) const {
        double px0 = (minX - gt[0]) / gt[1], px1 = (maxX - gt[0]) / gt[1];
        double py0 = (maxY - gt[3]) / gt[5], py1 = (minY - gt[3]) / gt[5];
        double cx0 = std::max(px0, 0.0), cx1 = std::min(px1, static_cast<double>(width));
        double cy0 = std::max(py0, 0.0), cy1 = std::min(py1, static_cast<double>(height));
        if (cx1 <= cx0 || cy1 <= cy0) return false;
        x0 = static_cast<int>(std::floor(cx0));
        y0 = static_cast<int>(std::floor(cy0));
        w = std::max(1, static_cast<int>(std::ceil(cx1)) - x0);
        h = std::max(1, static_cast<int>(std::ceil(cy1)) - y0);
        double sx = size / (px1 - px0), sy = size / (py1 - py0);
        dx = std::clamp(static_cast<int>(std::lround((x0 - px0) * sx)), 0, size - 1);
        dy = std::clamp(static_cast<int>(std::lround((y0 - py0) * sy)), 0, size - 1);
        dw = std::clamp(static_cast<int>(std::lround(w * sx)), 1, size - dx);
        dh = std::clamp(static_cast<int>(std::lround(h * sy)), 1, size - dy);
        return true;
    }
};

class GdalRasterProvider : public TileProvider {
public:
    explicit GdalRasterProvider(const ImageryLayer& layer) {
        ok_ = data_.open(layer.path, 3857, GRA_Bilinear);
        int maxZoom = layer.maxZoom, minZoom = layer.minZoom;
        if (ok_) {
            double res = std::abs(data_.gt[1]);
            int native = static_cast<int>(std::ceil(std::log2(2.0 * kMercHalf / (256.0 * res))));
            maxZoom = std::clamp(native, 0, 23);
            minZoom = std::max(0, std::min(layer.minZoom > 0 ? layer.minZoom : 0, maxZoom));
        }
        matrix_ = TileMatrix::webMercator(minZoom, maxZoom, 256);
    }

    FetchResult fetch(int z, int x, int y) override {
        FetchResult r;
        r.status = 404;
        if (!ok_) return r;
        double minX, minY, maxX, maxY;
        matrix_.tileBounds(z, x, y, minX, minY, maxX, maxY);
        int x0, y0, w, h, dx, dy, dw, dh;
        const int n = 256;
        if (!data_.window(minX, minY, maxX, maxY, n, x0, y0, w, h, dx, dy, dw, dh)) return r;
        std::lock_guard lock(data_.mutex);
        int bands = GDALGetRasterCount(data_.warped);
        std::vector<std::uint8_t> rgba(static_cast<size_t>(n) * n * 4, 0);
        std::vector<std::uint8_t> plane(static_cast<size_t>(dw) * dh);
        auto read = [&](int band, int channel) {
            GDALRasterBandH b = GDALGetRasterBand(data_.warped, band);
            if (GDALRasterIO(b, GF_Read, x0, y0, w, h, plane.data(), dw, dh, GDT_Byte, 0, 0) != CE_None) return false;
            for (int j = 0; j < dh; ++j)
                for (int i = 0; i < dw; ++i)
                    rgba[(static_cast<size_t>(dy + j) * n + dx + i) * 4 + channel] = plane[static_cast<size_t>(j) * dw + i];
            return true;
        };
        GDALRasterBandH first = GDALGetRasterBand(data_.warped, 1);
        GDALColorTableH ct = GDALGetRasterColorTable(first);
        if (bands == 1 && ct) {
            if (!read(1, 0)) return r;
            for (int j = 0; j < dh; ++j)
                for (int i = 0; i < dw; ++i) {
                    std::uint8_t* p = &rgba[(static_cast<size_t>(dy + j) * n + dx + i) * 4];
                    const GDALColorEntry* e = GDALGetColorEntry(ct, p[0]);
                    p[0] = static_cast<std::uint8_t>(e ? e->c1 : 0);
                    p[1] = static_cast<std::uint8_t>(e ? e->c2 : 0);
                    p[2] = static_cast<std::uint8_t>(e ? e->c3 : 0);
                    p[3] = static_cast<std::uint8_t>(e ? e->c4 : 255);
                }
        } else {
            for (int c = 0; c < 3; ++c)
                if (!read(std::min(c + 1, bands), c)) return r;
            if (bands >= 4) {
                read(4, 3);
            } else {
                GDALRasterBandH mask = GDALGetMaskBand(first);
                std::vector<std::uint8_t> m(static_cast<size_t>(dw) * dh, 255);
                if (mask) GDALRasterIO(mask, GF_Read, x0, y0, w, h, m.data(), dw, dh, GDT_Byte, 0, 0);
                for (int j = 0; j < dh; ++j)
                    for (int i = 0; i < dw; ++i)
                        rgba[(static_cast<size_t>(dy + j) * n + dx + i) * 4 + 3] = m[static_cast<size_t>(j) * dw + i];
            }
        }
        r.status = 200;
        r.raw = true;
        r.width = n;
        r.height = n;
        r.data = std::move(rgba);
        return r;
    }

private:
    Warped data_;
    bool ok_ = false;
};

class GdalElevationProvider : public ElevationProvider {
public:
    explicit GdalElevationProvider(const ElevationLayer& layer) {
        ok_ = data_.open(layer.path, 4326, GRA_Bilinear);
        int maxZoom = layer.maxZoom;
        if (ok_) {
            double res = std::abs(data_.gt[1]);
            maxZoom = std::clamp(static_cast<int>(std::ceil(std::log2(180.0 / (64.0 * res)))), 0, 18);
        }
        matrix_ = TileMatrix::geographic(0, maxZoom, 64);
    }

    int fetch(int z, int x, int y, HeightGrid& out) override {
        if (!ok_) return 404;
        matrix_.tileBounds(z, x, y, out.minX, out.minY, out.maxX, out.maxY);
        const int n = 64;
        int x0, y0, w, h, dx, dy, dw, dh;
        if (!data_.window(out.minX, out.minY, out.maxX, out.maxY, n, x0, y0, w, h, dx, dy, dw, dh)) return 404;
        std::lock_guard lock(data_.mutex);
        GDALRasterBandH band = GDALGetRasterBand(data_.warped, 1);
        int hasNoData = 0;
        double noData = GDALGetRasterNoDataValue(band, &hasNoData);
        std::vector<float> plane(static_cast<size_t>(dw) * dh);
        if (GDALRasterIO(band, GF_Read, x0, y0, w, h, plane.data(), dw, dh, GDT_Float32, 0, 0) != CE_None) return 0;
        out.width = out.height = n;
        out.values.assign(static_cast<size_t>(n) * n, std::numeric_limits<float>::quiet_NaN());
        bool any = false;
        for (int j = 0; j < dh; ++j)
            for (int i = 0; i < dw; ++i) {
                float v = plane[static_cast<size_t>(j) * dw + i];
                if (hasNoData && v == static_cast<float>(noData)) continue;
                double lat = out.maxY - (dy + j + 0.5) * (out.maxY - out.minY) / n;
                double lon = out.minX + (dx + i + 0.5) * (out.maxX - out.minX) / n;
                out.values[static_cast<size_t>(dy + j) * n + dx + i] = v + static_cast<float>(geoidHeight(lat, lon));
                any = true;
            }
        return any ? 200 : 404;
    }

private:
    Warped data_;
    bool ok_ = false;
};

void ogrGeometry(OGRGeometryH g, Feature& f) {
    if (!g) return;
    OGRwkbGeometryType t = wkbFlatten(OGR_G_GetGeometryType(g));
    auto coords = [](OGRGeometryH line) {
        std::vector<Coord> out;
        int n = OGR_G_GetPointCount(line);
        for (int i = 0; i < n; ++i) out.push_back({OGR_G_GetY(line, i), OGR_G_GetX(line, i), OGR_G_GetZ(line, i)});
        return out;
    };
    switch (t) {
    case wkbPoint:
        f.geometries.push_back({GeometryType::Point, {Coord{OGR_G_GetY(g, 0), OGR_G_GetX(g, 0), OGR_G_GetZ(g, 0)}}, {}});
        break;
    case wkbLineString:
        f.geometries.push_back({GeometryType::LineString, coords(g), {}});
        break;
    case wkbPolygon: {
        Geometry geom{GeometryType::Polygon, {}, {}};
        int rings = OGR_G_GetGeometryCount(g);
        for (int i = 0; i < rings; ++i) {
            auto ring = coords(OGR_G_GetGeometryRef(g, i));
            if (i == 0) geom.points = ring;
            else geom.holes.push_back(ring);
        }
        f.geometries.push_back(std::move(geom));
        break;
    }
    case wkbMultiPoint:
    case wkbMultiLineString:
    case wkbMultiPolygon:
    case wkbGeometryCollection:
        for (int i = 0; i < OGR_G_GetGeometryCount(g); ++i) ogrGeometry(OGR_G_GetGeometryRef(g, i), f);
        break;
    default:
        break;
    }
}

}

bool gdalAvailable() { return true; }

bool gdalIsElevation(const std::string& path) {
    GDALDatasetH ds = GDALOpen(path.c_str(), GA_ReadOnly);
    if (!ds) return false;
    bool dem = GDALGetRasterCount(ds) == 1 && GDALGetRasterDataType(GDALGetRasterBand(ds, 1)) != GDT_Byte;
    GDALClose(ds);
    return dem;
}

bool transformToGeographic(int epsg, const std::string& wkt, double* x, double* y, double* z, std::size_t n) {
    registerOnce();
    OGRSpatialReferenceH src = OSRNewSpatialReference(nullptr);
    bool ok = !wkt.empty() ? OSRSetFromUserInput(src, wkt.c_str()) == OGRERR_NONE
                           : epsg > 0 && OSRImportFromEPSG(src, epsg) == OGRERR_NONE;
    OGRSpatialReferenceH dst = OSRNewSpatialReference(nullptr);
    OSRImportFromEPSG(dst, 4979);
    OSRSetAxisMappingStrategy(src, OAMS_TRADITIONAL_GIS_ORDER);
    OSRSetAxisMappingStrategy(dst, OAMS_TRADITIONAL_GIS_ORDER);
    if (ok) {
        OGRCoordinateTransformationH ct = OCTNewCoordinateTransformation(src, dst);
        ok = ct && OCTTransform(ct, static_cast<int>(n), x, y, z);
        if (ct) OCTDestroyCoordinateTransformation(ct);
    }
    OSRDestroySpatialReference(src);
    OSRDestroySpatialReference(dst);
    return ok;
}

std::shared_ptr<TileProvider> makeGdalRaster(const ImageryLayer& layer) { return std::make_shared<GdalRasterProvider>(layer); }

std::shared_ptr<ElevationProvider> makeGdalElevation(const ElevationLayer& layer) {
    return std::make_shared<GdalElevationProvider>(layer);
}

std::vector<Feature> parseWithOgr(const std::string& path) {
    registerOnce();
    std::vector<Feature> out;
    GDALDatasetH ds = GDALOpenEx(path.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr);
    if (!ds) return out;
    OGRSpatialReferenceH wgs = OSRNewSpatialReference(nullptr);
    OSRImportFromEPSG(wgs, 4326);
    OSRSetAxisMappingStrategy(wgs, OAMS_TRADITIONAL_GIS_ORDER);
    for (int l = 0; l < GDALDatasetGetLayerCount(ds); ++l) {
        OGRLayerH layer = GDALDatasetGetLayer(ds, l);
        OGRSpatialReferenceH srs = OGR_L_GetSpatialRef(layer);
        OGRCoordinateTransformationH ct = srs ? OCTNewCoordinateTransformation(srs, wgs) : nullptr;
        OGRFeatureDefnH defn = OGR_L_GetLayerDefn(layer);
        int nameField = -1;
        for (const char* n : {"name", "Name", "NAME", "title", "label"}) {
            nameField = OGR_FD_GetFieldIndex(defn, n);
            if (nameField >= 0) break;
        }
        OGR_L_ResetReading(layer);
        while (OGRFeatureH feat = OGR_L_GetNextFeature(layer)) {
            Feature f;
            if (nameField >= 0) f.name = OGR_F_GetFieldAsString(feat, nameField);
            OGRGeometryH g = OGR_F_GetGeometryRef(feat);
            if (g) {
                OGRGeometryH c = OGR_G_Clone(g);
                if (ct) OGR_G_Transform(c, ct);
                ogrGeometry(c, f);
                OGR_G_DestroyGeometry(c);
            }
            for (const auto& geom : f.geometries)
                if (geom.type == GeometryType::Polygon) f.style.fillColor = 0x40FFFFFF;
            if (!f.geometries.empty()) out.push_back(std::move(f));
            OGR_F_Destroy(feat);
        }
        if (ct) OCTDestroyCoordinateTransformation(ct);
    }
    OSRDestroySpatialReference(wgs);
    GDALClose(ds);
    return out;
}

}
