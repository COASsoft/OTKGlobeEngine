#include "tiles3d/tileset.hpp"

#include "core/geo.hpp"
#include "net/http.hpp"
#include "raster/tile_cache.hpp"
#include "raster/tile_provider.hpp"

#include <json.hpp>
#include <miniz.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>

namespace ote {

class TileSource {
public:
    virtual ~TileSource() = default;
    virtual std::vector<std::uint8_t> read(const std::string& path, bool& transient) = 0;
    static std::string join(const std::string& base, const std::string& rel) {
        if (rel.find("://") != std::string::npos || (!rel.empty() && rel[0] == '/')) return rel;
        if (base.empty()) return rel;
        std::string b = base;
        if (b.back() != '/') b += '/';
        std::string r = rel;
        while (r.rfind("./", 0) == 0) r = r.substr(2);
        while (r.rfind("../", 0) == 0) {
            r = r.substr(3);
            size_t slash = b.find_last_of('/', b.size() - 2);
            b = slash == std::string::npos ? std::string() : b.substr(0, slash + 1);
        }
        return b + r;
    }
    static std::string dir(const std::string& path) {
        size_t slash = path.find_last_of('/');
        return slash == std::string::npos ? std::string() : path.substr(0, slash);
    }
};

struct Tileset3D::Implicit {
    std::string contentTemplate;
    std::string subtreeTemplate;
    std::string base;
    std::string query;
    Mat4 transform = Mat4::identity();
    bool octree = false;
    bool add = false;
    int subtreeLevels = 1;
    int availableLevels = 1;
    double rootError = 0;
    bool region = false;
    double bounds[6] = {0, 0, 0, 0, 0, 0};
    Vec3 boxCenter, boxX, boxY, boxZ;
};

struct Tileset3D::Subtree {
    struct Bits {
        int constant = 0;
        std::vector<std::uint8_t> bits;
        bool get(std::uint64_t i) const {
            if (bits.empty()) return constant != 0;
            std::uint64_t b = i >> 3;
            return b < bits.size() && ((bits[b] >> (i & 7)) & 1) != 0;
        }
    };
    Bits tiles;
    Bits content;
    Bits children;
};

namespace {

constexpr int kUnloaded = 0;
constexpr int kLoading = 1;
constexpr int kMesh = 2;
constexpr int kExpanded = 3;
constexpr int kFailed = 4;
constexpr int kSubtreeReady = 2;
constexpr int kUploadsPerFrame = 8;
constexpr double kUploadMs = 3.0;
constexpr double kRetrySeconds = 30.0;
constexpr float kPointSize = 2.0f;

double seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

FILE* openFile(const std::string& path) {
    std::filesystem::path p = std::filesystem::u8path(path);
#if defined(_WIN32)
    return _wfopen(p.wstring().c_str(), L"rb");
#else
    return std::fopen(p.string().c_str(), "rb");
#endif
}

class FileSource : public TileSource {
public:
    std::vector<std::uint8_t> read(const std::string& path, bool& transient) override {
        transient = false;
        std::vector<std::uint8_t> out;
        FILE* f = openFile(path);
        if (!f) return out;
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        out.resize(n > 0 ? static_cast<size_t>(n) : 0);
        if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
        std::fclose(f);
        return out;
    }
};

class HttpSource : public TileSource {
public:
    using Refresh = std::function<bool(net::Headers&)>;
    HttpSource(TileCache* cache, const std::atomic<bool>* offline, net::Headers headers, bool persist, Refresh refresh)
        : cache_(cache), offline_(offline), headers_(std::move(headers)), persist_(persist), refresh_(std::move(refresh)) {}
    std::vector<std::uint8_t> read(const std::string& path, bool& transient) override {
        transient = false;
        std::string key = "3dtiles|" + stripCredentials(path);
        bool useCache = persist_ && cache_ && cache_->isOpen();
        std::vector<std::uint8_t> data;
        std::int64_t expires = 0;
        if (useCache && cache_->get(key, -2, 0, 0, data, expires)) return data;
        if (offline_ && offline_->load()) {
            transient = true;
            return {};
        }
        net::Headers headers;
        {
            std::lock_guard lock(mutex_);
            headers = headers_;
        }
        net::Response r = net::get(path, "OpenTAKEngine/0.1", headers);
        if (r.status == 401 && refresh_) {
            std::lock_guard lock(mutex_);
            if (headers_ == headers && !refresh_(headers_)) return {};
            headers = headers_;
        }
        if (r.status == 401 && refresh_) r = net::get(path, "OpenTAKEngine/0.1", headers);
        if (r.status != 200 || r.body.empty()) {
            transient = r.status <= 0 || r.status == 401 || r.status == 408 || r.status == 429 || r.status >= 500;
            return {};
        }
        if (useCache) cache_->put(key, -2, 0, 0, r.body, std::int64_t(1) << 50);
        return std::move(r.body);
    }

private:
    TileCache* cache_;
    const std::atomic<bool>* offline_;
    std::mutex mutex_;
    net::Headers headers_;
    bool persist_;
    Refresh refresh_;
};

class ZipSource : public TileSource {
public:
    explicit ZipSource(const std::string& path) {
        file_ = openFile(path);
        ok_ = file_ && mz_zip_reader_init_cfile(&zip_, file_, 0, 0);
    }
    ZipSource(const ZipSource&) = delete;
    ZipSource& operator=(const ZipSource&) = delete;
    ~ZipSource() override {
        if (ok_) mz_zip_reader_end(&zip_);
        if (file_) std::fclose(file_);
    }
    std::vector<std::uint8_t> read(const std::string& path, bool& transient) override {
        transient = false;
        std::vector<std::uint8_t> out;
        if (!ok_) return out;
        std::lock_guard lock(mutex_);
        int i = mz_zip_reader_locate_file(&zip_, path.c_str(), nullptr, 0);
        if (i < 0) return out;
        size_t n = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip_, static_cast<mz_uint>(i), &n, 0);
        if (!p) return out;
        out.assign(static_cast<std::uint8_t*>(p), static_cast<std::uint8_t*>(p) + n);
        mz_free(p);
        return out;
    }

private:
    FILE* file_ = nullptr;
    mz_zip_archive zip_{};
    bool ok_ = false;
    std::mutex mutex_;
};

Mat4 fromJson(const nlohmann::json& a) {
    Mat4 m = Mat4::identity();
    if (a.is_array() && a.size() == 16)
        for (int i = 0; i < 16; ++i) m.m[i] = a[i].get<double>();
    return m;
}

Vec3 apply(const Mat4& m, Vec3 p) {
    Vec4 r = m * Vec4{p.x, p.y, p.z, 1.0};
    return {r.x, r.y, r.z};
}

Vec3 applyDir(const Mat4& m, Vec3 v) {
    Vec4 r = m * Vec4{v.x, v.y, v.z, 0.0};
    return {r.x, r.y, r.z};
}

bool looksLikeJson(const std::vector<std::uint8_t>& bytes) {
    for (std::uint8_t c : bytes) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        return c == '{';
    }
    return false;
}

