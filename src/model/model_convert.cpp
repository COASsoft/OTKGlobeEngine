#include "model/model_convert.hpp"

#include "ote/image.hpp"

#include <json.hpp>
#include <miniz.h>
#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>
#include <tuple>

namespace ote {

namespace {

struct Prim {
    std::vector<float> pos;
    std::vector<float> nrm;
    std::vector<float> uv;
    std::vector<std::uint32_t> idx;
    float color[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    std::string texture;
    std::vector<std::uint8_t> image;
    std::string mime;
    std::map<std::tuple<long long, long long, long long>, std::uint32_t> seen;
};

std::string lowered(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string fileUri(std::string p) {
    p = trimmed(p);
    if (lowered(p).rfind("file://", 0) == 0) {
        p = p.substr(7);
        if (p.size() > 2 && p[0] == '/' && p[2] == ':') p = p.substr(1);
    }
    std::string out;
    for (size_t i = 0; i < p.size(); ++i) {
        if (p[i] == '%' && i + 2 < p.size() && std::isxdigit(static_cast<unsigned char>(p[i + 1])) && std::isxdigit(static_cast<unsigned char>(p[i + 2]))) {
            out.push_back(static_cast<char>(std::stoi(p.substr(i + 1, 2), nullptr, 16)));
            i += 2;
        } else {
            out.push_back(p[i] == '\\' ? '/' : p[i]);
        }
    }
    return out;
}

std::vector<std::uint8_t> embeddable(const std::vector<std::uint8_t>& bytes, std::string& mime) {
    if (bytes.size() > 8 && bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G') {
        mime = "image/png";
        return bytes;
    }
    if (bytes.size() > 3 && bytes[0] == 0xFF && bytes[1] == 0xD8) {
        mime = "image/jpeg";
        return bytes;
    }
    Image img;
    if (bytes.empty() || !decodeImage(bytes.data(), bytes.size(), img) || img.width <= 0 || img.height <= 0) return {};
    size_t n = 0;
    void* png = tdefl_write_image_to_png_file_in_memory(img.rgba.data(), img.width, img.height, 4, &n);
    if (!png) return {};
    const auto* p = static_cast<const std::uint8_t*>(png);
    std::vector<std::uint8_t> out(p, p + n);
    mz_free(png);
    mime = "image/png";
    return out;
}

void computeNormals(Prim& p) {
    p.nrm.assign(p.pos.size(), 0.0f);
    for (size_t t = 0; t + 2 < p.idx.size(); t += 3) {
        const float* a = &p.pos[p.idx[t] * 3];
        const float* b = &p.pos[p.idx[t + 1] * 3];
        const float* c = &p.pos[p.idx[t + 2] * 3];
        float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        for (int k = 0; k < 3; ++k) {
            float* n = &p.nrm[p.idx[t + k] * 3];
            n[0] += nx;
            n[1] += ny;
            n[2] += nz;
        }
    }
    for (size_t i = 0; i + 2 < p.nrm.size(); i += 3) {
        float l = std::sqrt(p.nrm[i] * p.nrm[i] + p.nrm[i + 1] * p.nrm[i + 1] + p.nrm[i + 2] * p.nrm[i + 2]);
        if (l > 1e-20f) {
            p.nrm[i] /= l;
            p.nrm[i + 1] /= l;
            p.nrm[i + 2] /= l;
        } else {
            p.nrm[i + 1] = 1.0f;
        }
    }
}

std::vector<std::uint8_t> writeGlb(std::vector<Prim>& prims) {
    std::vector<std::uint8_t> bin;
    nlohmann::json views = nlohmann::json::array(), accessors = nlohmann::json::array(), materials = nlohmann::json::array();
    nlohmann::json textures = nlohmann::json::array(), images = nlohmann::json::array(), primitives = nlohmann::json::array();
    auto addView = [&](const void* data, size_t bytes, int target) {
        while (bin.size() % 4) bin.push_back(0);
        size_t offset = bin.size();
        const auto* b = static_cast<const std::uint8_t*>(data);
        bin.insert(bin.end(), b, b + bytes);
        nlohmann::json v;
        v["buffer"] = 0;
        v["byteOffset"] = offset;
        v["byteLength"] = bytes;
        if (target) v["target"] = target;
        views.push_back(v);
        return views.size() - 1;
    };
    for (Prim& p : prims) {
        if (p.idx.empty() || p.pos.empty()) continue;
        if (p.nrm.size() != p.pos.size()) computeNormals(p);
        nlohmann::json prim;
        std::array<float, 3> mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
        for (size_t i = 0; i + 2 < p.pos.size(); i += 3)
            for (int k = 0; k < 3; ++k) {
                mn[k] = std::min(mn[k], p.pos[i + k]);
                mx[k] = std::max(mx[k], p.pos[i + k]);
            }
        nlohmann::json a;
        a["bufferView"] = addView(p.pos.data(), p.pos.size() * 4, 34962);
        a["componentType"] = 5126;
        a["count"] = p.pos.size() / 3;
        a["type"] = "VEC3";
        a["min"] = mn;
        a["max"] = mx;
        accessors.push_back(a);
        prim["attributes"]["POSITION"] = accessors.size() - 1;
        nlohmann::json n;
        n["bufferView"] = addView(p.nrm.data(), p.nrm.size() * 4, 34962);
        n["componentType"] = 5126;
        n["count"] = p.nrm.size() / 3;
        n["type"] = "VEC3";
        accessors.push_back(n);
        prim["attributes"]["NORMAL"] = accessors.size() - 1;
        const bool textured = !p.image.empty() && p.uv.size() / 2 == p.pos.size() / 3;
        if (textured) {
            nlohmann::json u;
            u["bufferView"] = addView(p.uv.data(), p.uv.size() * 4, 34962);
            u["componentType"] = 5126;
            u["count"] = p.uv.size() / 2;
            u["type"] = "VEC2";
            accessors.push_back(u);
            prim["attributes"]["TEXCOORD_0"] = accessors.size() - 1;
        }
        nlohmann::json ix;
        ix["bufferView"] = addView(p.idx.data(), p.idx.size() * 4, 34963);
        ix["componentType"] = 5125;
        ix["count"] = p.idx.size();
        ix["type"] = "SCALAR";
        accessors.push_back(ix);
        prim["indices"] = accessors.size() - 1;
        nlohmann::json mat;
        mat["pbrMetallicRoughness"]["baseColorFactor"] = {textured ? 1.0f : p.color[0], textured ? 1.0f : p.color[1], textured ? 1.0f : p.color[2], p.color[3]};
        mat["pbrMetallicRoughness"]["metallicFactor"] = 0.0;
        mat["pbrMetallicRoughness"]["roughnessFactor"] = 1.0;
        mat["doubleSided"] = true;
        if (p.color[3] < 0.999f) mat["alphaMode"] = "BLEND";
        if (textured) {
            nlohmann::json im;
            im["bufferView"] = addView(p.image.data(), p.image.size(), 0);
            im["mimeType"] = p.mime;
            images.push_back(im);
            nlohmann::json tex;
            tex["source"] = images.size() - 1;
            textures.push_back(tex);
            mat["pbrMetallicRoughness"]["baseColorTexture"]["index"] = textures.size() - 1;
        }
        materials.push_back(mat);
        prim["material"] = materials.size() - 1;
        primitives.push_back(prim);
    }
    if (primitives.empty()) return {};
    while (bin.size() % 4) bin.push_back(0);
    nlohmann::json j;
    j["asset"]["version"] = "2.0";
    j["asset"]["generator"] = "OpenTAKEngine";
    j["scene"] = 0;
    j["scenes"] = nlohmann::json::array({nlohmann::json{{"nodes", nlohmann::json::array({0})}}});
    j["nodes"] = nlohmann::json::array({nlohmann::json{{"mesh", 0}}});
    j["meshes"] = nlohmann::json::array({nlohmann::json{{"primitives", primitives}}});
    j["accessors"] = accessors;
    j["bufferViews"] = views;
    j["materials"] = materials;
    if (!textures.empty()) {
        j["textures"] = textures;
        j["images"] = images;
    }
    j["buffers"] = nlohmann::json::array({nlohmann::json{{"byteLength", bin.size()}}});
    std::string js = j.dump();
    while (js.size() % 4) js.push_back(' ');
    std::vector<std::uint8_t> out;
    auto u32 = [&out](std::uint32_t v) {
        for (int k = 0; k < 4; ++k) out.push_back(static_cast<std::uint8_t>((v >> (8 * k)) & 0xFF));
    };
    u32(0x46546C67u);
    u32(2);
    u32(static_cast<std::uint32_t>(12 + 8 + js.size() + 8 + bin.size()));
    u32(static_cast<std::uint32_t>(js.size()));
    u32(0x4E4F534Au);
    out.insert(out.end(), js.begin(), js.end());
    u32(static_cast<std::uint32_t>(bin.size()));
    u32(0x004E4942u);
    out.insert(out.end(), bin.begin(), bin.end());
    return out;
}

std::string directoryOf(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? std::string() : p.substr(0, s + 1);
}

void loadTexture(Prim& p, const AssetReader& read) {
    if (p.texture.empty() || !read) return;
    std::vector<std::uint8_t> bytes = read(p.texture);
    if (bytes.empty()) {
        std::string base = p.texture.substr(p.texture.find_last_of("/\\") == std::string::npos ? 0 : p.texture.find_last_of("/\\") + 1);
        if (base != p.texture) bytes = read(base);
    }
    p.image = embeddable(bytes, p.mime);
}

using Mat = std::array<double, 16>;

Mat identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

Mat mul(const Mat& a, const Mat& b) {
    Mat r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += a[i * 4 + k] * b[k * 4 + j];
            r[i * 4 + j] = s;
        }
    return r;
}

std::vector<double> numbers(const char* text) {
    std::vector<double> out;
    const char* p = text;
    while (*p) {
        while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
        if (!*p) break;
        char* end = nullptr;
        double v = std::strtod(p, &end);
        if (end == p) {
            ++p;
            continue;
        }
        out.push_back(v);
        p = end;
    }
    return out;
}

Mat nodeTransform(const pugi::xml_node& node) {
    Mat m = identity();
    for (auto c : node.children()) {
        std::string n = c.name();
        std::vector<double> v = numbers(c.child_value());
        Mat t = identity();
        if (n == "matrix" && v.size() >= 16) {
            std::copy(v.begin(), v.begin() + 16, t.begin());
        } else if (n == "translate" && v.size() >= 3) {
            t[3] = v[0];
            t[7] = v[1];
            t[11] = v[2];
        } else if (n == "scale" && v.size() >= 3) {
            t[0] = v[0];
            t[5] = v[1];
            t[10] = v[2];
        } else if (n == "rotate" && v.size() >= 4) {
            double x = v[0], y = v[1], z = v[2], l = std::sqrt(x * x + y * y + z * z);
            if (l < 1e-12) continue;
            x /= l;
            y /= l;
            z /= l;
            double a = v[3] * 3.14159265358979323846 / 180.0, c0 = std::cos(a), s0 = std::sin(a), k = 1 - c0;
            t = {x * x * k + c0,     x * y * k - z * s0, x * z * k + y * s0, 0, y * x * k + z * s0, y * y * k + c0, y * z * k - x * s0, 0,
                 z * x * k - y * s0, z * y * k + x * s0, z * z * k + c0,     0, 0,                  0,              0,                  1};
        } else {
            continue;
        }
        m = mul(m, t);
    }
    return m;
}

struct Input {
    std::string semantic;
    std::string source;
    int offset = 0;
    int set = 0;
};

struct FloatSource {
    std::vector<double> data;
    int stride = 1;
};

struct Effect {
    float color[4] = {0.8f, 0.8f, 0.8f, 1.0f};
    std::string image;
};

}

std::vector<std::uint8_t> objToGlb(const std::string& text, const AssetReader& read) {
    std::vector<std::array<float, 3>> v, vn;
    std::vector<std::array<float, 2>> vt;
    struct Material {
        float color[4] = {0.8f, 0.8f, 0.8f, 1.0f};
        std::string map;
    };
    std::map<std::string, Material> materials;
    std::map<std::string, size_t> byMaterial;
    std::vector<Prim> prims;
    std::vector<bool> missingNormals;
    std::string current;
    auto prim = [&]() -> size_t {
        auto it = byMaterial.find(current);
        if (it != byMaterial.end()) return it->second;
        Prim p;
        auto m = materials.find(current);
        if (m != materials.end()) {
            std::copy(m->second.color, m->second.color + 4, p.color);
            p.texture = m->second.map;
        }
        prims.push_back(std::move(p));
        missingNormals.push_back(false);
        byMaterial[current] = prims.size() - 1;
        return prims.size() - 1;
    };
    auto readMtl = [&](const std::string& file) {
        if (!read) return;
        std::vector<std::uint8_t> bytes = read(file);
        std::istringstream in(std::string(bytes.begin(), bytes.end()));
        std::string line, name;
        while (std::getline(in, line)) {
            std::istringstream ls(line);
            std::string key;
            ls >> key;
            if (key == "newmtl") {
                std::getline(ls, name);
                name = trimmed(name);
                materials[name];
            } else if (name.empty()) {
                continue;
            } else if (key == "Kd") {
                ls >> materials[name].color[0] >> materials[name].color[1] >> materials[name].color[2];
            } else if (key == "d") {
                ls >> materials[name].color[3];
            } else if (key == "Tr") {
                float tr = 0;
                ls >> tr;
                materials[name].color[3] = 1.0f - tr;
            } else if (key == "map_Kd") {
                std::string rest;
                std::getline(ls, rest);
                rest = trimmed(rest);
                size_t sp = rest.find_last_of(" \t");
                std::string path = sp == std::string::npos ? rest : rest.substr(sp + 1);
                materials[name].map = directoryOf(file) + fileUri(path);
            }
        }
    };
    auto index = [](long long i, size_t n) -> long long { return i < 0 ? static_cast<long long>(n) + i : i - 1; };
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "v") {
            std::array<float, 3> p{};
            ls >> p[0] >> p[1] >> p[2];
            v.push_back(p);
        } else if (key == "vn") {
            std::array<float, 3> p{};
            ls >> p[0] >> p[1] >> p[2];
            vn.push_back(p);
        } else if (key == "vt") {
            std::array<float, 2> p{};
            ls >> p[0] >> p[1];
            vt.push_back(p);
        } else if (key == "mtllib") {
            std::string file;
            std::getline(ls, file);
            readMtl(fileUri(trimmed(file)));
        } else if (key == "usemtl") {
            std::getline(ls, current);
            current = trimmed(current);
        } else if (key == "f") {
            std::vector<std::tuple<long long, long long, long long>> face;
            std::string tok;
            while (ls >> tok) {
                long long a = 0, b = 0, c = 0;
                bool hasT = false, hasN = false;
                size_t s1 = tok.find('/');
                a = std::atoll(tok.substr(0, s1).c_str());
                if (s1 != std::string::npos) {
                    size_t s2 = tok.find('/', s1 + 1);
                    std::string t = tok.substr(s1 + 1, s2 == std::string::npos ? std::string::npos : s2 - s1 - 1);
                    if (!t.empty()) {
                        b = std::atoll(t.c_str());
                        hasT = true;
                    }
                    if (s2 != std::string::npos && s2 + 1 < tok.size()) {
                        c = std::atoll(tok.substr(s2 + 1).c_str());
                        hasN = true;
                    }
                }
                long long vi = index(a, v.size());
                long long ti = hasT ? index(b, vt.size()) : -1;
                long long ni = hasN ? index(c, vn.size()) : -1;
                if (vi < 0 || vi >= static_cast<long long>(v.size())) {
                    face.clear();
                    break;
                }
                if (ti >= static_cast<long long>(vt.size())) ti = -1;
                if (ni >= static_cast<long long>(vn.size())) ni = -1;
                face.emplace_back(vi, ti, ni);
            }
            if (face.size() < 3) continue;
            size_t pi = prim();
            Prim& p = prims[pi];
            auto emit = [&](const std::tuple<long long, long long, long long>& key) {
                auto it = p.seen.find(key);
                if (it != p.seen.end()) return it->second;
                auto [vi, ti, ni] = key;
                std::uint32_t id = static_cast<std::uint32_t>(p.pos.size() / 3);
                p.pos.insert(p.pos.end(), v[vi].begin(), v[vi].end());
                if (ti >= 0) p.uv.insert(p.uv.end(), {vt[ti][0], 1.0f - vt[ti][1]});
                else p.uv.insert(p.uv.end(), {0.0f, 0.0f});
                if (ni >= 0) p.nrm.insert(p.nrm.end(), vn[ni].begin(), vn[ni].end());
                else {
                    p.nrm.insert(p.nrm.end(), {0.0f, 0.0f, 0.0f});
                    missingNormals[pi] = true;
                }
                p.seen[key] = id;
                return id;
            };
            for (size_t k = 1; k + 1 < face.size(); ++k) {
                std::uint32_t a = emit(face[0]), b = emit(face[k]), c = emit(face[k + 1]);
                p.idx.insert(p.idx.end(), {a, b, c});
            }
        }
    }
    for (size_t i = 0; i < prims.size(); ++i) {
        if (missingNormals[i]) prims[i].nrm.clear();
        loadTexture(prims[i], read);
    }
    return writeGlb(prims);
}

