#include "model/model_layer.hpp"

#include "model.glsl.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <json.hpp>
#include <stb_image.h>

#include <cmath>
#include <cstring>
#include <filesystem>

namespace ote {

namespace {

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

bool decodeImage(const cgltf_image* img, const std::string& baseDir, CpuPrimitive& p) {
    std::vector<unsigned char> bytes;
    if (img->buffer_view && img->buffer_view->buffer->data) {
        const auto* b = static_cast<const unsigned char*>(img->buffer_view->buffer->data) + img->buffer_view->offset;
        bytes.assign(b, b + img->buffer_view->size);
    } else if (img->uri && std::strncmp(img->uri, "data:", 5) != 0 && !baseDir.empty()) {
        bytes = readFile(std::filesystem::u8path(baseDir) / std::filesystem::u8path(img->uri));
    }
    if (bytes.empty()) return false;
    int w = 0, h = 0, c = 0;
    unsigned char* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &c, 4);
    if (!px) return false;
    p.imageWidth = w;
    p.imageHeight = h;
    p.image.assign(px, px + static_cast<size_t>(w) * h * 4);
    stbi_image_free(px);
    return true;
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

bool parseGltf(const std::uint8_t* bytes, std::size_t size, const std::string& baseDir, CpuModel& out) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse(&options, bytes, size, &data) != cgltf_result_success) return false;
    for (cgltf_size i = 0; i < data->extensions_required_count; ++i) {
        const char* e = data->extensions_required[i];
        for (const char* missing : {"KHR_draco_mesh_compression", "EXT_meshopt_compression", "KHR_meshopt_compression", "KHR_texture_basisu",
                                    "EXT_texture_webp", "EXT_texture_avif"})
            if (e && std::strcmp(e, missing) == 0) {
                cgltf_free(data);
                return false;
            }
    }
    std::string base = baseDir.empty() ? std::string() : (std::filesystem::u8path(baseDir) / "x").string();
    if (cgltf_load_buffers(&options, data, base.empty() ? nullptr : base.c_str()) != cgltf_result_success) {
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
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        float world[16];
        cgltf_node_transform_world(node, world);
        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const cgltf_primitive& prim = node->mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr, *tex = nullptr;
            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                const cgltf_attribute& a = prim.attributes[ai];
                if (a.type == cgltf_attribute_type_position) pos = a.data;
                else if (a.type == cgltf_attribute_type_normal) nrm = a.data;
                else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) tex = a.data;
            }
            if (!pos) continue;
            CpuPrimitive p;
            size_t n = pos->count;
            p.vertices.assign(n * 8, 0.0f);
            for (size_t i = 0; i < n; ++i) {
                cgltf_accessor_read_float(pos, i, &p.vertices[i * 8], 3);
                if (nrm) cgltf_accessor_read_float(nrm, i, &p.vertices[i * 8 + 3], 3);
                else p.vertices[i * 8 + 4] = 1.0f;
                if (tex) cgltf_accessor_read_float(tex, i, &p.vertices[i * 8 + 6], 2);
            }
            if (prim.indices) {
                p.indices.resize(prim.indices->count);
                for (size_t i = 0; i < p.indices.size(); ++i)
                    p.indices[i] = static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, i));
            } else {
                p.indices.resize(n);
                for (size_t i = 0; i < n; ++i) p.indices[i] = static_cast<std::uint32_t>(i);
            }
            if (prim.material) {
                const auto& pbr = prim.material->pbr_metallic_roughness;
                for (int k = 0; k < 4; ++k) p.color[k] = pbr.base_color_factor[k];
                if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image)
                    decodeImage(pbr.base_color_texture.texture->image, baseDir, p);
            }
            for (int k = 0; k < 16; ++k) p.matrix[k] = world[k];
            out.bytes += p.vertices.size() * 4 + p.indices.size() * 4 + p.image.size();
            out.primitives.push_back(std::move(p));
        }
    }
    cgltf_free(data);
    return !out.primitives.empty();
}