bool isUrl(const std::string& s) { return s.find("://") != std::string::npos; }

std::string splitQuery(const std::string& u, std::string& query) {
    size_t q = u.find('?');
    if (q == std::string::npos) {
        query.clear();
        return u;
    }
    query = u.substr(q + 1);
    return u.substr(0, q);
}

std::string withQuery(const std::string& url, const std::string& inherited) {
    if (inherited.empty()) return url;
    std::string own;
    std::string path = splitQuery(url, own);
    auto names = [](const std::string& q) {
        std::vector<std::pair<std::string, std::string>> out;
        size_t pos = 0;
        while (pos < q.size()) {
            size_t amp = q.find('&', pos);
            if (amp == std::string::npos) amp = q.size();
            std::string part = q.substr(pos, amp - pos);
            if (!part.empty()) out.emplace_back(part.substr(0, part.find('=')), part);
            pos = amp + 1;
        }
        return out;
    };
    std::string merged = own;
    auto present = names(own);
    for (const auto& [name, part] : names(inherited)) {
        bool has = false;
        for (const auto& p : present) has = has || p.first == name;
        if (!has) merged += (merged.empty() ? "" : "&") + part;
    }
    return merged.empty() ? path : path + "?" + merged;
}

std::string resolveRef(const std::string& base, const std::string& rel) {
    if (isUrl(rel)) return rel;
    if (isUrl(base)) {
        size_t scheme = base.find("://");
        if (rel.rfind("//", 0) == 0) return base.substr(0, scheme + 1) + rel;
        if (!rel.empty() && rel[0] == '/') {
            size_t slash = base.find('/', scheme + 3);
            return (slash == std::string::npos ? base : base.substr(0, slash)) + rel;
        }
    }
    return TileSource::join(base, rel);
}

std::string stripTags(const std::string& html) {
    std::string out;
    bool tag = false;
    for (char c : html) {
        if (c == '<') tag = true;
        else if (c == '>') tag = false;
        else if (!tag) out += c;
    }
    size_t a = out.find_first_not_of(" \t\r\n"), b = out.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : out.substr(a, b - a + 1);
}

