#include "model/model_layer.hpp"

#include "model/gltf_codecs.hpp"

#include "model.glsl.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <json.hpp>
#include "ote/image.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <tuple>

namespace ote {

namespace {

constexpr size_t kMaxMergedVertices = 1u << 20;
constexpr int kMaxMips = 16;

Mat4 rotZ(double a) {
    Mat4 r = Mat4::identity();
    double c = std::cos(a), s = std::sin(a);
    r.at(0, 0) = c;
    r.at(0, 1) = -s;
    r.at(1, 0) = s;
    r.at(1, 1) = c;
    return r;
}

Mat4 rotX(double a) {
    Mat4 r = Mat4::identity();
    double c = std::cos(a), s = std::sin(a);
    r.at(1, 1) = c;
    r.at(1, 2) = -s;
    r.at(2, 1) = s;
    r.at(2, 2) = c;
    return r;
}

Mat4 rotY(double a) {
    Mat4 r = Mat4::identity();
    double c = std::cos(a), s = std::sin(a);
    r.at(0, 0) = c;
    r.at(0, 2) = s;
    r.at(2, 0) = -s;
    r.at(2, 2) = c;
    return r;
}

Mat4 scaleMat(double s) {
    Mat4 r = Mat4::identity();
    r.at(0, 0) = r.at(1, 1) = r.at(2, 2) = s;
    return r;
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& f) {
    std::vector<std::uint8_t> bytes;
#if defined(_WIN32)
    FILE* fp = _wfopen(f.wstring().c_str(), L"rb");
#else
    FILE* fp = std::fopen(f.string().c_str(), "rb");
#endif
    if (!fp) return bytes;
    std::fseek(fp, 0, SEEK_END);
    long n = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    bytes.resize(n > 0 ? static_cast<size_t>(n) : 0);
    if (std::fread(bytes.data(), 1, bytes.size(), fp) != bytes.size()) bytes.clear();
    std::fclose(fp);
    return bytes;
}

std::string dirOf(const std::string& path) {
    size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? std::string() : path.substr(0, s + 1);
}

cgltf_result readResource(const cgltf_memory_options* memory, const cgltf_file_options* file, const char* path, cgltf_size* size, void** data) {
    const auto* read = static_cast<const ResourceReader*>(file->user_data);
    if (!read || !*read || !path) return cgltf_result_file_not_found;
    std::vector<std::uint8_t> bytes = (*read)(path);
    if (bytes.empty()) return cgltf_result_file_not_found;
    void* p = memory->alloc_func ? memory->alloc_func(memory->user_data, bytes.size()) : std::malloc(bytes.size());
    if (!p) return cgltf_result_out_of_memory;
    std::memcpy(p, bytes.data(), bytes.size());
    *size = bytes.size();
    *data = p;
    return cgltf_result_success;
}

void releaseResource(const cgltf_memory_options* memory, const cgltf_file_options*, void* data) {
    if (memory->free_func) memory->free_func(memory->user_data, data);
    else std::free(data);
}

const std::uint8_t* viewData(const cgltf_buffer_view* bv) {
    if (bv->data) return static_cast<const std::uint8_t*>(bv->data);
    if (!bv->buffer || !bv->buffer->data || bv->offset + bv->size > bv->buffer->size) return nullptr;
    return static_cast<const std::uint8_t*>(bv->buffer->data) + bv->offset;
}

std::vector<std::uint8_t> imageBytes(const cgltf_image* img, const cgltf_options& options, const std::string& path, const ResourceReader& read) {
    std::vector<std::uint8_t> bytes;
    if (img->buffer_view) {
        if (const std::uint8_t* p = viewData(img->buffer_view)) bytes.assign(p, p + img->buffer_view->size);
    } else if (img->uri && std::strncmp(img->uri, "data:", 5) == 0) {
        const char* comma = std::strchr(img->uri, ',');
        if (comma && comma - img->uri >= 7 && std::strncmp(comma - 7, ";base64", 7) == 0) {
            size_t len = std::strlen(comma + 1);
            size_t pad = (len >= 1 && comma[len] == '=') + (len >= 2 && comma[len - 1] == '=');
            size_t size = len / 4 * 3 - pad;
            void* out = nullptr;
            if (size > 0 && cgltf_load_buffer_base64(&options, size, comma + 1, &out) == cgltf_result_success) {
                bytes.assign(static_cast<std::uint8_t*>(out), static_cast<std::uint8_t*>(out) + size);
                std::free(out);
            }
        }
    } else if (img->uri && read) {
        std::string uri = img->uri;
        uri.resize(cgltf_decode_uri(uri.data()));
        bytes = read(dirOf(path) + uri);
    }
    return bytes;
}

void buildMips(CpuImage& img) {
    int w = img.width, h = img.height;
    while ((w > 1 || h > 1) && static_cast<int>(img.levels.size()) < kMaxMips) {
        const std::vector<std::uint8_t>& src = img.levels.back();
        int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
        std::vector<std::uint8_t> dst(static_cast<size_t>(nw) * nh * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x) {
                int x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1);
                int y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
                for (int c = 0; c < 4; ++c) {
                    int s = src[(static_cast<size_t>(y0) * w + x0) * 4 + c] + src[(static_cast<size_t>(y0) * w + x1) * 4 + c] +
                            src[(static_cast<size_t>(y1) * w + x0) * 4 + c] + src[(static_cast<size_t>(y1) * w + x1) * 4 + c];
                    dst[(static_cast<size_t>(y) * nw + x) * 4 + c] = static_cast<std::uint8_t>((s + 2) / 4);
                }
            }
        img.levels.push_back(std::move(dst));
        w = nw;
        h = nh;
    }
}

bool decodeRaster(const std::vector<std::uint8_t>& bytes, CpuImage& img) {
    if (bytes.empty()) return false;
    if (bytes.size() >= 12 && std::memcmp(bytes.data(), "\xABKTX 20\xBB\r\n\x1A\n", 12) == 0) return transcodeKtx2(bytes.data(), bytes.size(), img);
    Image decoded;
    if (!decodeImage(bytes.data(), bytes.size(), decoded)) return false;
    img.width = decoded.width;
    img.height = decoded.height;
    img.format = SG_PIXELFORMAT_RGBA8;
    img.levels.assign(1, std::move(decoded.rgba));
    buildMips(img);
    return true;
}

int decodeTexture(const cgltf_texture* tex, const cgltf_options& options, const std::string& path, const ResourceReader& read, CpuModel& out,
                  std::map<const cgltf_image*, int>& cache) {
    if (!tex) return -1;
    for (const cgltf_image* img : {tex->has_basisu ? tex->basisu_image : nullptr, tex->has_webp ? tex->webp_image : nullptr, tex->image}) {
        if (!img) continue;
        auto it = cache.find(img);
        if (it != cache.end()) {
            if (it->second >= 0) return it->second;
            continue;
        }
        CpuImage decoded;
        int index = -1;
        if (decodeRaster(imageBytes(img, options, path, read), decoded)) {
            index = static_cast<int>(out.images.size());
            out.images.push_back(std::move(decoded));
        }
        cache[img] = index;
        if (index >= 0) return index;
    }
    return -1;
}

int decodeMaterial(const cgltf_material* m, const cgltf_options& options, const std::string& path, const ResourceReader& read, CpuModel& out,
                   std::map<const cgltf_image*, int>& images, std::map<const cgltf_material*, int>& materials) {
    if (!m) return -1;
    auto it = materials.find(m);
    if (it != materials.end()) return it->second;
    CpuMaterial mat;
    const cgltf_pbr_metallic_roughness& pbr = m->pbr_metallic_roughness;
    for (int k = 0; k < 4; ++k) mat.color[k] = pbr.base_color_factor[k];
    mat.metallic = m->has_pbr_metallic_roughness ? pbr.metallic_factor : 0.0f;
    mat.roughness = m->has_pbr_metallic_roughness ? pbr.roughness_factor : 1.0f;
    const float strength = m->has_emissive_strength ? m->emissive_strength.emissive_strength : 1.0f;
    for (int k = 0; k < 3; ++k) mat.emissive[k] = m->emissive_factor[k] * strength;
    mat.unlit = m->unlit != 0;
    mat.image = decodeTexture(pbr.base_color_texture.texture, options, path, read, out, images);
    if (!mat.unlit) {
        mat.normalImage = decodeTexture(m->normal_texture.texture, options, path, read, out, images);
        mat.normalScale = m->normal_texture.scale;
        mat.emissiveImage = decodeTexture(m->emissive_texture.texture, options, path, read, out, images);
        mat.ormImage = decodeTexture(pbr.metallic_roughness_texture.texture, options, path, read, out, images);
        mat.ormOcclusion = mat.ormImage >= 0 && m->occlusion_texture.texture && pbr.metallic_roughness_texture.texture &&
                           m->occlusion_texture.texture->image == pbr.metallic_roughness_texture.texture->image;
    }
    int index = static_cast<int>(out.materials.size());
    out.materials.push_back(mat);
    materials[m] = index;
    return index;
}

void applyTextureTransform(std::vector<float>& verts, const cgltf_texture_transform& t) {
    const float c = std::cos(t.rotation), s = std::sin(t.rotation);
    for (size_t i = 0; i + 8 <= verts.size(); i += 8) {
        float u = verts[i + 6], v = verts[i + 7];
        verts[i + 6] = t.offset[0] + c * t.scale[0] * u + s * t.scale[1] * v;
        verts[i + 7] = t.offset[1] - s * t.scale[0] * u + c * t.scale[1] * v;
    }
}

std::vector<std::uint32_t> toTriangleList(cgltf_primitive_type mode, const std::vector<std::uint32_t>& in) {
    if (mode == cgltf_primitive_type_triangles) return in;
    std::vector<std::uint32_t> out;
    if (in.size() < 3) return out;
    out.reserve((in.size() - 2) * 3);
    for (size_t i = 2; i < in.size(); ++i) {
        if (mode == cgltf_primitive_type_triangle_strip) {
            if (i % 2 == 0) out.insert(out.end(), {in[i - 2], in[i - 1], in[i]});
            else out.insert(out.end(), {in[i - 1], in[i - 2], in[i]});
        } else {
            out.insert(out.end(), {in[0], in[i - 1], in[i]});
        }
    }
    return out;
}

Mat4 fromFloats(const float m[16]) {
    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = m[i];
    return r;
}

bool gpuInstances(const cgltf_node* node, std::vector<float>& out) {
    const cgltf_accessor *tr = nullptr, *rot = nullptr, *sc = nullptr;
    for (cgltf_size i = 0; i < node->mesh_gpu_instancing.attributes_count; ++i) {
        const cgltf_attribute& a = node->mesh_gpu_instancing.attributes[i];
        if (!a.name) continue;
        if (std::strcmp(a.name, "TRANSLATION") == 0) tr = a.data;
        else if (std::strcmp(a.name, "ROTATION") == 0) rot = a.data;
        else if (std::strcmp(a.name, "SCALE") == 0) sc = a.data;
    }
    const cgltf_accessor* any = tr ? tr : rot ? rot : sc;
    if (!any) return false;
    size_t n = any->count;
    out.resize(n * 16);
    for (size_t i = 0; i < n; ++i) {
        float t[3] = {0, 0, 0}, q[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
        if (tr) cgltf_accessor_read_float(tr, i, t, 3);
        if (rot) cgltf_accessor_read_float(rot, i, q, 4);
        if (sc) cgltf_accessor_read_float(sc, i, s, 3);
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        float* m = &out[i * 16];
        m[0] = (1 - 2 * (y * y + z * z)) * s[0];
        m[1] = (2 * (x * y + z * w)) * s[0];
        m[2] = (2 * (x * z - y * w)) * s[0];
        m[3] = 0;
        m[4] = (2 * (x * y - z * w)) * s[1];
        m[5] = (1 - 2 * (x * x + z * z)) * s[1];
        m[6] = (2 * (y * z + x * w)) * s[1];
        m[7] = 0;
        m[8] = (2 * (x * z + y * w)) * s[2];
        m[9] = (2 * (y * z - x * w)) * s[2];
        m[10] = (1 - 2 * (x * x + y * y)) * s[2];
        m[11] = 0;
        m[12] = t[0];
        m[13] = t[1];
        m[14] = t[2];
        m[15] = 1;
    }
    return true;
}

void transformVertices(std::vector<float>& verts, const Mat4& m) {
    Mat4 inv;
    Mat4 normalM = Mat4::identity();
    if (invert(m, inv))
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) normalM.at(r, c) = inv.at(c, r);
    for (size_t i = 0; i + 8 <= verts.size(); i += 8) {
        Vec4 p = m * Vec4{verts[i], verts[i + 1], verts[i + 2], 1.0};
        Vec4 n = normalM * Vec4{verts[i + 3], verts[i + 4], verts[i + 5], 0.0};
        Vec3 nn = normalize(Vec3{n.x, n.y, n.z});
        verts[i] = static_cast<float>(p.x);
        verts[i + 1] = static_cast<float>(p.y);
        verts[i + 2] = static_cast<float>(p.z);
        verts[i + 3] = static_cast<float>(nn.x);
        verts[i + 4] = static_cast<float>(nn.y);
        verts[i + 5] = static_cast<float>(nn.z);
    }
}

}