bool parseB3dm(const std::uint8_t* data, std::size_t size, CpuModel& out) {
    if (size < 28 || std::memcmp(data, "b3dm", 4) != 0) {
        if (size >= 4 && std::memcmp(data, "glTF", 4) == 0) return parseGltf(data, size, {}, out);
        return false;
    }
    auto u32 = [&](size_t o) {
        std::uint32_t v;
        std::memcpy(&v, data + o, 4);
        return v;
    };
    std::uint32_t ftJson = u32(12), ftBin = u32(16), btJson = u32(20), btBin = u32(24);
    size_t header = 28;
    if (btJson >= 570425344) {
        header = 20;
        btJson = ftBin;
        btBin = ftJson;
        ftJson = 0;
        ftBin = 0;
    }
    size_t glb = header + ftJson + ftBin + btJson + btBin;
    if (glb >= size) return false;
    if (ftJson > 0) {
        std::string json(reinterpret_cast<const char*>(data + header), ftJson);
        nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
        if (!j.is_discarded() && j.contains("RTC_CENTER") && j["RTC_CENTER"].is_array() && j["RTC_CENTER"].size() == 3)
            for (int k = 0; k < 3; ++k) out.rtc[k] = j["RTC_CENTER"][k].get<double>();
    }
    std::array<double, 3> rtc = out.rtc;
    bool ok = parseGltf(data + glb, size - glb, {}, out);
    if (rtc[0] != 0 || rtc[1] != 0 || rtc[2] != 0) out.rtc = rtc;
    return ok;
}

GpuMesh::~GpuMesh() {
    if (!sg_isvalid()) return;
    for (auto& p : primitives) {
        sg_destroy_buffer(p.vertices);
        sg_destroy_buffer(p.indices);
        if (p.image.id) {
            sg_destroy_view(p.texture);
            sg_destroy_image(p.image);
        }
    }
}

std::shared_ptr<GpuMesh> ModelLayer::upload(const CpuModel& model) {
    auto mesh = std::make_shared<GpuMesh>();
    mesh->rtc = model.rtc;
    mesh->bytes = model.bytes;
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
        p.color = c.color;
        p.matrix = c.matrix;
        if (!c.image.empty()) {
            sg_image_desc d{};
            d.width = c.imageWidth;
            d.height = c.imageHeight;
            d.pixel_format = SG_PIXELFORMAT_RGBA8;
            d.data.mip_levels[0] = {c.image.data(), c.image.size()};
            p.image = sg_make_image(&d);
            sg_view_desc tv{};
            tv.texture.image = p.image;
            p.texture = sg_make_view(&tv);
        }
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
    sg_sampler_desc s{};
    s.min_filter = SG_FILTER_LINEAR;
    s.mag_filter = SG_FILTER_LINEAR;
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
        sg_destroy_sampler(sampler_);
        sg_destroy_view(whiteView_);
        sg_destroy_image(white_);
    }
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
    if (!parseGltf(bytes.data(), bytes.size(), p.parent_path().string(), cpu)) return nullptr;
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

bool ModelLayer::update(std::uint64_t id, const ModelPlacement& placement) {
    auto it = instances_.find(id);
    if (it == instances_.end()) return false;
    it->second.placement = placement;
    return true;
}

bool ModelLayer::remove(std::uint64_t id) { return instances_.erase(id) > 0; }

void ModelLayer::begin() { sg_apply_pipeline(pipeline_); }

void ModelLayer::drawMesh(const Camera& cam, const GpuMesh& mesh, const Mat4& model, const Mat4& orientation, Vec3 light) {
    for (const GpuMesh::Primitive& p : mesh.primitives) {
        Mat4 node;
        node.m = p.matrix;
        model_vs_params_t vs{};
        toFloat(cam.viewProjRel() * model * node, vs.mvp);
        toFloat(orientation * node, vs.rotation);
        model_fs_params_t fs{};
        for (int k = 0; k < 4; ++k) fs.base_color[k] = p.color[k];
        fs.light[0] = static_cast<float>(light.x);
        fs.light[1] = static_cast<float>(light.y);
        fs.light[2] = static_cast<float>(light.z);
        sg_bindings b{};
        b.vertex_buffers[0] = p.vertices;
        b.index_buffer = p.indices;
        b.views[VIEW_tex] = p.image.id ? p.texture : whiteView_;
        b.samplers[SMP_smp] = sampler_;
        sg_apply_bindings(&b);
        sg_apply_uniforms(UB_model_vs_params, SG_RANGE(vs));
        sg_apply_uniforms(UB_model_fs_params, SG_RANGE(fs));
        sg_draw(0, p.count, 1);
    }
}

void ModelLayer::draw(const Camera& cam, const GroundFn& ground) {
    if (!ready_ || instances_.empty()) return;
    Mat4 yUp = yUpToZUp();
    begin();
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
        Mat4 base = translation(origin - cam.eye()) * orient * scaleMat(pl.scale) * yUp;
        Vec3 light = normalize(normalize(cam.eye() - origin) + u * 0.8);
        drawMesh(cam, *inst.mesh, base, orient * yUp, light);
    }
}

}