std::string normalizePath(const std::string& p) {
    std::string prefix;
    size_t start = 0;
    size_t scheme = p.find("://");
    if (scheme != std::string::npos) {
        size_t slash = p.find('/', scheme + 3);
        if (slash == std::string::npos) return p;
        prefix = p.substr(0, slash + 1);
        start = slash + 1;
    } else if (!p.empty() && p[0] == '/') {
        prefix = "/";
        start = 1;
    }
    std::string rest = p.substr(start), tail;
    size_t query = rest.find('?');
    if (query != std::string::npos) {
        tail = rest.substr(query);
        rest = rest.substr(0, query);
    }
    std::vector<std::string> parts;
    size_t pos = 0;
    while (pos <= rest.size()) {
        size_t next = rest.find('/', pos);
        if (next == std::string::npos) next = rest.size();
        std::string seg = rest.substr(pos, next - pos);
        if (seg == "..") {
            if (!parts.empty() && parts.back() != "..") parts.pop_back();
            else parts.push_back(seg);
        } else if (seg != "." && !(seg.empty() && next < rest.size())) {
            parts.push_back(seg);
        }
        pos = next + 1;
    }
    std::string out = prefix;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += '/';
        out += parts[i];
    }
    return out + tail;
}

void regionSphere(double w, double s, double e, double n, double h0, double h1, Vec3& center, double& radius) {
    center = geodeticToEcef((s + n) * 0.5, (w + e) * 0.5, (h0 + h1) * 0.5);
    radius = 0;
    for (double la : {s, (s + n) * 0.5, n})
        for (double lo : {w, (w + e) * 0.5, e})
            for (double h : {h0, h1}) radius = std::max(radius, length(geodeticToEcef(la, lo, h) - center));
}

std::uint64_t morton(std::uint32_t x, std::uint32_t y, std::uint32_t z, bool octree) {
    std::uint64_t r = 0;
    const int dims = octree ? 3 : 2;
    for (int i = 0; i < (octree ? 21 : 32); ++i) {
        r |= static_cast<std::uint64_t>((x >> i) & 1) << (dims * i);
        r |= static_cast<std::uint64_t>((y >> i) & 1) << (dims * i + 1);
        if (octree) r |= static_cast<std::uint64_t>((z >> i) & 1) << (dims * i + 2);
    }
    return r;
}

std::uint64_t levelOffset(int level, bool octree) {
    const std::uint64_t base = octree ? 8 : 4;
    std::uint64_t total = 0, count = 1;
    for (int l = 0; l < level; ++l) {
        total += count;
        count *= base;
    }
    return total;
}

std::string substitute(std::string t, int level, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    auto put = [&t](const char* key, const std::string& v) {
        for (size_t p = t.find(key); p != std::string::npos; p = t.find(key, p + v.size())) t.replace(p, std::strlen(key), v);
    };
    put("{level}", std::to_string(level));
    put("{x}", std::to_string(x));
    put("{y}", std::to_string(y));
    put("{z}", std::to_string(z));
    return t;
}

std::string contentUri(const nlohmann::json& j) {
    const nlohmann::json* c = nullptr;
    if (j.contains("content")) c = &j["content"];
    else if (j.contains("contents") && j["contents"].is_array() && !j["contents"].empty()) c = &j["contents"][0];
    if (!c) return {};
    if (c->contains("uri")) return (*c)["uri"].get<std::string>();
    if (c->contains("url")) return (*c)["url"].get<std::string>();
    return {};
}

bool parseSubtree(const std::vector<std::uint8_t>& bytes, const std::string& path, const ResourceReader& read, Tileset3D::Subtree& out) {
    const std::uint8_t* json = bytes.data();
    size_t jsonLen = bytes.size();
    const std::uint8_t* bin = nullptr;
    size_t binLen = 0;
    if (bytes.size() >= 24 && std::memcmp(bytes.data(), "subt", 4) == 0) {
        std::uint64_t jl = 0, bl = 0;
        std::memcpy(&jl, bytes.data() + 8, 8);
        std::memcpy(&bl, bytes.data() + 16, 8);
        if (24 + jl + bl > bytes.size()) return false;
        json = bytes.data() + 24;
        jsonLen = static_cast<size_t>(jl);
        bin = json + jl;
        binLen = static_cast<size_t>(bl);
    }
    nlohmann::json j = nlohmann::json::parse(json, json + jsonLen, nullptr, false);
    if (j.is_discarded()) return false;
    std::vector<std::vector<std::uint8_t>> buffers;
    if (j.contains("buffers"))
        for (const auto& b : j["buffers"]) {
            if (b.contains("uri")) {
                buffers.push_back(read(normalizePath(TileSource::join(TileSource::dir(path), b["uri"].get<std::string>()))));
            } else {
                buffers.emplace_back(bin, bin + binLen);
            }
        }
    auto bits = [&](const nlohmann::json& a, Tileset3D::Subtree::Bits& dst) {
        if (a.contains("constant")) {
            dst.constant = a["constant"].get<int>();
            return true;
        }
        int view = a.contains("bitstream") ? a["bitstream"].get<int>() : a.value("bufferView", -1);
        if (view < 0 || !j.contains("bufferViews") || view >= static_cast<int>(j["bufferViews"].size())) return false;
        const auto& bv = j["bufferViews"][view];
        size_t buffer = bv.value("buffer", size_t(0)), off = bv.value("byteOffset", size_t(0)), len = bv.value("byteLength", size_t(0));
        if (buffer >= buffers.size() || off + len > buffers[buffer].size()) return false;
        dst.bits.assign(buffers[buffer].begin() + off, buffers[buffer].begin() + off + len);
        return true;
    };
    if (!j.contains("tileAvailability") || !bits(j["tileAvailability"], out.tiles)) return false;
    if (j.contains("contentAvailability")) {
        const auto& c = j["contentAvailability"];
        if (c.is_array() && !c.empty()) bits(c[0], out.content);
        else if (c.is_object()) bits(c, out.content);
    }
    if (j.contains("childSubtreeAvailability")) bits(j["childSubtreeAvailability"], out.children);
    return true;
}

}

