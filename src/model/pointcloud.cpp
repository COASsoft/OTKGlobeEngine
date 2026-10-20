#include "model/pointcloud.hpp"

#include "gdal/gdal_sources.hpp"
#include "ote/mgrs.hpp"
#include "points.glsl.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace ote {

namespace {

template <typename T>
T rd(const std::vector<std::uint8_t>& b, size_t o) {
    T v{};
    if (o + sizeof(T) <= b.size()) std::memcpy(&v, b.data() + o, sizeof(T));
    return v;
}

bool readRange(FILE* f, std::uint64_t offset, std::size_t size, std::vector<std::uint8_t>& out) {
    out.resize(size);
#if defined(_WIN32)
    if (_fseeki64(f, static_cast<long long>(offset), SEEK_SET) != 0) return false;
#else
    if (fseeko(f, static_cast<off_t>(offset), SEEK_SET) != 0) return false;
#endif
    return std::fread(out.data(), 1, size, f) == size;
}

int epsgFromGeoKeys(const std::vector<std::uint8_t>& vlr) {
    std::uint16_t keys = rd<std::uint16_t>(vlr, 6);
    int epsg = 0;
    for (std::uint16_t i = 0; i < keys; ++i) {
        size_t o = 8 + static_cast<size_t>(i) * 8;
        std::uint16_t id = rd<std::uint16_t>(vlr, o), loc = rd<std::uint16_t>(vlr, o + 2), value = rd<std::uint16_t>(vlr, o + 6);
        if (loc != 0) continue;
        if (id == 3072 && value != 32767) epsg = value;
        if (id == 2048 && value != 32767 && epsg == 0) epsg = value;
    }
    return epsg;
}

bool nativeTransform(int epsg, double x, double y, double& lat, double& lon) {
    if (epsg == 4326 || epsg == 4269 || epsg == 4979) {
        lon = x;
        lat = y;
        return true;
    }
    Utm u;
    if (epsg >= 32601 && epsg <= 32660) {
        u.zone = epsg - 32600;
        u.north = true;
    } else if (epsg >= 32701 && epsg <= 32760) {
        u.zone = epsg - 32700;
        u.north = false;
    } else if (epsg >= 26901 && epsg <= 26923) {
        u.zone = epsg - 26900;
        u.north = true;
    } else if (epsg >= 6330 && epsg <= 6348) {
        u.zone = epsg - 6329;
        u.north = true;
    } else {
        return false;
    }
    u.easting = x;
    u.northing = y;
    fromUtm(u, lat, lon);
    return true;
}

void ramp(double t, std::uint8_t* c) {
    t = std::clamp(t, 0.0, 1.0);
    const double stops[5][3] = {{0.1, 0.2, 0.8}, {0.0, 0.7, 0.8}, {0.2, 0.8, 0.2}, {0.95, 0.85, 0.1}, {0.9, 0.2, 0.1}};
    double s = t * 4.0;
    int i = std::min(static_cast<int>(s), 3);
    double f = s - i;
    for (int k = 0; k < 3; ++k) c[k] = static_cast<std::uint8_t>(255.0 * (stops[i][k] * (1 - f) + stops[i + 1][k] * f));
    c[3] = 255;
}

}