Mat4 yUpToZUp() {
    Mat4 m;
    m.at(0, 0) = 1;
    m.at(2, 1) = 1;
    m.at(1, 2) = -1;
    m.at(3, 3) = 1;
    return m;
}

bool parseGltf(const std::uint8_t* bytes, std::size_t size, const std::string& path, const ResourceReader& read, CpuModel& out) {
    cgltf_options options{};
    options.file.read = readResource;
    options.file.release = releaseResource;
    options.file.user_data = const_cast<ResourceReader*>(&read);
    cgltf_data* data = nullptr;
    if (cgltf_parse(&options, bytes, size, &data) != cgltf_result_success) return false;
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i)
        if (!codecSupported(data->extensions_required[i])) {
            cgltf_free(data);
            return false;
        }
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success || !decodeMeshopt(data)) {
        cgltf_free(data);
        return false;
    }
    for (cgltf_size i = 0; i < data->data_extensions_count; ++i) {
        if (data->data_extensions[i].name && std::strcmp(data->data_extensions[i].name, "CESIUM_RTC") == 0 && data->data_extensions[i].data) {
            nlohmann::json j = nlohmann::json::parse(data->data_extensions[i].data, nullptr, false);
            if (j.contains("center") && j["center"].is_array() && j["center"].size() == 3)
                for (int k = 0; k < 3; ++k) out.rtc[k] = j["center"][k].get<double>();
        }
    }
    std::map<const cgltf_image*, int> images;
    std::map<const cgltf_material*, int> materials;
    if (data->asset.copyright) out.copyright = data->asset.copyright;
    std::vector<double> pointPositions;
    std::vector<std::uint8_t> pointColors;
    const Mat4 yUp = yUpToZUp();
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        float world[16];
        cgltf_node_transform_world(node, world);
        int instanceSet = -1;
        if (node->has_mesh_gpu_instancing) {
            CpuInstances set;
            set.local = true;
            if (gpuInstances(node, set.matrices)) {
                instanceSet = static_cast<int>(out.instances.size());
                out.instances.push_back(std::move(set));
            }
        }
        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const cgltf_primitive& prim = node->mesh->primitives[pi];
            const bool triangles = prim.type == cgltf_primitive_type_triangles || prim.type == cgltf_primitive_type_triangle_strip ||
                                   prim.type == cgltf_primitive_type_triangle_fan;
            if (!triangles && prim.type != cgltf_primitive_type_points) continue;
            std::vector<float> pos, nrm, tex, col;
            std::vector<std::uint32_t> idx;
            if (prim.has_draco_mesh_compression) {
                if (!decodeDracoPrimitive(data, prim, pos, nrm, tex, col, idx)) continue;
            } else {
                const cgltf_accessor *ap = nullptr, *an = nullptr, *at = nullptr, *ac = nullptr;
                for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                    const cgltf_attribute& a = prim.attributes[ai];
                    if (a.type == cgltf_attribute_type_position) ap = a.data;
                    else if (a.type == cgltf_attribute_type_normal) an = a.data;
                    else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) at = a.data;
                    else if (a.type == cgltf_attribute_type_color && a.index == 0) ac = a.data;
                }
                if (!ap) continue;
                const size_t n = ap->count;
                pos.resize(n * 3);
                for (size_t i = 0; i < n; ++i) cgltf_accessor_read_float(ap, i, &pos[i * 3], 3);
                if (an) {
                    nrm.resize(n * 3);
                    for (size_t i = 0; i < n; ++i) cgltf_accessor_read_float(an, i, &nrm[i * 3], 3);
                }
                if (at) {
                    tex.resize(n * 2);
                    for (size_t i = 0; i < n; ++i) cgltf_accessor_read_float(at, i, &tex[i * 2], 2);
                }
                if (ac) {
                    col.assign(n * 4, 1.0f);
                    const cgltf_size comps = cgltf_num_components(ac->type);
                    for (size_t i = 0; i < n; ++i) cgltf_accessor_read_float(ac, i, &col[i * 4], comps);
                }
                if (prim.indices) {
                    idx.resize(prim.indices->count);
                    for (size_t i = 0; i < idx.size(); ++i) idx[i] = static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, i));
                } else if (triangles) {
                    idx.resize(n);
                    for (size_t i = 0; i < n; ++i) idx[i] = static_cast<std::uint32_t>(i);
                }
            }
            const size_t n = pos.size() / 3;
            std::array<float, 4> base{1, 1, 1, 1};
            if (prim.material)
                for (int k = 0; k < 4; ++k) base[k] = prim.material->pbr_metallic_roughness.base_color_factor[k];
            if (!triangles) {
                Mat4 m = yUp * fromFloats(world);
                for (size_t i = 0; i < n; ++i) {
                    Vec4 q = m * Vec4{pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2], 1.0};
                    pointPositions.insert(pointPositions.end(), {q.x, q.y, q.z});
                    for (int k = 0; k < 4; ++k) {
                        float v = base[k] * (col.size() >= (i + 1) * 4 ? col[i * 4 + k] : 1.0f);
                        pointColors.push_back(static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f));
                    }
                }
                continue;
            }
            CpuPrimitive p;
            p.vertices.assign(n * 8, 0.0f);
            for (size_t i = 0; i < n; ++i) {
                std::memcpy(&p.vertices[i * 8], &pos[i * 3], 12);
                if (nrm.size() >= (i + 1) * 3) std::memcpy(&p.vertices[i * 8 + 3], &nrm[i * 3], 12);
                if (tex.size() >= (i + 1) * 2) std::memcpy(&p.vertices[i * 8 + 6], &tex[i * 2], 8);
            }
            p.indices = toTriangleList(prim.type, idx);
            p.indices.erase(std::remove_if(p.indices.begin(), p.indices.end(), [n](std::uint32_t v) { return v >= n; }), p.indices.end());
            p.indices.resize(p.indices.size() / 3 * 3);
            if (p.indices.empty()) continue;
            p.material = decodeMaterial(prim.material, options, path, read, out, images, materials);
            if (p.material >= 0) {
                const cgltf_texture_view& tv = prim.material->pbr_metallic_roughness.base_color_texture;
                if (out.materials[p.material].image >= 0 && tv.has_transform) applyTextureTransform(p.vertices, tv.transform);
            }
            for (int k = 0; k < 16; ++k) p.matrix[k] = world[k];
            p.instances = instanceSet;
            out.primitives.push_back(std::move(p));
        }
    }
    cgltf_free(data);
    if (!pointPositions.empty()) {
        size_t n = pointPositions.size() / 3;
        for (int k = 0; k < 3; ++k) out.pointsOrigin[k] = pointPositions[k];
        out.points.resize(n * 16);
        for (size_t i = 0; i < n; ++i) {
            float f[3];
            for (int k = 0; k < 3; ++k) f[k] = static_cast<float>(pointPositions[i * 3 + k] - out.pointsOrigin[k]);
            std::memcpy(&out.points[i * 16], f, 12);
            std::memcpy(&out.points[i * 16 + 12], &pointColors[i * 4], 4);
        }
    }
    mergePrimitives(out);
    finishModel(out);
    return !out.primitives.empty() || !out.points.empty();
}

