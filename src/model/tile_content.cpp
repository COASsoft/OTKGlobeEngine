#include "model/model_layer.hpp"

#include "core/geo.hpp"
#include "model/gltf_codecs.hpp"

#include <json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace otk {

namespace {

constexpr int kMaxCompositeDepth = 8;

std::uint32_t u32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

float f32(const std::uint8_t* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

std::uint16_t u16(const std::uint8_t* p) {
    std::uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

std::string dirOf(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? std::string() : path.substr(0, s + 1);
}

struct FeatureTable {
    nlohmann::json json;
    const std::uint8_t* bin = nullptr;
    size_t binSize = 0;

    const std::uint8_t* property(const char* name, size_t elementBytes, size_t count) const {
        if (!json.contains(name) || !json[name].is_object() || !json[name].contains("byteOffset")) return nullptr;
        size_t off = json[name]["byteOffset"].get<size_t>();
        if (off + elementBytes * count > binSize) return nullptr;
        return bin + off;
    }

    bool vec3(const char* name, double out[3]) const {
        if (!json.contains(name)) return false;
        const auto& v = json[name];
        if (v.is_array() && v.size() == 3) {
            for (int k = 0; k < 3; ++k) out[k] = v[k].get<double>();
            return true;
        }
        if (const std::uint8_t* p = property(name, 12, 1)) {
            for (int k = 0; k < 3; ++k) out[k] = f32(p + k * 4);
            return true;
        }
        return false;
    }

    size_t count(const char* name) const {
        if (!json.contains(name)) return 0;
        const auto& v = json[name];
        if (v.is_number_unsigned() || v.is_number_integer()) return v.get<size_t>();
        if (const std::uint8_t* p = property(name, 4, 1)) return u32(p);
        return 0;
    }
};

bool readFeatureTable(const std::uint8_t* data, size_t size, size_t header, std::uint32_t jsonLen, std::uint32_t binLen, FeatureTable& ft) {
    if (header + static_cast<size_t>(jsonLen) + binLen > size) return false;
    if (jsonLen > 0) {
        ft.json = nlohmann::json::parse(data + header, data + header + jsonLen, nullptr, false);
        if (ft.json.is_discarded()) return false;
    } else {
        ft.json = nlohmann::json::object();
    }
    ft.bin = data + header + jsonLen;
    ft.binSize = binLen;
    return true;
}

Vec3 octDecode(std::uint16_t x, std::uint16_t y) {
    double fx = x / 65535.0 * 2.0 - 1.0, fy = y / 65535.0 * 2.0 - 1.0;
    double fz = 1.0 - std::abs(fx) - std::abs(fy);
    if (fz < 0) {
        double ox = fx;
        fx = (1.0 - std::abs(fy)) * (ox >= 0 ? 1.0 : -1.0);
        fy = (1.0 - std::abs(ox)) * (fy >= 0 ? 1.0 : -1.0);
    }
    return normalize(Vec3{fx, fy, fz});
}

bool positions(const FeatureTable& ft, size_t n, std::vector<double>& out) {
    out.resize(n * 3);
    if (const std::uint8_t* p = ft.property("POSITION", 12, n)) {
        for (size_t i = 0; i < n * 3; ++i) out[i] = f32(p + i * 4);
        return true;
    }
    double offset[3], scale[3];
    if (const std::uint8_t* p = ft.property("POSITION_QUANTIZED", 6, n)) {
        if (!ft.vec3("QUANTIZED_VOLUME_OFFSET", offset) || !ft.vec3("QUANTIZED_VOLUME_SCALE", scale)) return false;
        for (size_t i = 0; i < n; ++i)
            for (int k = 0; k < 3; ++k) out[i * 3 + k] = offset[k] + u16(p + (i * 3 + k) * 2) / 65535.0 * scale[k];
        return true;
    }
    return false;
}

bool parseGlb(const std::uint8_t* data, size_t size, const std::string& path, const ResourceReader& read, std::vector<CpuModel>& out) {
    CpuModel m;
    if (!parseGltf(data, size, path, read, m)) return false;
    out.push_back(std::move(m));
    return true;
}

bool parseB3dm(const std::uint8_t* data, size_t size, const std::string& path, const ResourceReader& read, std::vector<CpuModel>& out) {
    if (size < 28) return false;
    std::uint32_t ftJson = u32(data + 12), ftBin = u32(data + 16), btJson = u32(data + 20), btBin = u32(data + 24);
    size_t header = 28;
    if (btJson >= 570425344) {
        header = 20;
        btJson = ftBin;
        btBin = ftJson;
        ftJson = 0;
        ftBin = 0;
    }
    FeatureTable ft;
    if (!readFeatureTable(data, size, header, ftJson, ftBin, ft)) return false;
    size_t glb = header + static_cast<size_t>(ftJson) + ftBin + btJson + btBin;
    if (glb >= size) return false;
    CpuModel m;
    if (!parseGltf(data + glb, size - glb, path, read, m)) return false;
    double rtc[3];
    if (ft.vec3("RTC_CENTER", rtc))
        for (int k = 0; k < 3; ++k) m.rtc[k] = rtc[k];
    out.push_back(std::move(m));
    return true;
}

bool parseI3dm(const std::uint8_t* data, size_t size, const std::string& path, const ResourceReader& read, const Mat4& tileTransform,
               std::vector<CpuModel>& out) {
    if (size < 32) return false;
    std::uint32_t ftJson = u32(data + 12), ftBin = u32(data + 16), btJson = u32(data + 20), btBin = u32(data + 24), format = u32(data + 28);
    FeatureTable ft;
    if (!readFeatureTable(data, size, 32, ftJson, ftBin, ft)) return false;
    size_t glb = 32 + static_cast<size_t>(ftJson) + ftBin + btJson + btBin;
    if (glb >= size) return false;
    size_t n = ft.count("INSTANCES_LENGTH");
    std::vector<double> pos;
    if (n == 0 || !positions(ft, n, pos)) return false;
    CpuModel m;
    if (format == 1) {
        if (!parseGltf(data + glb, size - glb, path, read, m)) return false;
    } else {
        std::string uri(reinterpret_cast<const char*>(data + glb), size - glb);
        uri.erase(std::find_if(uri.rbegin(), uri.rend(), [](char c) { return c != '\0' && c != ' '; }).base(), uri.end());
        std::string ref = uri.find("://") != std::string::npos ? uri : dirOf(path) + uri;
        std::vector<std::uint8_t> bytes = read ? read(ref) : std::vector<std::uint8_t>();
        if (bytes.empty() || !parseGltf(bytes.data(), bytes.size(), ref, read, m)) return false;
    }
    double rtc[3] = {0, 0, 0};
    ft.vec3("RTC_CENTER", rtc);
    const std::uint8_t* up = ft.property("NORMAL_UP", 12, n);
    const std::uint8_t* right = ft.property("NORMAL_RIGHT", 12, n);
    const std::uint8_t* upOct = ft.property("NORMAL_UP_OCT32P", 4, n);
    const std::uint8_t* rightOct = ft.property("NORMAL_RIGHT_OCT32P", 4, n);
    const std::uint8_t* scale = ft.property("SCALE", 4, n);
    const std::uint8_t* scaleNu = ft.property("SCALE_NON_UNIFORM", 12, n);
    const bool enu = ft.json.value("EAST_NORTH_UP", false);
    Mat4 toLocal = Mat4::identity();
    if (enu) {
        Mat4 inv;
        if (invert(tileTransform, inv)) toLocal = inv;
        toLocal.at(0, 3) = toLocal.at(1, 3) = toLocal.at(2, 3) = 0;
    }
    const Mat4 yUp = yUpToZUp();
    CpuInstances set;
    set.matrices.resize(n * 16);
    for (size_t i = 0; i < n; ++i) {
        Vec3 p{pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]};
        Vec3 ax{1, 0, 0}, ay{0, 1, 0}, az{0, 0, 1};
        if ((up && right) || (upOct && rightOct)) {
            Vec3 u = up ? Vec3{f32(up + i * 12), f32(up + i * 12 + 4), f32(up + i * 12 + 8)} : octDecode(u16(upOct + i * 4), u16(upOct + i * 4 + 2));
            Vec3 r = right ? Vec3{f32(right + i * 12), f32(right + i * 12 + 4), f32(right + i * 12 + 8)}
                           : octDecode(u16(rightOct + i * 4), u16(rightOct + i * 4 + 2));
            ax = r;
            ay = u;
            az = cross(r, u);
        } else if (enu) {
            Vec4 w = tileTransform * Vec4{p.x + rtc[0], p.y + rtc[1], p.z + rtc[2], 1.0};
            GeoPoint g = ecefToGeodetic({w.x, w.y, w.z});
            Vec3 e, nn, uu;
            enuFrame(g.lat, g.lon, e, nn, uu);
            auto local = [&](Vec3 v) {
                Vec4 r = toLocal * Vec4{v.x, v.y, v.z, 0.0};
                return normalize(Vec3{r.x, r.y, r.z});
            };
            ax = local(e);
            ay = local(nn);
            az = local(uu);
        }
        Vec3 s{1, 1, 1};
        if (scale) s = Vec3{1, 1, 1} * f32(scale + i * 4);
        if (scaleNu) s = Vec3{f32(scaleNu + i * 12), f32(scaleNu + i * 12 + 4), f32(scaleNu + i * 12 + 8)};
        Mat4 inst = Mat4::identity();
        inst.at(0, 0) = ax.x * s.x; inst.at(1, 0) = ax.y * s.x; inst.at(2, 0) = ax.z * s.x;
        inst.at(0, 1) = ay.x * s.y; inst.at(1, 1) = ay.y * s.y; inst.at(2, 1) = ay.z * s.y;
        inst.at(0, 2) = az.x * s.z; inst.at(1, 2) = az.y * s.z; inst.at(2, 2) = az.z * s.z;
        inst.at(0, 3) = p.x; inst.at(1, 3) = p.y; inst.at(2, 3) = p.z;
        toFloat(inst * yUp, &set.matrices[i * 16]);
    }
    m.instances.assign(1, std::move(set));
    for (CpuPrimitive& prim : m.primitives) prim.instances = 0;
    for (int k = 0; k < 3; ++k) m.rtc[k] = rtc[k];
    m.points.clear();
    finishModel(m);
    if (m.primitives.empty()) return false;
    out.push_back(std::move(m));
    return true;
}

bool parsePnts(const std::uint8_t* data, size_t size, std::vector<CpuModel>& out) {
    if (size < 28) return false;
    FeatureTable ft;
    if (!readFeatureTable(data, size, 28, u32(data + 12), u32(data + 16), ft)) return false;
    size_t n = ft.count("POINTS_LENGTH");
    if (n == 0) return false;
    CpuModel m;
    double rtc[3];
    if (ft.vec3("RTC_CENTER", rtc))
        for (int k = 0; k < 3; ++k) m.rtc[k] = rtc[k];
    std::vector<double> pos;
    std::vector<std::uint8_t> rgba;
    const nlohmann::json* draco = nullptr;
    if (ft.json.contains("extensions") && ft.json["extensions"].contains("3DTILES_draco_point_compression"))
        draco = &ft.json["extensions"]["3DTILES_draco_point_compression"];
    if (draco) {
        std::map<std::string, int> props;
        if (draco->contains("properties"))
            for (auto it = (*draco)["properties"].begin(); it != (*draco)["properties"].end(); ++it) props[it.key()] = it.value().get<int>();
        size_t off = draco->value("byteOffset", size_t(0)), len = draco->value("byteLength", size_t(0));
        if (off + len > ft.binSize) return false;
        std::vector<float> fpos;
        if (!decodeDracoPoints(ft.bin + off, len, props, fpos, rgba)) return false;
        pos.assign(fpos.begin(), fpos.end());
        n = pos.size() / 3;
        if (n == 0) return false;
    } else if (!positions(ft, n, pos)) {
        return false;
    }
    if (rgba.size() < n * 4) {
        rgba.assign(n * 4, 255);
        if (const std::uint8_t* c = ft.property("RGBA", 4, n)) {
            std::memcpy(rgba.data(), c, n * 4);
        } else if (const std::uint8_t* c3 = ft.property("RGB", 3, n)) {
            for (size_t i = 0; i < n; ++i)
                for (int k = 0; k < 3; ++k) rgba[i * 4 + k] = c3[i * 3 + k];
        } else if (const std::uint8_t* c565 = ft.property("RGB565", 2, n)) {
            for (size_t i = 0; i < n; ++i) {
                std::uint16_t v = u16(c565 + i * 2);
                rgba[i * 4] = static_cast<std::uint8_t>(((v >> 11) & 31) * 255 / 31);
                rgba[i * 4 + 1] = static_cast<std::uint8_t>(((v >> 5) & 63) * 255 / 63);
                rgba[i * 4 + 2] = static_cast<std::uint8_t>((v & 31) * 255 / 31);
            }
        } else if (ft.json.contains("CONSTANT_RGBA") && ft.json["CONSTANT_RGBA"].is_array() && ft.json["CONSTANT_RGBA"].size() == 4) {
            for (size_t i = 0; i < n; ++i)
                for (int k = 0; k < 4; ++k) rgba[i * 4 + k] = ft.json["CONSTANT_RGBA"][k].get<std::uint8_t>();
        } else {
            for (size_t i = 0; i < n; ++i) rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 220;
        }
    }
    for (int k = 0; k < 3; ++k) m.pointsOrigin[k] = pos[k];
    m.points.resize(n * 16);
    for (size_t i = 0; i < n; ++i) {
        float f[3];
        for (int k = 0; k < 3; ++k) f[k] = static_cast<float>(pos[i * 3 + k] - m.pointsOrigin[k]);
        std::memcpy(&m.points[i * 16], f, 12);
        std::memcpy(&m.points[i * 16 + 12], &rgba[i * 4], 4);
    }
    finishModel(m);
    out.push_back(std::move(m));
    return true;
}

bool parseContent(const std::uint8_t* data, size_t size, const std::string& path, const ResourceReader& read, const Mat4& tileTransform,
                  std::vector<CpuModel>& out, int depth);

bool parseCmpt(const std::uint8_t* data, size_t size, const std::string& path, const ResourceReader& read, const Mat4& tileTransform,
               std::vector<CpuModel>& out, int depth) {
    if (size < 16 || depth > kMaxCompositeDepth) return false;
    std::uint32_t tiles = u32(data + 12);
    size_t off = 16;
    bool any = false;
    for (std::uint32_t i = 0; i < tiles; ++i) {
        if (off + 12 > size) break;
        std::uint32_t len = u32(data + off + 8);
        if (len < 12 || off + len > size) break;
        any = parseContent(data + off, len, path, read, tileTransform, out, depth + 1) || any;
        off += len;
    }
    return any;
}

bool parseContent(const std::uint8_t* data, size_t size, const std::string& path, const ResourceReader& read, const Mat4& tileTransform,
                  std::vector<CpuModel>& out, int depth) {
    if (size < 4) return false;
    if (std::memcmp(data, "b3dm", 4) == 0) return parseB3dm(data, size, path, read, out);
    if (std::memcmp(data, "i3dm", 4) == 0) return parseI3dm(data, size, path, read, tileTransform, out);
    if (std::memcmp(data, "pnts", 4) == 0) return parsePnts(data, size, out);
    if (std::memcmp(data, "cmpt", 4) == 0) return parseCmpt(data, size, path, read, tileTransform, out, depth);
    if (std::memcmp(data, "glTF", 4) == 0) return parseGlb(data, size, path, read, out);
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == ' ' || data[i] == '\t' || data[i] == '\r' || data[i] == '\n') continue;
        if (data[i] == '{') return parseGlb(data, size, path, read, out);
        break;
    }
    return false;
}

}

bool parseTileContent(const std::uint8_t* data, std::size_t size, const std::string& path, const ResourceReader& read, const Mat4& tileTransform,
                      std::vector<CpuModel>& out) {
    return parseContent(data, size, path, read, tileTransform, out, 0);
}

}
