#include "model/gltf_codecs.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

#if defined(OTE_WITH_MESHOPT)
#include <meshoptimizer.h>
#endif
#if defined(OTE_WITH_DRACO)
#include <draco/compression/decode.h>
#endif
#if defined(OTE_WITH_KTX2)
#include <basisu_transcoder.h>
#endif

namespace ote {

namespace {

std::atomic<int> gTextureSupport{0};

constexpr int kBc7 = 1;
constexpr int kAstc = 2;
constexpr int kEtc2 = 4;

#if defined(OTE_WITH_DRACO)
template <typename T>
bool readAttribute(const draco::PointCloud& pc, int id, int components, std::vector<T>& out, int* stored = nullptr) {
    if (id < 0) return false;
    const draco::PointAttribute* a = pc.GetAttributeByUniqueId(static_cast<std::uint32_t>(id));
    if (!a) return false;
    if (stored) *stored = a->num_components();
    const std::uint32_t n = pc.num_points();
    out.assign(static_cast<size_t>(n) * components, T(0));
    for (std::uint32_t i = 0; i < n; ++i)
        if (!a->ConvertValue<T>(a->mapped_index(draco::PointIndex(i)), static_cast<std::int8_t>(components), &out[static_cast<size_t>(i) * components]))
            return false;
    return true;
}
#endif

}

bool codecSupported(const char* extension) {
    if (!extension) return true;
    if (std::strcmp(extension, "KHR_draco_mesh_compression") == 0) {
#if defined(OTE_WITH_DRACO)
        return true;
#else
        return false;
#endif
    }
    if (std::strcmp(extension, "EXT_meshopt_compression") == 0) {
#if defined(OTE_WITH_MESHOPT)
        return true;
#else
        return false;
#endif
    }
    if (std::strcmp(extension, "KHR_texture_basisu") == 0) {
#if defined(OTE_WITH_KTX2)
        return true;
#else
        return false;
#endif
    }
    for (const char* missing : {"KHR_meshopt_compression", "EXT_texture_webp", "EXT_texture_avif"})
        if (std::strcmp(extension, missing) == 0) return false;
    return true;
}

bool decodeMeshopt(cgltf_data* data) {
    for (cgltf_size i = 0; i < data->buffer_views_count; ++i) {
        cgltf_buffer_view& bv = data->buffer_views[i];
        if (!bv.has_meshopt_compression || bv.data) continue;
#if defined(OTE_WITH_MESHOPT)
        const cgltf_meshopt_compression& mc = bv.meshopt_compression;
        if (!mc.buffer || !mc.buffer->data || mc.offset + mc.size > mc.buffer->size) return false;
        const unsigned char* src = static_cast<const unsigned char*>(mc.buffer->data) + mc.offset;
        size_t bytes = mc.count * mc.stride;
        void* dst = data->memory.alloc_func(data->memory.user_data, bytes ? bytes : 1);
        if (!dst) return false;
        int rc = -1;
        if (mc.mode == cgltf_meshopt_compression_mode_attributes) rc = meshopt_decodeVertexBuffer(dst, mc.count, mc.stride, src, mc.size);
        else if (mc.mode == cgltf_meshopt_compression_mode_triangles) rc = meshopt_decodeIndexBuffer(dst, mc.count, mc.stride, src, mc.size);
        else if (mc.mode == cgltf_meshopt_compression_mode_indices) rc = meshopt_decodeIndexSequence(dst, mc.count, mc.stride, src, mc.size);
        if (rc != 0) {
            data->memory.free_func(data->memory.user_data, dst);
            return false;
        }
        if (mc.filter == cgltf_meshopt_compression_filter_octahedral) meshopt_decodeFilterOct(dst, mc.count, mc.stride);
        else if (mc.filter == cgltf_meshopt_compression_filter_quaternion) meshopt_decodeFilterQuat(dst, mc.count, mc.stride);
        else if (mc.filter == cgltf_meshopt_compression_filter_exponential) meshopt_decodeFilterExp(dst, mc.count, mc.stride);
        bv.data = dst;
#else
        return false;
#endif
    }
    return true;
}

bool decodeDracoPrimitive(const cgltf_data* data, const cgltf_primitive& prim, std::vector<float>& positions, std::vector<float>& normals,
                          std::vector<float>& texcoords, std::vector<float>& colors, std::vector<std::uint32_t>& indices) {
#if defined(OTE_WITH_DRACO)
    const cgltf_draco_mesh_compression& dc = prim.draco_mesh_compression;
    const cgltf_buffer_view* bv = dc.buffer_view;
    if (!bv) return false;
    const char* src = nullptr;
    if (bv->data) src = static_cast<const char*>(bv->data);
    else if (bv->buffer && bv->buffer->data && bv->offset + bv->size <= bv->buffer->size)
        src = static_cast<const char*>(bv->buffer->data) + bv->offset;
    if (!src) return false;
    draco::DecoderBuffer buffer;
    buffer.Init(src, bv->size);
    draco::Decoder decoder;
    auto decoded = decoder.DecodeMeshFromBuffer(&buffer);
    if (!decoded.ok()) return false;
    std::unique_ptr<draco::Mesh> mesh = std::move(decoded).value();
    int pos = -1, nrm = -1, tex = -1, col = -1;
    for (cgltf_size i = 0; i < dc.attributes_count; ++i) {
        const cgltf_attribute& a = dc.attributes[i];
        int id = static_cast<int>(a.data - data->accessors);
        if (a.type == cgltf_attribute_type_position) pos = id;
        else if (a.type == cgltf_attribute_type_normal) nrm = id;
        else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) tex = id;
        else if (a.type == cgltf_attribute_type_color && a.index == 0) col = id;
    }
    if (!readAttribute(*mesh, pos, 3, positions)) return false;
    if (!readAttribute(*mesh, nrm, 3, normals)) normals.clear();
    if (!readAttribute(*mesh, tex, 2, texcoords)) texcoords.clear();
    int colorComponents = 4;
    if (!readAttribute(*mesh, col, 4, colors, &colorComponents)) colors.clear();
    if (colorComponents == 3)
        for (size_t i = 3; i < colors.size(); i += 4) colors[i] = 1.0f;
    indices.resize(static_cast<size_t>(mesh->num_faces()) * 3);
    for (std::uint32_t f = 0; f < mesh->num_faces(); ++f) {
        const draco::Mesh::Face& face = mesh->face(draco::FaceIndex(f));
        for (int k = 0; k < 3; ++k) indices[static_cast<size_t>(f) * 3 + k] = face[k].value();
    }
    return true;
#else
    (void)data;
    (void)prim;
    (void)positions;
    (void)normals;
    (void)texcoords;
    (void)colors;
    (void)indices;
    return false;
#endif
}