void mergePrimitives(CpuModel& model) {
    using Key = std::tuple<int, int>;
    std::vector<CpuPrimitive> merged;
    std::map<Key, size_t> open;
    for (CpuPrimitive& p : model.primitives) {
        Key key{p.material, p.instances};
        auto it = open.find(key);
        bool fits = it != open.end() && (merged[it->second].vertices.size() + p.vertices.size()) / 8 <= kMaxMergedVertices;
        if (fits && merged[it->second].matrix != p.matrix) {
            bool local = p.instances >= 0 && p.instances < static_cast<int>(model.instances.size()) && model.instances[p.instances].local;
            Mat4 a, b, inv;
            a.m = merged[it->second].matrix;
            b.m = p.matrix;
            if (local || !invert(a, inv)) fits = false;
            else transformVertices(p.vertices, inv * b);
        }
        if (!fits) {
            open[key] = merged.size();
            merged.push_back(std::move(p));
            continue;
        }
        CpuPrimitive& dst = merged[it->second];
        const std::uint32_t base = static_cast<std::uint32_t>(dst.vertices.size() / 8);
        dst.vertices.insert(dst.vertices.end(), p.vertices.begin(), p.vertices.end());
        dst.indices.reserve(dst.indices.size() + p.indices.size());
        for (std::uint32_t i : p.indices) dst.indices.push_back(i + base);
    }
    model.primitives = std::move(merged);
}