Tileset3D::Tileset3D(const std::string& uri, const TilesetOptions& options, TileCache* cache, const std::atomic<bool>* offline)
    : maxSse_(options.maxScreenSpaceError > 0 ? options.maxScreenSpaceError : 16.0), lighting_(static_cast<int>(options.lighting)) {
    std::string entry;
    std::string base;
    std::string query;
    std::string norm = uri;
    net::Headers headers(options.headers.begin(), options.headers.end());
    HttpSource::Refresh refresh;
    if (norm.rfind("ion://", 0) == 0) {
        std::string params;
        std::string id = splitQuery(norm.substr(6), params);
        std::string token, server = "https://api.cesium.com";
        size_t pos = 0;
        while (pos < params.size()) {
            size_t amp = params.find('&', pos);
            if (amp == std::string::npos) amp = params.size();
            std::string part = params.substr(pos, amp - pos);
            size_t eq = part.find('=');
            if (eq != std::string::npos && part.substr(0, eq) == "access_token") token = part.substr(eq + 1);
            if (eq != std::string::npos && part.substr(0, eq) == "server") server = part.substr(eq + 1);
            pos = amp + 1;
        }
        std::string endpoint = server + "/v1/assets/" + id + "/endpoint?access_token=" + token;
        std::string cacheKey = "ion-endpoint|" + server + "|" + id;
        auto fetch = [cache, offline, endpoint, cacheKey](nlohmann::json& out) {
            std::vector<std::uint8_t> body;
            bool online = !(offline && offline->load());
            if (online) {
                net::Response r = net::get(endpoint, "OpenTAKEngine/0.1");
                if (r.status == 200 && !r.body.empty()) {
                    body = std::move(r.body);
                    if (cache && cache->isOpen()) cache->put(cacheKey, -1, 0, 0, body, std::int64_t(1) << 50, true);
                }
            }
            std::int64_t expires = 0;
            if (body.empty() && cache && cache->isOpen()) cache->get(cacheKey, -1, 0, 0, body, expires);
            out = nlohmann::json::parse(body.begin(), body.end(), nullptr, false);
            return !out.is_discarded() && out.is_object();
        };
        nlohmann::json ep;
        if (!fetch(ep)) return;
        if (ep.value("externalType", std::string()) == "3DTILES" && ep.contains("options") && ep["options"].contains("url")) {
            norm = ep["options"]["url"].get<std::string>();
        } else if (ep.contains("url")) {
            norm = ep["url"].get<std::string>();
            if (ep.contains("accessToken")) headers.emplace_back("Authorization", "Bearer " + ep["accessToken"].get<std::string>());
            refresh = [fetch](net::Headers& h) {
                nlohmann::json again;
                if (!fetch(again) || !again.contains("accessToken")) return false;
                for (auto& [name, value] : h)
                    if (name == "Authorization") value = "Bearer " + again["accessToken"].get<std::string>();
                return true;
            };
        } else {
            return;
        }
        if (ep.contains("attributions"))
            for (const auto& a : ep["attributions"])
                if (a.contains("html")) attributions_.push_back(stripTags(a["html"].get<std::string>()));
    }
    std::replace(norm.begin(), norm.end(), '\\', '/');
    std::string ext = std::filesystem::u8path(splitQuery(norm, query)).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (isUrl(norm)) {
        remote_ = true;
        bool persist = options.cacheOffline && norm.find("googleapis.com") == std::string::npos;
        source_ = std::make_shared<HttpSource>(cache, offline, headers, persist, refresh);
        entry = norm;
        base = TileSource::dir(splitQuery(norm, query));
    } else if (ext == ".3tz" || ext == ".zip") {
        source_ = std::make_shared<ZipSource>(norm);
        entry = "tileset.json";
    } else {
        source_ = std::make_shared<FileSource>();
        std::error_code ec;
        entry = std::filesystem::is_directory(std::filesystem::u8path(norm), ec) ? TileSource::join(norm, "tileset.json") : norm;
        base = TileSource::dir(entry);
        query.clear();
    }
    bool transient = false;
    std::vector<std::uint8_t> bytes = source_->read(entry, transient);
    nlohmann::json j = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (j.is_discarded() || !j.contains("root")) return;
    root_ = parse(&j["root"], Mat4::identity(), base, query, false);
    int cores = static_cast<int>(std::thread::hardware_concurrency());
    int threads = norm.find("://") != std::string::npos ? 6 : std::clamp(cores - 1, 2, 6);
    for (int i = 0; i < threads; ++i) workers_.emplace_back([this] { run(); });
}