bool decodeDracoPoints(const std::uint8_t* data, std::size_t size, const std::map<std::string, int>& properties, std::vector<float>& positions,
                       std::vector<std::uint8_t>& rgba) {
#if defined(OTE_WITH_DRACO)
    draco::DecoderBuffer buffer;
    buffer.Init(reinterpret_cast<const char*>(data), size);
    draco::Decoder decoder;
    auto decoded = decoder.DecodePointCloudFromBuffer(&buffer);
    if (!decoded.ok()) return false;
    std::unique_ptr<draco::PointCloud> pc = std::move(decoded).value();
    auto id = [&](const char* name) {
        auto it = properties.find(name);
        return it == properties.end() ? -1 : it->second;
    };
    if (!readAttribute(*pc, id("POSITION"), 3, positions)) return false;
    size_t n = positions.size() / 3;
    std::vector<std::uint8_t> c;
    if (readAttribute(*pc, id("RGBA"), 4, c)) {
        rgba = std::move(c);
    } else if (readAttribute(*pc, id("RGB"), 3, c)) {
        rgba.resize(n * 4);
        for (size_t i = 0; i < n; ++i) {
            for (int k = 0; k < 3; ++k) rgba[i * 4 + k] = c[i * 3 + k];
            rgba[i * 4 + 3] = 255;
        }
    } else {
        rgba.clear();
    }
    return true;
#else
    (void)data;
    (void)size;
    (void)properties;
    (void)positions;
    (void)rgba;
    return false;
#endif
}

void setCompressedTextureSupport(bool bc7, bool astc, bool etc2) {
    gTextureSupport = (bc7 ? kBc7 : 0) | (astc ? kAstc : 0) | (etc2 ? kEtc2 : 0);
}

bool transcodeKtx2(const std::uint8_t* data, std::size_t size, CpuImage& out) {
#if defined(OTE_WITH_KTX2)
    static std::once_flag once;
    std::call_once(once, [] { basist::basisu_transcoder_init(); });
    basist::ktx2_transcoder t;
    if (!t.init(data, static_cast<std::uint32_t>(size)) || t.is_hdr() || !t.start_transcoding()) return false;
    const std::uint32_t w = t.get_width(), h = t.get_height();
    if (w == 0 || h == 0) return false;
    struct Target {
        basist::transcoder_texture_format fmt = basist::transcoder_texture_format::cTFRGBA32;
        int pixel = SG_PIXELFORMAT_RGBA8;
        bool block = false;
    };
    std::vector<Target> targets;
    int support = gTextureSupport.load();
    bool aligned = w % 4 == 0 && h % 4 == 0;
    if (aligned && (support & kBc7)) targets.push_back({basist::transcoder_texture_format::cTFBC7_RGBA, SG_PIXELFORMAT_BC7_RGBA, true});
    if (aligned && (support & kAstc)) targets.push_back({basist::transcoder_texture_format::cTFASTC_4x4_RGBA, SG_PIXELFORMAT_ASTC_4x4_RGBA, true});
    if (aligned && (support & kEtc2)) targets.push_back({basist::transcoder_texture_format::cTFETC2_RGBA, SG_PIXELFORMAT_ETC2_RGBA8, true});
    targets.push_back({basist::transcoder_texture_format::cTFRGBA32, SG_PIXELFORMAT_RGBA8, false});
    for (const Target& target : targets) {
        CpuImage img;
        img.width = static_cast<int>(w);
        img.height = static_cast<int>(h);
        img.format = target.pixel;
        const std::uint32_t perUnit = basist::basis_get_bytes_per_block_or_pixel(target.fmt);
        bool ok = true;
        for (std::uint32_t level = 0; level < t.get_levels(); ++level) {
            const std::uint32_t lw = std::max(1u, w >> level), lh = std::max(1u, h >> level);
            basist::ktx2_image_level_info info{};
            if (!t.get_image_level_info(info, level, 0, 0) || info.m_orig_width != lw || info.m_orig_height != lh) break;
            const std::uint32_t units = target.block ? ((lw + 3) / 4) * ((lh + 3) / 4) : lw * lh;
            std::vector<std::uint8_t> bytes(static_cast<size_t>(units) * perUnit);
            if (!t.transcode_image_level(level, 0, 0, bytes.data(), units, target.fmt, 0, 0, target.block ? 0 : lh)) {
                ok = level > 0;
                break;
            }
            img.levels.push_back(std::move(bytes));
        }
        if (ok && !img.levels.empty()) {
            out = std::move(img);
            return true;
        }
    }
    return false;
#else
    (void)data;
    (void)size;
    (void)out;
    return false;
#endif
}

}