std::vector<std::uint8_t> colladaToGlb(const std::string& text, const AssetReader& read) {
    pugi::xml_document doc;
    if (!doc.load_buffer(text.data(), text.size())) return {};
    pugi::xml_node root = doc.child("COLLADA");
    if (!root) return {};
    const double unit = root.child("asset").child("unit").attribute("meter").as_double(1.0);
    const bool zUp = trimmed(root.child("asset").child_value("up_axis")) == "Z_UP";
    auto local = [](const char* url) { return std::string(url[0] == '#' ? url + 1 : url); };
    std::map<std::string, std::string> images;
    for (auto im : root.child("library_images").children("image")) {
        pugi::xml_node init = im.child("init_from");
        std::string path = init.child("ref") ? init.child_value("ref") : init.child_value();
        images[im.attribute("id").as_string()] = fileUri(path);
    }
    std::map<std::string, Effect> effects;
    for (auto fx : root.child("library_effects").children("effect")) {
        Effect e;
        pugi::xml_node prof = fx.child("profile_COMMON");
        std::map<std::string, std::string> surfaces, samplers;
        for (auto np : prof.children("newparam")) {
            std::string sid = np.attribute("sid").as_string();
            if (auto s = np.child("surface")) surfaces[sid] = trimmed(s.child_value("init_from"));
            if (auto s = np.child("sampler2D")) {
                if (s.child("source")) samplers[sid] = trimmed(s.child_value("source"));
                else if (s.child("instance_image")) samplers[sid] = "#" + local(s.child("instance_image").attribute("url").as_string());
            }
        }
        pugi::xml_node tech = prof.child("technique");
        pugi::xml_node shade;
        for (const char* k : {"phong", "lambert", "blinn", "constant"})
            if (!shade) shade = tech.child(k);
        pugi::xml_node diffuse = shade.child("diffuse");
        if (!diffuse) diffuse = shade.child("emission");
        if (auto c = diffuse.child("color")) {
            std::vector<double> v = numbers(c.child_value());
            for (size_t k = 0; k < 4 && k < v.size(); ++k) e.color[k] = static_cast<float>(v[k]);
        }
        if (auto t = diffuse.child("texture")) {
            std::string ref = t.attribute("texture").as_string();
            auto s = samplers.find(ref);
            if (s != samplers.end()) ref = s->second;
            if (!ref.empty() && ref[0] == '#') ref = ref.substr(1);
            else {
                auto sf = surfaces.find(ref);
                if (sf != surfaces.end()) ref = sf->second;
            }
            auto im = images.find(ref);
            e.image = im != images.end() ? im->second : std::string();
        }
        effects[fx.attribute("id").as_string()] = e;
    }
    std::map<std::string, std::string> materialEffect;
    for (auto m : root.child("library_materials").children("material"))
        materialEffect[m.attribute("id").as_string()] = local(m.child("instance_effect").attribute("url").as_string());
    std::map<std::string, pugi::xml_node> geometries, libraryNodes;
    for (auto g : root.child("library_geometries").children("geometry")) geometries[g.attribute("id").as_string()] = g;
    for (auto n : root.child("library_nodes").children("node")) libraryNodes[n.attribute("id").as_string()] = n;

    std::map<std::string, size_t> byEffect;
    std::vector<Prim> prims;
    size_t totalVertices = 0;
    auto primFor = [&](const std::string& effect) -> Prim& {
        auto it = byEffect.find(effect);
        if (it != byEffect.end()) return prims[it->second];
        Prim p;
        auto e = effects.find(effect);
        if (e != effects.end()) {
            std::copy(e->second.color, e->second.color + 4, p.color);
            p.texture = e->second.image;
        }
        prims.push_back(std::move(p));
        byEffect[effect] = prims.size() - 1;
        return prims.back();
    };

    auto addGeometry = [&](const pugi::xml_node& geom, const Mat& world, const std::map<std::string, std::string>& binding) {
        pugi::xml_node mesh = geom.child("mesh");
        if (!mesh) return;
        std::map<std::string, FloatSource> sources;
        for (auto s : mesh.children("source")) {
            FloatSource fs;
            fs.data = numbers(s.child_value("float_array"));
            fs.stride = std::max(1, s.child("technique_common").child("accessor").attribute("stride").as_int(1));
            sources[s.attribute("id").as_string()] = std::move(fs);
        }
        std::map<std::string, std::string> vertexPositions, vertexNormals;
        for (auto vs : mesh.children("vertices"))
            for (auto in : vs.children("input")) {
                std::string sem = in.attribute("semantic").as_string();
                if (sem == "POSITION") vertexPositions[vs.attribute("id").as_string()] = local(in.attribute("source").as_string());
                if (sem == "NORMAL") vertexNormals[vs.attribute("id").as_string()] = local(in.attribute("source").as_string());
            }
        for (auto part : mesh.children()) {
            std::string kind = part.name();
            if (kind != "triangles" && kind != "polylist" && kind != "polygons") continue;
            std::vector<Input> inputs;
            int stride = 0;
            for (auto in : part.children("input")) {
                Input i{in.attribute("semantic").as_string(), local(in.attribute("source").as_string()), in.attribute("offset").as_int(), in.attribute("set").as_int()};
                stride = std::max(stride, i.offset + 1);
                inputs.push_back(i);
            }
            const FloatSource* pos = nullptr;
            const FloatSource* nrm = nullptr;
            const FloatSource* tex = nullptr;
            int posOff = -1, nrmOff = -1, texOff = -1, texSet = 1 << 30;
            for (const Input& i : inputs) {
                if (i.semantic == "VERTEX") {
                    posOff = i.offset;
                    auto vp = vertexPositions.find(i.source);
                    if (vp != vertexPositions.end() && sources.count(vp->second)) pos = &sources[vp->second];
                    auto vn = vertexNormals.find(i.source);
                    if (vn != vertexNormals.end() && sources.count(vn->second) && !nrm) {
                        nrm = &sources[vn->second];
                        nrmOff = i.offset;
                    }
                } else if (i.semantic == "NORMAL" && sources.count(i.source)) {
                    nrm = &sources[i.source];
                    nrmOff = i.offset;
                } else if (i.semantic == "TEXCOORD" && sources.count(i.source) && i.set < texSet) {
                    tex = &sources[i.source];
                    texOff = i.offset;
                    texSet = i.set;
                }
            }
            if (!pos || posOff < 0 || stride == 0) continue;
            std::vector<std::vector<long long>> polygons;
            if (kind == "polygons") {
                for (auto p : part.children("p")) {
                    std::vector<double> d = numbers(p.child_value());
                    polygons.emplace_back(d.begin(), d.end());
                }
            } else {
                std::vector<double> d = numbers(part.child_value("p"));
                if (kind == "triangles") {
                    for (size_t s = 0; s + 3 * stride <= d.size(); s += 3 * stride)
                        polygons.emplace_back(d.begin() + static_cast<std::ptrdiff_t>(s), d.begin() + static_cast<std::ptrdiff_t>(s + 3 * stride));
                } else {
                    std::vector<double> counts = numbers(part.child_value("vcount"));
                    size_t s = 0;
                    for (double c : counts) {
                        size_t n = static_cast<size_t>(c) * stride;
                        if (s + n > d.size()) break;
                        polygons.emplace_back(d.begin() + static_cast<std::ptrdiff_t>(s), d.begin() + static_cast<std::ptrdiff_t>(s + n));
                        s += n;
                    }
                }
            }
            std::string symbol = part.attribute("material").as_string();
            auto b = binding.find(symbol);
            std::string material = b != binding.end() ? b->second : symbol;
            auto me = materialEffect.find(material);
            Prim& prim = primFor(me != materialEffect.end() ? me->second : material);
            auto vertex = [&](const std::vector<long long>& poly, size_t corner) -> std::uint32_t {
                long long pi = poly[corner * stride + posOff];
                long long ni = nrmOff >= 0 ? poly[corner * stride + nrmOff] : -1;
                long long ti = texOff >= 0 ? poly[corner * stride + texOff] : -1;
                auto key = std::make_tuple(pi, ni, ti * 65536 + static_cast<long long>(reinterpret_cast<std::uintptr_t>(&world) & 0xFFFF));
                auto it = prim.seen.find(key);
                if (it != prim.seen.end()) return it->second;
                std::uint32_t id = static_cast<std::uint32_t>(prim.pos.size() / 3);
                double x = 0, y = 0, z = 0;
                if (pi >= 0 && static_cast<size_t>(pi) * pos->stride + 2 < pos->data.size()) {
                    x = pos->data[pi * pos->stride];
                    y = pos->data[pi * pos->stride + 1];
                    z = pos->data[pi * pos->stride + 2];
                }
                double wx = world[0] * x + world[1] * y + world[2] * z + world[3];
                double wy = world[4] * x + world[5] * y + world[6] * z + world[7];
                double wz = world[8] * x + world[9] * y + world[10] * z + world[11];
                wx *= unit;
                wy *= unit;
                wz *= unit;
                if (zUp) prim.pos.insert(prim.pos.end(), {static_cast<float>(wx), static_cast<float>(wz), static_cast<float>(-wy)});
                else prim.pos.insert(prim.pos.end(), {static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz)});
                double nx = 0, ny = 1, nz = 0;
                if (nrm && ni >= 0 && static_cast<size_t>(ni) * nrm->stride + 2 < nrm->data.size()) {
                    double ax = nrm->data[ni * nrm->stride], ay = nrm->data[ni * nrm->stride + 1], az = nrm->data[ni * nrm->stride + 2];
                    nx = world[0] * ax + world[1] * ay + world[2] * az;
                    ny = world[4] * ax + world[5] * ay + world[6] * az;
                    nz = world[8] * ax + world[9] * ay + world[10] * az;
                    double l = std::sqrt(nx * nx + ny * ny + nz * nz);
                    if (l > 1e-20) {
                        nx /= l;
                        ny /= l;
                        nz /= l;
                    }
                }
                if (zUp) prim.nrm.insert(prim.nrm.end(), {static_cast<float>(nx), static_cast<float>(nz), static_cast<float>(-ny)});
                else prim.nrm.insert(prim.nrm.end(), {static_cast<float>(nx), static_cast<float>(ny), static_cast<float>(nz)});
                if (tex && ti >= 0 && static_cast<size_t>(ti) * tex->stride + 1 < tex->data.size())
                    prim.uv.insert(prim.uv.end(), {static_cast<float>(tex->data[ti * tex->stride]), static_cast<float>(1.0 - tex->data[ti * tex->stride + 1])});
                else
                    prim.uv.insert(prim.uv.end(), {0.0f, 0.0f});
                prim.seen[key] = id;
                ++totalVertices;
                return id;
            };
            for (const auto& poly : polygons) {
                size_t corners = poly.size() / stride;
                for (size_t k = 1; k + 1 < corners && totalVertices < 20000000; ++k) {
                    std::uint32_t a = vertex(poly, 0), bb = vertex(poly, k), c = vertex(poly, k + 1);
                    prim.idx.insert(prim.idx.end(), {a, bb, c});
                }
            }
            if (!nrm) prim.nrm.clear();
        }
    };

    std::function<void(const pugi::xml_node&, const Mat&, int)> walk = [&](const pugi::xml_node& node, const Mat& parent, int depth) {
        if (depth > 64) return;
        Mat world = mul(parent, nodeTransform(node));
        for (auto ig : node.children("instance_geometry")) {
            std::map<std::string, std::string> binding;
            for (auto im : ig.child("bind_material").child("technique_common").children("instance_material"))
                binding[im.attribute("symbol").as_string()] = local(im.attribute("target").as_string());
            auto g = geometries.find(local(ig.attribute("url").as_string()));
            if (g != geometries.end()) addGeometry(g->second, world, binding);
        }
        for (auto in : node.children("instance_node")) {
            auto n = libraryNodes.find(local(in.attribute("url").as_string()));
            if (n != libraryNodes.end()) walk(n->second, world, depth + 1);
        }
        for (auto child : node.children("node")) walk(child, world, depth + 1);
    };
    std::string sceneUrl = local(root.child("scene").child("instance_visual_scene").attribute("url").as_string());
    for (auto vs : root.child("library_visual_scenes").children("visual_scene")) {
        if (!sceneUrl.empty() && sceneUrl != vs.attribute("id").as_string()) continue;
        for (auto node : vs.children("node")) walk(node, identity(), 0);
    }
    for (Prim& p : prims) {
        bool missing = p.nrm.size() != p.pos.size();
        if (missing) p.nrm.clear();
        loadTexture(p, read);
    }
    return writeGlb(prims);
}

std::vector<std::uint8_t> modelToGlb(const std::string& name, const std::vector<std::uint8_t>& bytes, const AssetReader& read) {
    std::string n = lowered(name);
    auto ends = [&n](const char* s) { return n.size() >= std::strlen(s) && n.compare(n.size() - std::strlen(s), std::strlen(s), s) == 0; };
    std::string text(bytes.begin(), bytes.end());
    if (ends(".obj")) return objToGlb(text, read);
    if (ends(".dae")) return colladaToGlb(text, read);
    return {};
}

}