void finishModel(CpuModel& model) {
    size_t bytes = model.points.size();
    for (const CpuPrimitive& p : model.primitives) bytes += p.vertices.size() * 4 + p.indices.size() * 4;
    for (const CpuImage& img : model.images)
        for (const auto& l : img.levels) bytes += l.size();
    for (const CpuInstances& set : model.instances) bytes += set.matrices.size() * 4;
    model.bytes = bytes;
}

GpuMesh::~GpuMesh() {
    if (!sg_isvalid()) return;
    for (auto& p : primitives) {
        sg_destroy_buffer(p.vertices);
        sg_destroy_buffer(p.indices);
    }
    for (sg_view v : views) sg_destroy_view(v);
    for (sg_image i : images) sg_destroy_image(i);
    for (auto& set : instances) sg_destroy_buffer(set.buffer);
    if (points.id) sg_destroy_buffer(points);
}

std::shared_ptr<GpuMesh> ModelLayer::upload(const CpuModel& model) {
    auto mesh = std::make_shared<GpuMesh>();
    mesh->rtc = model.rtc;
    mesh->materials = model.materials;
    mesh->copyright = model.copyright;
    mesh->pointsOrigin = model.pointsOrigin;
    mesh->bytes = model.bytes;
    for (const CpuImage& img : model.images) {
        sg_image_desc d{};
        d.width = img.width;
        d.height = img.height;
        d.pixel_format = static_cast<sg_pixel_format>(img.format);
        d.num_mipmaps = static_cast<int>(std::min<size_t>(img.levels.size(), kMaxMips));
        for (int l = 0; l < d.num_mipmaps; ++l) d.data.mip_levels[l] = {img.levels[l].data(), img.levels[l].size()};
        sg_image image = sg_make_image(&d);
        sg_view_desc tv{};
        tv.texture.image = image;
        mesh->images.push_back(image);
        mesh->views.push_back(sg_make_view(&tv));
    }
    for (const CpuInstances& set : model.instances) {
        GpuMesh::Instances g;
        g.count = static_cast<int>(set.matrices.size() / 16);
        g.local = set.local;
        if (g.count > 0) {
            sg_buffer_desc d{};
            d.usage.vertex_buffer = true;
            d.data = {set.matrices.data(), set.matrices.size() * sizeof(float)};
            g.buffer = sg_make_buffer(&d);
        }
        mesh->instances.push_back(g);
    }
    if (!model.points.empty()) {
        sg_buffer_desc d{};
        d.usage.vertex_buffer = true;
        d.data = {model.points.data(), model.points.size()};
        mesh->points = sg_make_buffer(&d);
        mesh->pointCount = static_cast<int>(model.points.size() / 16);
    }
    for (const CpuPrimitive& c : model.primitives) {
        GpuMesh::Primitive p;
        sg_buffer_desc vb{};
        vb.usage.vertex_buffer = true;
        vb.data = {c.vertices.data(), c.vertices.size() * sizeof(float)};
        p.vertices = sg_make_buffer(&vb);
        sg_buffer_desc ib{};
        ib.usage.index_buffer = true;
        ib.data = {c.indices.data(), c.indices.size() * sizeof(std::uint32_t)};
        p.indices = sg_make_buffer(&ib);
        p.count = static_cast<int>(c.indices.size());
        p.matrix = c.matrix;
        p.material = c.material;
        p.instances = c.instances;
        mesh->primitives.push_back(p);
    }
    return mesh;
}