Tileset3D::~Tileset3D() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_) t.join();
}

std::unique_ptr<Tileset3D::Node> Tileset3D::parse(const void* jp, const Mat4& parent, const std::string& base, const std::string& query,
                                                  bool parentAdd) {
    const nlohmann::json& j = *static_cast<const nlohmann::json*>(jp);
    auto n = std::make_unique<Node>();
    n->transform = j.contains("transform") ? parent * fromJson(j["transform"]) : parent;
    n->geometricError = j.value("geometricError", 0.0);
    std::string refine = j.value("refine", parentAdd ? "ADD" : "REPLACE");
    n->add = refine == "ADD" || refine == "add";
    n->base = base;
    n->query = query;
    n->content = contentUri(j);
    const auto& bv = j.contains("boundingVolume") ? j["boundingVolume"] : nlohmann::json();
    Vec3 boxC, boxX, boxY, boxZ;
    double region[6] = {0, 0, 0, 0, 0, 0};
    bool isRegion = false, isBox = false;
    if (bv.contains("box") && bv["box"].size() == 12) {
        const auto& b = bv["box"];
        isBox = true;
        boxC = {b[0].get<double>(), b[1].get<double>(), b[2].get<double>()};
        boxX = {b[3].get<double>(), b[4].get<double>(), b[5].get<double>()};
        boxY = {b[6].get<double>(), b[7].get<double>(), b[8].get<double>()};
        boxZ = {b[9].get<double>(), b[10].get<double>(), b[11].get<double>()};
        Vec3 x = applyDir(n->transform, boxX), y = applyDir(n->transform, boxY), z = applyDir(n->transform, boxZ);
        n->center = apply(n->transform, boxC);
        n->radius = std::sqrt(dot(x, x) + dot(y, y) + dot(z, z));
    } else if (bv.contains("sphere") && bv["sphere"].size() == 4) {
        const auto& s = bv["sphere"];
        n->center = apply(n->transform, {s[0].get<double>(), s[1].get<double>(), s[2].get<double>()});
        double scale = std::max({length(applyDir(n->transform, {1, 0, 0})), length(applyDir(n->transform, {0, 1, 0})),
                                 length(applyDir(n->transform, {0, 0, 1}))});
        n->radius = s[3].get<double>() * scale;
    } else if (bv.contains("region") && bv["region"].size() == 6) {
        const auto& r = bv["region"];
        isRegion = true;
        for (int k = 0; k < 6; ++k) region[k] = r[k].get<double>();
        regionSphere(region[0] * kRadToDeg, region[1] * kRadToDeg, region[2] * kRadToDeg, region[3] * kRadToDeg, region[4], region[5], n->center,
                     n->radius);
    }
    const nlohmann::json* it = nullptr;
    if (j.contains("implicitTiling")) it = &j["implicitTiling"];
    else if (j.contains("extensions") && j["extensions"].contains("3DTILES_implicit_tiling")) it = &j["extensions"]["3DTILES_implicit_tiling"];
    if (it && (isRegion || isBox) && it->contains("subtrees")) {
        auto im = std::make_shared<Implicit>();
        im->contentTemplate = n->content;
        const auto& st = (*it)["subtrees"];
        im->subtreeTemplate = st.contains("uri") ? st["uri"].get<std::string>() : st.value("url", std::string());
        im->base = base;
        im->query = query;
        im->transform = n->transform;
        im->octree = it->value("subdivisionScheme", std::string("QUADTREE")) == "OCTREE";
        im->add = n->add;
        im->subtreeLevels = std::max(1, it->value("subtreeLevels", 1));
        im->availableLevels = it->contains("availableLevels") ? (*it)["availableLevels"].get<int>() : it->value("maximumLevel", 0) + 1;
        im->rootError = n->geometricError;
        im->region = isRegion;
        for (int k = 0; k < 6; ++k) im->bounds[k] = region[k];
        im->boxCenter = boxC;
        im->boxX = boxX;
        im->boxY = boxY;
        im->boxZ = boxZ;
        n->implicit = im;
        n->content.clear();
        n->subtreeState = kUnloaded;
        return n;
    }
    if (j.contains("children"))
        for (const auto& c : j["children"]) n->children.push_back(parse(&c, n->transform, base, query, n->add));
    return n;
}