bool readLas(const std::string& path, std::size_t maxPoints, std::vector<double>& ecef, std::vector<std::uint8_t>& rgba,
             PointCloudInfo& info) {
    std::filesystem::path p = std::filesystem::u8path(path);
#if defined(_WIN32)
    FILE* f = _wfopen(p.wstring().c_str(), L"rb");
#else
    FILE* f = std::fopen(p.string().c_str(), "rb");
#endif
    if (!f) return false;
    std::vector<std::uint8_t> h;
    if (!readRange(f, 0, 375, h) && !readRange(f, 0, 227, h)) {
        std::fclose(f);
        return false;
    }
    if (std::memcmp(h.data(), "LASF", 4) != 0) {
        std::fclose(f);
        return false;
    }
    std::uint8_t minor = h[25];
    std::uint16_t headerSize = rd<std::uint16_t>(h, 94);
    std::uint32_t pointOffset = rd<std::uint32_t>(h, 96);
    std::uint32_t vlrCount = rd<std::uint32_t>(h, 100);
    std::uint8_t format = h[104] & 0x3F;
    bool compressed = (h[104] & 0x80) != 0;
    std::uint16_t recordLen = rd<std::uint16_t>(h, 105);
    std::uint64_t count = rd<std::uint32_t>(h, 107);
    if (minor >= 4 && headerSize >= 375) count = rd<std::uint64_t>(h, 247);
    double sx = rd<double>(h, 131), sy = rd<double>(h, 139), sz = rd<double>(h, 147);
    double ox = rd<double>(h, 155), oy = rd<double>(h, 163), oz = rd<double>(h, 171);
    double maxZ = rd<double>(h, 211), minZ = rd<double>(h, 219);
    if (compressed || recordLen < 20 || count == 0) {
        std::fclose(f);
        return false;
    }
    int epsg = 0;
    std::string wkt;
    std::uint64_t pos = headerSize;
    for (std::uint32_t i = 0; i < vlrCount; ++i) {
        std::vector<std::uint8_t> vh;
        if (!readRange(f, pos, 54, vh)) break;
        std::uint16_t record = rd<std::uint16_t>(vh, 18), len = rd<std::uint16_t>(vh, 20);
        std::string user(reinterpret_cast<const char*>(vh.data() + 2), strnlen(reinterpret_cast<const char*>(vh.data() + 2), 16));
        std::vector<std::uint8_t> body;
        if (len > 0 && readRange(f, pos + 54, len, body)) {
            if (user == "LASF_Projection" && record == 34735) epsg = epsgFromGeoKeys(body);
            if (user == "LASF_Projection" && record == 2112) wkt.assign(body.begin(), body.end());
        }
        pos += 54 + len;
    }
    info.pointCount = count;
    info.epsg = epsg;
    std::uint64_t stride = std::max<std::uint64_t>(1, (count + maxPoints - 1) / std::max<std::size_t>(maxPoints, 1));
    std::size_t colorOffset = 0;
    if (format == 2) colorOffset = 20;
    else if (format == 3 || format == 5) colorOffset = 28;
    else if (format == 7 || format == 8 || format == 10) colorOffset = 30;
    std::vector<double> xs, ys, zs;
    std::vector<std::uint16_t> colors;
    const std::uint64_t chunk = 65536;
    std::vector<std::uint8_t> buf;
    for (std::uint64_t start = 0; start < count; start += chunk) {
        std::uint64_t n = std::min(chunk, count - start);
        if (!readRange(f, pointOffset + start * recordLen, static_cast<std::size_t>(n * recordLen), buf)) break;
        for (std::uint64_t i = 0; i < n; ++i) {
            if ((start + i) % stride) continue;
            size_t o = static_cast<size_t>(i * recordLen);
            xs.push_back(rd<std::int32_t>(buf, o) * sx + ox);
            ys.push_back(rd<std::int32_t>(buf, o + 4) * sy + oy);
            zs.push_back(rd<std::int32_t>(buf, o + 8) * sz + oz);
            if (colorOffset && colorOffset + 6 <= recordLen) {
                colors.push_back(rd<std::uint16_t>(buf, o + colorOffset));
                colors.push_back(rd<std::uint16_t>(buf, o + colorOffset + 2));
                colors.push_back(rd<std::uint16_t>(buf, o + colorOffset + 4));
            }
        }
    }
    std::fclose(f);
    if (xs.empty()) return false;
    std::vector<double> lats(xs.size()), lons(xs.size());
    bool ok = true;
    if (epsg == 0 && wkt.empty()) {
        bool geographic = std::abs(xs[0]) <= 180 && std::abs(ys[0]) <= 90;
        epsg = geographic ? 4326 : 0;
    }
    double t0, t1;
    if (epsg && nativeTransform(epsg, xs[0], ys[0], t0, t1)) {
        for (size_t i = 0; i < xs.size(); ++i) nativeTransform(epsg, xs[i], ys[i], lats[i], lons[i]);
    } else {
        std::vector<double> tx = xs, ty = ys, tz = zs;
        ok = transformToGeographic(epsg, wkt, tx.data(), ty.data(), tz.data(), tx.size());
        if (ok)
            for (size_t i = 0; i < xs.size(); ++i) {
                lons[i] = tx[i];
                lats[i] = ty[i];
                zs[i] = tz[i];
            }
    }
    if (!ok) return false;
    bool wideColor = false;
    for (std::uint16_t c : colors)
        if (c > 255) wideColor = true;
    ecef.resize(xs.size() * 3);
    rgba.resize(xs.size() * 4);
    info.west = info.south = 1e9;
    info.east = info.north = -1e9;
    info.minHeight = minZ;
    info.maxHeight = maxZ;
    for (size_t i = 0; i < xs.size(); ++i) {
        Vec3 e = geodeticToEcef(lats[i], lons[i], zs[i]);
        ecef[i * 3] = e.x;
        ecef[i * 3 + 1] = e.y;
        ecef[i * 3 + 2] = e.z;
        info.west = std::min(info.west, lons[i]);
        info.east = std::max(info.east, lons[i]);
        info.south = std::min(info.south, lats[i]);
        info.north = std::max(info.north, lats[i]);
        std::uint8_t* c = &rgba[i * 4];
        if (colors.size() >= (i + 1) * 3) {
            for (int k = 0; k < 3; ++k) c[k] = static_cast<std::uint8_t>(wideColor ? colors[i * 3 + k] >> 8 : colors[i * 3 + k]);
            c[3] = 255;
        } else {
            ramp((zs[i] - minZ) / std::max(maxZ - minZ, 1e-6), c);
        }
    }
    info.loadedPoints = xs.size();
    return true;
}

