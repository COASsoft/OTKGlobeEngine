#include "model/model_layer.hpp"

#include "model.glsl.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <stb_image.h>

#include <cmath>
#include <cstring>
#include <filesystem>

namespace ote {

namespace {

Mat4 fromFloat(const float m[16]) {
    Mat4 r;
    for (int i = 0; i < 16; ++i) r.m[i] = m[i];
    return r;
}

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

sg_image decodeImage(const cgltf_image* img, const std::filesystem::path& dir) {
    std::vector<unsigned char> bytes;
    if (img->buffer_view && img->buffer_view->buffer->data) {
        const auto* p = static_cast<const unsigned char*>(img->buffer_view->buffer->data) + img->buffer_view->offset;
        bytes.assign(p, p + img->buffer_view->size);
    } else if (img->uri && std::strncmp(img->uri, "data:", 5) != 0) {
        std::filesystem::path f = dir / std::filesystem::u8path(img->uri);
        FILE* fp = nullptr;
#if defined(_WIN32)
        fp = _wfopen(f.wstring().c_str(), L"rb");
#else
        fp = std::fopen(f.string().c_str(), "rb");
#endif
        if (fp) {
            std::fseek(fp, 0, SEEK_END);
            long n = std::ftell(fp);
            std::fseek(fp, 0, SEEK_SET);
            bytes.resize(n > 0 ? static_cast<size_t>(n) : 0);
            if (std::fread(bytes.data(), 1, bytes.size(), fp) != bytes.size()) bytes.clear();
            std::fclose(fp);
        }
    }
    if (bytes.empty()) return {};
    int w = 0, h = 0, c = 0;
    unsigned char* px = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &c, 4);
    if (!px) return {};
    sg_image_desc d{};
    d.width = w;
    d.height = h;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.data.mip_levels[0] = {px, static_cast<size_t>(w) * h * 4};
    sg_image out = sg_make_image(&d);
    stbi_image_free(px);
    return out;
}

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

std::shared_ptr<ModelLayer::Mesh> ModelLayer::load(const std::string& path) {
    auto cached = meshes_.find(path);
    if (cached != meshes_.end())
        if (auto m = cached->second.lock()) return m;
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) return nullptr;
    if (cgltf_load_buffers(&options, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        return nullptr;
    }
    std::filesystem::path dir = std::filesystem::u8path(path).parent_path();
    auto mesh = std::shared_ptr<Mesh>(new Mesh, [](Mesh* m) {
        if (sg_isvalid())
            for (auto& p : m->primitives) {
                sg_destroy_buffer(p.vertices);
                sg_destroy_buffer(p.indices);
                if (p.image.id) {
                    sg_destroy_view(p.texture);
                    sg_destroy_image(p.image);
                }
            }
        delete m;
    });
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
            size_t n = pos->count;
            std::vector<float> v(n * 8, 0.0f);
            for (size_t i = 0; i < n; ++i) {
                cgltf_accessor_read_float(pos, i, &v[i * 8], 3);
                if (nrm) cgltf_accessor_read_float(nrm, i, &v[i * 8 + 3], 3);
                else v[i * 8 + 4] = 1.0f;
                if (tex) cgltf_accessor_read_float(tex, i, &v[i * 8 + 6], 2);
            }
            std::vector<std::uint32_t> idx;
            if (prim.indices) {
                idx.resize(prim.indices->count);
                for (size_t i = 0; i < idx.size(); ++i) idx[i] = static_cast<std::uint32_t>(cgltf_accessor_read_index(prim.indices, i));
            } else {
                idx.resize(n);
                for (size_t i = 0; i < n; ++i) idx[i] = static_cast<std::uint32_t>(i);
            }
            Primitive p;
            sg_buffer_desc vb{};
            vb.usage.vertex_buffer = true;
            vb.data = {v.data(), v.size() * sizeof(float)};
            p.vertices = sg_make_buffer(&vb);
            sg_buffer_desc ib{};
            ib.usage.index_buffer = true;
            ib.data = {idx.data(), idx.size() * sizeof(std::uint32_t)};
            p.indices = sg_make_buffer(&ib);
            p.count = static_cast<int>(idx.size());
            if (prim.material) {
                const auto& pbr = prim.material->pbr_metallic_roughness;
                for (int k = 0; k < 4; ++k) p.color[k] = pbr.base_color_factor[k];
                if (pbr.base_color_texture.texture && pbr.base_color_texture.texture->image) {
                    p.image = decodeImage(pbr.base_color_texture.texture->image, dir);
                    if (p.image.id) {
                        sg_view_desc tv{};
                        tv.texture.image = p.image;
                        p.texture = sg_make_view(&tv);
                    }
                }
            }
            Mat4 m = fromFloat(world);
            p.matrix = m.m;
            mesh->primitives.push_back(p);
        }
    }
    cgltf_free(data);
    if (mesh->primitives.empty()) return nullptr;
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

void ModelLayer::draw(const Camera& cam, const GroundFn& ground) {
    if (!ready_ || instances_.empty()) return;
    Mat4 yUp;
    yUp.at(0, 0) = 1;
    yUp.at(2, 1) = 1;
    yUp.at(1, 2) = -1;
    yUp.at(3, 3) = 1;
    sg_apply_pipeline(pipeline_);
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
        Vec3 toEye = normalize(cam.eye() - origin);
        Vec3 light = normalize(toEye + u * 0.8);
        for (const Primitive& p : inst.mesh->primitives) {
            Mat4 node;
            node.m = p.matrix;
            model_vs_params_t vs{};
            toFloat(cam.viewProjRel() * base * node, vs.mvp);
            toFloat(orient * yUp * node, vs.rotation);
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
}

}