std::unique_ptr<Tileset3D::Node> Tileset3D::implicitNode(const std::shared_ptr<const Implicit>& im, int level, std::uint32_t x, std::uint32_t y,
                                                         std::uint32_t z) const {
    auto n = std::make_unique<Node>();
    n->implicit = im;
    n->transform = im->transform;
    n->add = im->add;
    n->base = im->base;
    n->query = im->query;
    n->level = level;
    n->x = x;
    n->y = y;
    n->z = z;
    n->geometricError = std::ldexp(im->rootError, -level);
    const double div = std::ldexp(1.0, level);
    if (im->region) {
        const double* b = im->bounds;
        double dw = (b[2] - b[0]) / div, ds = (b[3] - b[1]) / div;
        double w = b[0] + dw * x, s = b[1] + ds * y;
        double h0 = b[4], h1 = b[5];
        if (im->octree) {
            double dh = (b[5] - b[4]) / div;
            h0 = b[4] + dh * z;
            h1 = h0 + dh;
        }
        regionSphere(w * kRadToDeg, s * kRadToDeg, (w + dw) * kRadToDeg, (s + ds) * kRadToDeg, h0, h1, n->center, n->radius);
    } else {
        auto offset = [div](std::uint32_t i) { return -1.0 + (2.0 * i + 1.0) / div; };
        Vec3 c = im->boxCenter + im->boxX * offset(x) + im->boxY * offset(y);
        Vec3 hz = im->boxZ;
        if (im->octree) {
            c = c + im->boxZ * offset(z);
            hz = im->boxZ * (1.0 / div);
        }
        Vec3 hx = applyDir(im->transform, im->boxX * (1.0 / div)), hy = applyDir(im->transform, im->boxY * (1.0 / div));
        hz = applyDir(im->transform, hz);
        n->center = apply(im->transform, c);
        n->radius = std::sqrt(dot(hx, hx) + dot(hy, hy) + dot(hz, hz));
    }
    return n;
}

void Tileset3D::expand(Node* n) {
    n->expanded = true;
    const Implicit& im = *n->implicit;
    const int childLevel = n->level + 1;
    if (childLevel >= im.availableLevels || !n->subtree) return;
    const bool newSubtree = childLevel - n->rootLevel == im.subtreeLevels;
    const int local = childLevel - n->rootLevel;
    for (int c = 0; c < (im.octree ? 8 : 4); ++c) {
        std::uint32_t cx = n->x * 2 + (c & 1), cy = n->y * 2 + ((c >> 1) & 1), cz = im.octree ? n->z * 2 + ((c >> 2) & 1) : 0;
        std::uint32_t lx = cx - (n->rootX << local), ly = cy - (n->rootY << local), lz = cz - (n->rootZ << local);
        std::uint64_t m = morton(lx, ly, lz, im.octree);
        if (newSubtree) {
            if (!n->subtree->children.get(m)) continue;
            auto kid = implicitNode(n->implicit, childLevel, cx, cy, cz);
            kid->rootLevel = childLevel;
            kid->rootX = cx;
            kid->rootY = cy;
            kid->rootZ = cz;
            kid->subtreeState = kUnloaded;
            n->children.push_back(std::move(kid));
        } else {
            std::uint64_t index = levelOffset(local, im.octree) + m;
            if (!n->subtree->tiles.get(index)) continue;
            auto kid = implicitNode(n->implicit, childLevel, cx, cy, cz);
            kid->subtree = n->subtree;
            kid->rootLevel = n->rootLevel;
            kid->rootX = n->rootX;
            kid->rootY = n->rootY;
            kid->rootZ = n->rootZ;
            if (!im.contentTemplate.empty() && n->subtree->content.get(index)) kid->content = substitute(im.contentTemplate, childLevel, cx, cy, cz);
            n->children.push_back(std::move(kid));
        }
    }
}

Vec3 Tileset3D::center() const { return root_ ? root_->center : Vec3{}; }

double Tileset3D::radius() const { return root_ ? root_->radius : 0.0; }

std::string Tileset3D::contentPath(const Node* n, const std::string& rel) const {
    std::string path = normalizePath(resolveRef(n->base, rel));
    return remote_ ? withQuery(path, n->query) : path;
}