bool PointCloudLayer::init() {
    if (ready_) return true;
    const float corners[] = {-1, -1, 1, -1, 1, 1, -1, -1, 1, 1, -1, 1};
    sg_buffer_desc cb{};
    cb.usage.vertex_buffer = true;
    cb.data = SG_RANGE(corners);
    corners_ = sg_make_buffer(&cb);
    shader_ = sg_make_shader(points_shader_desc(sg_query_backend()));
    sg_pipeline_desc d{};
    d.shader = shader_;
    d.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
    d.layout.attrs[ATTR_points_corner] = {0, 0, SG_VERTEXFORMAT_FLOAT2};
    d.layout.attrs[ATTR_points_position] = {1, 0, SG_VERTEXFORMAT_FLOAT3};
    d.layout.attrs[ATTR_points_color0] = {1, 12, SG_VERTEXFORMAT_UBYTE4N};
    d.layout.buffers[1].stride = 16;
    d.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    d.depth.write_enabled = true;
    pipeline_ = sg_make_pipeline(&d);
    ready_ = true;
    return true;
}

void PointCloudLayer::shutdown() {
    if (!ready_) return;
    if (sg_isvalid()) {
        for (auto& [id, c] : clouds_) sg_destroy_buffer(c.instances);
        sg_destroy_buffer(corners_);
        sg_destroy_pipeline(pipeline_);
        sg_destroy_shader(shader_);
    }
    clouds_.clear();
    ready_ = false;
}

std::uint64_t PointCloudLayer::add(const std::string& path, std::size_t maxPoints, float pointSize, PointCloudInfo* info) {
    std::vector<double> ecef;
    std::vector<std::uint8_t> rgba;
    PointCloudInfo pi;
    if (!readLas(path, maxPoints, ecef, rgba, pi)) return 0;
    if (info) *info = pi;
    size_t n = ecef.size() / 3;
    Vec3 center;
    for (size_t i = 0; i < n; ++i) center = center + Vec3{ecef[i * 3], ecef[i * 3 + 1], ecef[i * 3 + 2]};
    center = center * (1.0 / static_cast<double>(n));
    std::vector<std::uint8_t> inst(n * 16);
    for (size_t i = 0; i < n; ++i) {
        float p[3] = {static_cast<float>(ecef[i * 3] - center.x), static_cast<float>(ecef[i * 3 + 1] - center.y),
                      static_cast<float>(ecef[i * 3 + 2] - center.z)};
        std::memcpy(&inst[i * 16], p, 12);
        std::memcpy(&inst[i * 16 + 12], &rgba[i * 4], 4);
    }
    Cloud c;
    sg_buffer_desc d{};
    d.usage.vertex_buffer = true;
    d.data = {inst.data(), inst.size()};
    c.instances = sg_make_buffer(&d);
    c.count = static_cast<int>(n);
    c.center = center;
    c.size = pointSize;
    std::uint64_t id = nextId_++;
    clouds_[id] = c;
    return id;
}

bool PointCloudLayer::remove(std::uint64_t id) {
    auto it = clouds_.find(id);
    if (it == clouds_.end()) return false;
    if (sg_isvalid()) sg_destroy_buffer(it->second.instances);
    clouds_.erase(it);
    return true;
}

void PointCloudLayer::drawBuffer(const Camera& cam, sg_buffer points, int count, const Mat4& modelRelEye, float size, double dpi) {
    if (!ready_ || count <= 0) return;
    sg_apply_pipeline(pipeline_);
    points_params_t p{};
    toFloat(cam.viewProjRel() * modelRelEye, p.mvp);
    p.size[0] = static_cast<float>(size * dpi / cam.width);
    p.size[1] = static_cast<float>(size * dpi / cam.height);
    sg_bindings b{};
    b.vertex_buffers[0] = corners_;
    b.vertex_buffers[1] = points;
    sg_apply_bindings(&b);
    sg_apply_uniforms(UB_points_params, SG_RANGE(p));
    sg_draw(0, 6, count);
}

void PointCloudLayer::draw(const Camera& cam, double dpi) {
    if (!ready_ || clouds_.empty()) return;
    sg_apply_pipeline(pipeline_);
    for (auto& [id, c] : clouds_) {
        points_params_t p{};
        toFloat(cam.viewProjRel() * translation(c.center - cam.eye()), p.mvp);
        p.size[0] = static_cast<float>(c.size * dpi / cam.width);
        p.size[1] = static_cast<float>(c.size * dpi / cam.height);
        sg_bindings b{};
        b.vertex_buffers[0] = corners_;
        b.vertex_buffers[1] = c.instances;
        sg_apply_bindings(&b);
        sg_apply_uniforms(UB_points_params, SG_RANGE(p));
        sg_draw(0, 6, c.count);
    }
}

}