bool ModelLayer::init() {
    if (ready_) return true;
    shader_ = sg_make_shader(model_shader_desc(sg_query_backend()));
    sg_pipeline_desc p{};
    p.shader = shader_;
    p.layout.attrs[ATTR_model_position].format = SG_VERTEXFORMAT_FLOAT3;
    p.layout.attrs[ATTR_model_normal].format = SG_VERTEXFORMAT_FLOAT3;
    p.layout.attrs[ATTR_model_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
    p.index_type = SG_INDEXTYPE_UINT32;
    p.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    p.depth.write_enabled = true;
    p.cull_mode = SG_CULLMODE_NONE;
    p.colors[0].blend.enabled = true;
    p.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    p.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    pipeline_ = sg_make_pipeline(&p);
    instShader_ = sg_make_shader(model_inst_shader_desc(sg_query_backend()));
    sg_pipeline_desc ip = p;
    ip.shader = instShader_;
    ip.layout = {};
    ip.layout.buffers[0].stride = 32;
    ip.layout.buffers[1].stride = 64;
    ip.layout.buffers[1].step_func = SG_VERTEXSTEP_PER_INSTANCE;
    ip.layout.attrs[ATTR_model_inst_position] = {0, 0, SG_VERTEXFORMAT_FLOAT3};
    ip.layout.attrs[ATTR_model_inst_normal] = {0, 12, SG_VERTEXFORMAT_FLOAT3};
    ip.layout.attrs[ATTR_model_inst_texcoord0] = {0, 24, SG_VERTEXFORMAT_FLOAT2};
    ip.layout.attrs[ATTR_model_inst_inst0] = {1, 0, SG_VERTEXFORMAT_FLOAT4};
    ip.layout.attrs[ATTR_model_inst_inst1] = {1, 16, SG_VERTEXFORMAT_FLOAT4};
    ip.layout.attrs[ATTR_model_inst_inst2] = {1, 32, SG_VERTEXFORMAT_FLOAT4};
    ip.layout.attrs[ATTR_model_inst_inst3] = {1, 48, SG_VERTEXFORMAT_FLOAT4};
    instPipeline_ = sg_make_pipeline(&ip);
    sg_sampler_desc s{};
    s.min_filter = SG_FILTER_LINEAR;
    s.mag_filter = SG_FILTER_LINEAR;
    s.mipmap_filter = SG_FILTER_LINEAR;
    s.max_anisotropy = 4;
    s.wrap_u = SG_WRAP_REPEAT;
    s.wrap_v = SG_WRAP_REPEAT;
    sampler_ = sg_make_sampler(&s);
    const std::uint8_t white[4] = {255, 255, 255, 255};
    sg_image_desc wd{};
    wd.width = wd.height = 1;
    wd.pixel_format = SG_PIXELFORMAT_RGBA8;
    wd.data.mip_levels[0] = SG_RANGE(white);
    white_ = sg_make_image(&wd);
    sg_view_desc wv{};
    wv.texture.image = white_;
    whiteView_ = sg_make_view(&wv);
    const std::uint8_t black[4] = {0, 0, 0, 255};
    wd.data.mip_levels[0] = SG_RANGE(black);
    black_ = sg_make_image(&wd);
    wv.texture.image = black_;
    blackView_ = sg_make_view(&wv);
    const std::uint8_t flat[4] = {128, 128, 255, 255};
    wd.data.mip_levels[0] = SG_RANGE(flat);
    flat_ = sg_make_image(&wd);
    wv.texture.image = flat_;
    flatView_ = sg_make_view(&wv);
    auto usable = [](sg_pixel_format f) {
        sg_pixelformat_info i = sg_query_pixelformat(f);
        return i.sample && i.filter;
    };
    setCompressedTextureSupport(usable(SG_PIXELFORMAT_BC7_RGBA), usable(SG_PIXELFORMAT_ASTC_4x4_RGBA), usable(SG_PIXELFORMAT_ETC2_RGBA8));
    ready_ = true;
    return true;
}

void ModelLayer::shutdown() {
    if (!ready_) return;
    instances_.clear();
    meshes_.clear();
    if (sg_isvalid()) {
        sg_destroy_pipeline(pipeline_);
        sg_destroy_shader(shader_);
        sg_destroy_pipeline(instPipeline_);
        sg_destroy_shader(instShader_);
        sg_destroy_sampler(sampler_);
        sg_destroy_view(whiteView_);
        sg_destroy_image(white_);
        sg_destroy_view(blackView_);
        sg_destroy_image(black_);
        sg_destroy_view(flatView_);
        sg_destroy_image(flat_);
        if (frameInstances_.id) sg_destroy_buffer(frameInstances_);
    }
    frameInstances_ = {};
    frameInstanceBytes_ = 0;
    ready_ = false;
}

std::shared_ptr<GpuMesh> ModelLayer::load(const std::string& path) {
    auto cached = meshes_.find(path);
    if (cached != meshes_.end())
        if (auto m = cached->second.lock()) return m;
    std::filesystem::path p = std::filesystem::u8path(path);
    std::vector<std::uint8_t> bytes = readFile(p);
    if (bytes.empty()) return nullptr;
    CpuModel cpu;
    ResourceReader read = [](const std::string& f) { return readFile(std::filesystem::u8path(f)); };
    if (!parseGltf(bytes.data(), bytes.size(), path, read, cpu) || cpu.primitives.empty()) return nullptr;
    auto mesh = upload(cpu);
    meshes_[path] = mesh;
    return mesh;
}

std::uint64_t ModelLayer::add(const std::string& path, const ModelPlacement& placement) {
    auto mesh = load(path);
    if (!mesh) return 0;
    std::uint64_t id = nextId_++;
    instances_[id] = {mesh, placement};
    return id;
}

std::uint64_t ModelLayer::add(std::shared_ptr<GpuMesh> mesh, const ModelPlacement& placement) {
    if (!mesh) return 0;
    std::uint64_t id = nextId_++;
    instances_[id] = {std::move(mesh), placement};
    return id;
}

bool ModelLayer::update(std::uint64_t id, const ModelPlacement& placement) {
    auto it = instances_.find(id);
    if (it == instances_.end()) return false;
    it->second.placement = placement;
    return true;
}

bool ModelLayer::remove(std::uint64_t id) { return instances_.erase(id) > 0; }

void ModelLayer::begin() { current_ = -1; }

void ModelLayer::usePipeline(bool instanced) {
    int want = instanced ? 1 : 0;
    if (current_ == want) return;
    sg_apply_pipeline(instanced ? instPipeline_ : pipeline_);
    current_ = want;
}

void ModelLayer::setSun(bool enabled, Vec3 direction) {
    sun_ = enabled;
    sunDir_ = normalize(direction);
}

void ModelLayer::drawPrimitive(const Camera& cam, const GpuMesh& mesh, const GpuMesh::Primitive& p, sg_buffer instances, int instanceOffset,
                               int instanceCount, const Mat4& world, const Mat4& node, const Mat4& rotation, Vec3 light, Vec3 up, int lighting) {
    static const CpuMaterial fallback;
    const CpuMaterial& mat = p.material >= 0 && p.material < static_cast<int>(mesh.materials.size()) ? mesh.materials[p.material] : fallback;
    auto view = [&mesh](int image, sg_view otherwise) {
        return image >= 0 && image < static_cast<int>(mesh.views.size()) ? mesh.views[image] : otherwise;
    };
    usePipeline(instanceCount > 0);
    sg_bindings b{};
    b.vertex_buffers[0] = p.vertices;
    if (instanceCount > 0) {
        b.vertex_buffers[1] = instances;
        b.vertex_buffer_offsets[1] = instanceOffset * 64;
    }
    b.index_buffer = p.indices;
    b.views[VIEW_tex] = view(mat.image, whiteView_);
    b.views[VIEW_nrm_tex] = view(mat.normalImage, flatView_);
    b.views[VIEW_emi_tex] = view(mat.emissiveImage, whiteView_);
    b.views[VIEW_orm_tex] = view(mat.ormImage, whiteView_);
    b.samplers[SMP_smp] = sampler_;
    sg_apply_bindings(&b);
    if (instanceCount > 0) {
        model_inst_params_t vs{};
        toFloat(cam.viewProjRel(), vs.view_proj);
        toFloat(world, vs.world);
        toFloat(node, vs.node);
        toFloat(rotation, vs.rotation);
        sg_apply_uniforms(UB_model_inst_params, SG_RANGE(vs));
    } else {
        model_vs_params_t vs{};
        toFloat(cam.viewProjRel(), vs.view_proj);
        toFloat(world, vs.world);
        toFloat(rotation, vs.rotation);
        sg_apply_uniforms(UB_model_vs_params, SG_RANGE(vs));
    }
    const bool unlit = lighting == 2 || (lighting == 0 && mat.unlit);
    float intensity = 0.85f;
    Vec3 l = light;
    if (sun_) {
        l = sunDir_;
        double elevation = std::asin(std::clamp(dot(sunDir_, up), -1.0, 1.0)) * kRadToDeg;
        intensity *= static_cast<float>(std::clamp((elevation + 2.0) / 7.0, 0.0, 1.0));
    }
    model_fs_params_t fs{};
    for (int k = 0; k < 4; ++k) fs.base_color[k] = mat.color[k];
    fs.base_color[3] *= opacity_;
    fs.light[0] = static_cast<float>(l.x);
    fs.light[1] = static_cast<float>(l.y);
    fs.light[2] = static_cast<float>(l.z);
    fs.light[3] = intensity;
    for (int k = 0; k < 3; ++k) fs.emissive[k] = mat.emissive[k];
    fs.emissive[3] = unlit ? 1.0f : 0.0f;
    fs.material[0] = mat.metallic;
    fs.material[1] = mat.roughness;
    fs.material[2] = mat.normalImage >= 0 ? mat.normalScale : 0.0f;
    fs.material[3] = sun_ ? 0.2f : 0.25f;
    fs.sky[0] = static_cast<float>(up.x);
    fs.sky[1] = static_cast<float>(up.y);
    fs.sky[2] = static_cast<float>(up.z);
    fs.sky[3] = mat.ormOcclusion ? 1.0f : 0.0f;
    sg_apply_uniforms(UB_model_fs_params, SG_RANGE(fs));
    sg_draw(0, p.count, instanceCount > 0 ? instanceCount : 1);
}

void ModelLayer::drawMesh(const Camera& cam, const GpuMesh& mesh, const Mat4& model, const Mat4& orientation, Vec3 light, int lighting) {
    const Mat4 yUp = yUpToZUp();
    const Vec4 origin = model * Vec4{0, 0, 0, 1};
    const Vec3 up = normalize(Vec3{origin.x, origin.y, origin.z} + cam.eye());
    for (const GpuMesh::Primitive& p : mesh.primitives) {
        Mat4 node;
        node.m = p.matrix;
        if (p.instances < 0 || p.instances >= static_cast<int>(mesh.instances.size()) || mesh.instances[p.instances].count == 0) {
            drawPrimitive(cam, mesh, p, {}, 0, 0, model * yUp * node, Mat4::identity(), orientation * yUp * node, light, up, lighting);
            continue;
        }
        const GpuMesh::Instances& set = mesh.instances[p.instances];
        if (set.local)
            drawPrimitive(cam, mesh, p, set.buffer, 0, set.count, model * yUp * node, Mat4::identity(), orientation * yUp * node, light, up, lighting);
        else drawPrimitive(cam, mesh, p, set.buffer, 0, set.count, model, node, orientation, light, up, lighting);
    }
}

void ModelLayer::draw(const Camera& cam, const GroundFn& ground, bool reuse) {
    if (!ready_ || instances_.empty()) return;
    const Mat4 yUp = yUpToZUp();
    if (!reuse || placed_.empty()) {
        frameOrigin_ = cam.eye();
        placed_.clear();
        placed_.reserve(instances_.size());
        for (auto& [id, inst] : instances_) {
            const ModelPlacement& pl = inst.placement;
            double h = pl.position.alt;
            if (pl.altitudeMode == AltitudeMode::ClampToGround) h = ground(pl.position.lat, pl.position.lon);
            else if (pl.altitudeMode == AltitudeMode::RelativeToGround) h += ground(pl.position.lat, pl.position.lon);
            Vec3 origin = geodeticToEcef(pl.position.lat, pl.position.lon, h);
            Vec3 e, n, u;
            enuFrame(pl.position.lat, pl.position.lon, e, n, u);
            Mat4 frame = Mat4::identity();
            frame.at(0, 0) = e.x; frame.at(1, 0) = e.y; frame.at(2, 0) = e.z;
            frame.at(0, 1) = n.x; frame.at(1, 1) = n.y; frame.at(2, 1) = n.z;
            frame.at(0, 2) = u.x; frame.at(1, 2) = u.y; frame.at(2, 2) = u.z;
            Mat4 orient = frame * rotZ(-pl.heading * kDegToRad) * rotX(pl.pitch * kDegToRad) * rotY(pl.roll * kDegToRad);
            Mat4 base = translation(origin - frameOrigin_) * orient * scaleMat(pl.scale);
            if (!(pl.opacity > 0.0f)) continue;
            placed_.push_back({inst.mesh.get(), base, orient, normalize(normalize(cam.eye() - origin) + u * 0.8), u, std::min(pl.opacity, 1.0f)});
        }
        std::stable_sort(placed_.begin(), placed_.end(),
                         [](const Placed& a, const Placed& b) { return a.mesh != b.mesh ? a.mesh < b.mesh : a.opacity > b.opacity; });
        batches_.clear();
        std::vector<float> matrices;
        for (size_t i = 0; i < placed_.size();) {
            size_t j = i;
            while (j < placed_.size() && placed_[j].mesh == placed_[i].mesh && placed_[j].opacity == placed_[i].opacity) ++j;
            if (j - i >= 2 && placed_[i].mesh->instances.empty()) {
                batches_.push_back({i, j - i});
                for (size_t k = i; k < j; ++k) {
                    float m[16];
                    toFloat(placed_[k].base * yUp, m);
                    matrices.insert(matrices.end(), m, m + 16);
                }
            }
            i = j;
        }
        if (!matrices.empty()) {
            size_t bytes = matrices.size() * sizeof(float);
            if (bytes > frameInstanceBytes_) {
                if (frameInstances_.id) sg_destroy_buffer(frameInstances_);
                frameInstanceBytes_ = std::max(bytes, frameInstanceBytes_ * 2);
                sg_buffer_desc d{};
                d.usage.vertex_buffer = true;
                d.usage.write_transient = true;
                d.size = frameInstanceBytes_;
                frameInstances_ = sg_make_buffer(&d);
            }
            sg_write_buffer_desc wd{};
            wd.src.data = {matrices.data(), bytes};
            wd.dst.buffer = frameInstances_;
            wd.size = bytes;
            sg_write_buffer_transient(&wd);
        }
    }
    begin();
    const Mat4 shift = translation(frameOrigin_ - cam.eye());
    size_t batch = 0, offset = 0;
    for (size_t i = 0; i < placed_.size();) {
        if (batch < batches_.size() && batches_[batch].first == i) {
            const Placed& first = placed_[i];
            opacity_ = first.opacity;
            for (const GpuMesh::Primitive& p : first.mesh->primitives) {
                Mat4 node;
                node.m = p.matrix;
                drawPrimitive(cam, *first.mesh, p, frameInstances_, static_cast<int>(offset), static_cast<int>(batches_[batch].count), shift, node,
                              Mat4::identity(), first.light, first.up, 0);
            }
            offset += batches_[batch].count;
            i += batches_[batch].count;
            ++batch;
            continue;
        }
        opacity_ = placed_[i].opacity;
        drawMesh(cam, *placed_[i].mesh, shift * placed_[i].base, placed_[i].orient, placed_[i].light);
        ++i;
    }
    opacity_ = 1.0f;
}

}