void Tileset3D::collectCredits() {
    std::map<std::string, int> counts;
    for (const Node* n : draw_)
        for (const auto& mesh : n->meshes) {
            size_t pos = 0;
            const std::string& c = mesh->copyright;
            while (pos < c.size()) {
                size_t semi = c.find(';', pos);
                if (semi == std::string::npos) semi = c.size();
                std::string part = stripTags(c.substr(pos, semi - pos));
                if (!part.empty()) ++counts[part];
                pos = semi + 1;
            }
        }
    credits_.clear();
    for (const std::string& a : attributions_) credits_.emplace_back(a, 1 << 30);
    for (const auto& [text, count] : counts) credits_.emplace_back(text, count);
    std::stable_sort(credits_.begin(), credits_.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
}

bool Tileset3D::contentReady(const Node* n) const {
    if (n->subtreeState.load() != kSubtreeReady) return false;
    if (n->content.empty()) return true;
    int s = n->state.load();
    return s == kMesh || s == kExpanded;
}

bool Tileset3D::hasFallback(const Node* n) const { return n->state.load() == kMesh && !n->meshes.empty(); }

void Tileset3D::request(Node* n, double priority) {
    int st = n->subtreeState.load();
    if (st != kSubtreeReady) {
        if (st == kFailed) {
            if (n->subtreePermanent || (seconds() < n->subtreeRetryAt && net::reachabilityEpoch() == n->failedEpoch)) return;
            n->subtreeState = kUnloaded;
            --failed_;
        } else if (st != kUnloaded) {
            return;
        }
        const Implicit& im = *n->implicit;
        wanted_.push_back({n, contentPath(n, substitute(im.subtreeTemplate, n->level, n->x, n->y, n->z)), priority, true});
        return;
    }
    if (n->content.empty()) return;
    int s = n->state.load();
    if (s == kFailed) {
        if (n->permanent || (seconds() < n->retryAt && net::reachabilityEpoch() == n->failedEpoch)) return;
        n->state = kUnloaded;
        --failed_;
    } else if (s != kUnloaded) {
        return;
    }
    wanted_.push_back({n, contentPath(n, n->content), priority, false});
}

void Tileset3D::select(Node* n, const Camera& cam, const std::array<double, 16>& frustum, std::uint64_t frame) {
    Vec3 rel = n->center - cam.eye();
    for (int p = 0; p < 4; ++p) {
        const double* pl = &frustum[p * 4];
        if (pl[0] * rel.x + pl[1] * rel.y + pl[2] * rel.z + pl[3] < -n->radius) return;
    }
    n->used = frame;
    if (n->implicit && !n->expanded && n->subtreeState.load() == kSubtreeReady) expand(n);
    double dist = std::max(length(rel) - n->radius, 1.0);
    double sse = n->geometricError * cam.height / (2.0 * dist * std::tan(cam.fovY * 0.5));
    bool refine = sse > maxSse_ && !n->children.empty();
    if (!refine) {
        if (contentReady(n)) draw_.push_back(n);
        else request(n, dist);
        return;
    }
    if (n->add) {
        if (contentReady(n)) draw_.push_back(n);
        else request(n, dist);
        for (auto& c : n->children) select(c.get(), cam, frustum, frame);
        return;
    }
    bool kidsReady = true;
    for (auto& c : n->children) {
        if (!contentReady(c.get())) {
            kidsReady = false;
            request(c.get(), dist * 1.5);
        }
    }
    if (kidsReady || !hasFallback(n)) {
        if (!contentReady(n)) request(n, dist);
        for (auto& c : n->children) select(c.get(), cam, frustum, frame);
        return;
    }
    draw_.push_back(n);
}

void Tileset3D::update(const Camera& cam, const std::array<double, 16>& frustum, std::uint64_t frame) {
    if (!root_) return;
    auto start = std::chrono::steady_clock::now();
    int uploads = 0;
    for (;;) {
        Done d;
        {
            std::lock_guard lock(mutex_);
            if (done_.empty()) break;
            if (uploads > 0 && (uploads >= kUploadsPerFrame ||
                                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() > kUploadMs))
                break;
            d = std::move(done_.front());
            done_.pop_front();
        }
        Node* n = d.node;
        if (d.isSubtree) {
            if (d.ok) {
                n->subtree = d.availability;
                const Implicit& im = *n->implicit;
                if (!im.contentTemplate.empty() && n->subtree->content.get(0)) n->content = substitute(im.contentTemplate, n->level, n->x, n->y, n->z);
                n->subtreeState = kSubtreeReady;
            } else {
                n->subtreePermanent = !d.transient;
                n->subtreeRetryAt = seconds() + kRetrySeconds;
                n->failedEpoch = net::reachabilityEpoch();
                n->subtreeState = kFailed;
                ++failed_;
            }
            continue;
        }
        if (d.ok && d.subtree) {
            n->children.push_back(std::move(d.subtree));
            n->state = kExpanded;
        } else if (d.ok && !d.models.empty()) {
            for (const CpuModel& m : d.models) {
                auto mesh = ModelLayer::upload(m);
                resident_ += mesh->bytes;
                n->meshes.push_back(std::move(mesh));
            }
            n->state = kMesh;
            loaded_.push_back(n);
            ++uploads;
        } else {
            n->permanent = !d.transient;
            n->retryAt = seconds() + kRetrySeconds;
            n->failedEpoch = net::reachabilityEpoch();
            n->state = kFailed;
            ++failed_;
        }
    }
    draw_.clear();
    wanted_.clear();
    select(root_.get(), cam, frustum, frame);
    collectCredits();
    std::sort(wanted_.begin(), wanted_.end(), [](const Job& a, const Job& b) { return a.priority > b.priority; });
    {
        std::lock_guard lock(mutex_);
        queue_ = wanted_;
        if (!queue_.empty()) cv_.notify_all();
    }
    evict(frame);
}

void Tileset3D::evict(std::uint64_t frame) {
    if (resident_ <= budget_) return;
    std::sort(loaded_.begin(), loaded_.end(), [](const Node* a, const Node* b) { return a->used < b->used; });
    for (auto it = loaded_.begin(); it != loaded_.end() && resident_ > budget_;) {
        Node* n = *it;
        if (n->used + 2 >= frame) break;
        for (const auto& m : n->meshes) resident_ -= m->bytes;
        n->meshes.clear();
        n->state = kUnloaded;
        it = loaded_.erase(it);
    }
}

bool Tileset3D::busy() const {
    std::lock_guard lock(mutex_);
    return !queue_.empty() || inflight_ > 0 || !done_.empty();
}

void Tileset3D::draw(const Camera& cam, ModelLayer& models, PointCloudLayer& points, double dpi) {
    if (draw_.empty() || !(opacity_ > 0.0f)) return;
    models.begin();
    models.setOpacity(opacity_);
    bool anyPoints = false;
    for (Node* n : draw_) {
        Mat4 orient = n->transform;
        orient.at(0, 3) = orient.at(1, 3) = orient.at(2, 3) = 0;
        Vec3 light = normalize(normalize(cam.eye() - n->center) + normalize(n->center) * 0.8);
        for (const auto& mesh : n->meshes) {
            anyPoints = anyPoints || mesh->pointCount > 0;
            if (mesh->primitives.empty()) continue;
            Mat4 model = translation(-cam.eye()) * n->transform * translation({mesh->rtc[0], mesh->rtc[1], mesh->rtc[2]});
            models.drawMesh(cam, *mesh, model, orient, light, lighting_);
        }
    }
    models.setOpacity(1.0f);
    if (!anyPoints) return;
    for (Node* n : draw_)
        for (const auto& mesh : n->meshes) {
            if (mesh->pointCount <= 0) continue;
            Mat4 model = translation(-cam.eye()) * n->transform * translation({mesh->rtc[0], mesh->rtc[1], mesh->rtc[2]}) *
                         translation({mesh->pointsOrigin[0], mesh->pointsOrigin[1], mesh->pointsOrigin[2]});
            points.drawBuffer(cam, mesh->points, mesh->pointCount, model, kPointSize, dpi);
        }
}

void Tileset3D::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            job = queue_.back();
            queue_.pop_back();
            std::atomic<int>& st = job.subtree ? job.node->subtreeState : job.node->state;
            int expected = kUnloaded;
            if (!st.compare_exchange_strong(expected, kLoading)) continue;
            ++inflight_;
        }
        Done d;
        d.node = job.node;
        d.isSubtree = job.subtree;
        std::string jobQuery;
        const std::string jobPath = splitQuery(job.path, jobQuery);
        ResourceReader read = [this, jobQuery](const std::string& p) {
            bool transient = false;
            std::string path = normalizePath(p);
            return source_->read(remote_ ? withQuery(path, jobQuery) : path, transient);
        };
        std::vector<std::uint8_t> bytes = source_->read(job.path, d.transient);
        if (!bytes.empty() && job.subtree) {
            auto sub = std::make_shared<Subtree>();
            d.ok = parseSubtree(bytes, jobPath, read, *sub);
            d.availability = sub;
        } else if (!bytes.empty()) {
            bool tileset = false;
            if (looksLikeJson(bytes)) {
                nlohmann::json j = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
                if (!j.is_discarded() && j.contains("root")) {
                    tileset = true;
                    d.subtree = parse(&j["root"], job.node->transform, TileSource::dir(jobPath), remote_ ? jobQuery : std::string(), job.node->add);
                    d.ok = true;
                }
            }
            if (!tileset) d.ok = parseTileContent(bytes.data(), bytes.size(), jobPath, read, job.node->transform, d.models);
        }
        std::lock_guard lock(mutex_);
        --inflight_;
        done_.push_back(std::move(d));
    }
}

}
