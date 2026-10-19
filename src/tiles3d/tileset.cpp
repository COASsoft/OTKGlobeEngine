#include "tiles3d/tileset.hpp"

#include "net/http.hpp"

#include <json.hpp>
#include <miniz.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace ote {

class TileSource {
public:
    virtual ~TileSource() = default;
    virtual std::vector<std::uint8_t> read(const std::string& path) = 0;
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

namespace {

class FileSource : public TileSource {
public:
    std::vector<std::uint8_t> read(const std::string& path) override {
        std::vector<std::uint8_t> out;
        std::filesystem::path p = std::filesystem::u8path(path);
#if defined(_WIN32)
        FILE* f = _wfopen(p.wstring().c_str(), L"rb");
#else
        FILE* f = std::fopen(p.string().c_str(), "rb");
#endif
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
    std::vector<std::uint8_t> read(const std::string& path) override {
        net::Response r = net::get(path, "OpenTAKEngine/0.1");
        return r.status == 200 ? std::move(r.body) : std::vector<std::uint8_t>();
    }
};

class ZipSource : public TileSource {
public:
    explicit ZipSource(const std::string& path) {
        std::vector<std::uint8_t> bytes = FileSource().read(path);
        data_ = std::move(bytes);
        ok_ = !data_.empty() && mz_zip_reader_init_mem(&zip_, data_.data(), data_.size(), 0);
    }
    ~ZipSource() override {
        if (ok_) mz_zip_reader_end(&zip_);
    }
    std::vector<std::uint8_t> read(const std::string& path) override {
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
    std::vector<std::uint8_t> data_;
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

std::string lowerExt(const std::string& path) {
    std::string e = std::filesystem::u8path(path).extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

}

Tileset3D::Tileset3D(const std::string& uri, double maxSse) : maxSse_(maxSse) {
    std::string entry;
    std::string base;
    std::string norm = uri;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    std::string ext = lowerExt(norm);
    if (norm.find("://") != std::string::npos) {
        source_ = std::make_shared<HttpSource>();
        entry = norm;
        base = TileSource::dir(norm);
    } else if (ext == ".3tz" || ext == ".zip") {
        source_ = std::make_shared<ZipSource>(norm);
        entry = "tileset.json";
    } else {
        source_ = std::make_shared<FileSource>();
        std::error_code ec;
        entry = std::filesystem::is_directory(std::filesystem::u8path(norm), ec) ? TileSource::join(norm, "tileset.json") : norm;
        base = TileSource::dir(entry);
    }
    std::vector<std::uint8_t> bytes = source_->read(entry);
    nlohmann::json j = nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false);
    if (j.is_discarded() || !j.contains("root")) return;
    root_ = parse(&j["root"], Mat4::identity(), base, false);
    for (int i = 0; i < 2; ++i) workers_.emplace_back([this] { run(); });
}

Tileset3D::~Tileset3D() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_) t.join();
}

std::unique_ptr<Tileset3D::Node> Tileset3D::parse(const void* jp, const Mat4& parent, const std::string& base, bool parentAdd) {
    const nlohmann::json& j = *static_cast<const nlohmann::json*>(jp);
    auto n = std::make_unique<Node>();
    n->transform = j.contains("transform") ? parent * fromJson(j["transform"]) : parent;
    n->geometricError = j.value("geometricError", 0.0);
    std::string refine = j.value("refine", parentAdd ? "ADD" : "REPLACE");
    n->add = refine == "ADD" || refine == "add";
    n->base = base;
    if (j.contains("content")) {
        const auto& c = j["content"];
        if (c.contains("uri")) n->content = c["uri"].get<std::string>();
        else if (c.contains("url")) n->content = c["url"].get<std::string>();
    }
    const auto& bv = j.contains("boundingVolume") ? j["boundingVolume"] : nlohmann::json();
    if (bv.contains("box") && bv["box"].size() == 12) {
        const auto& b = bv["box"];
        Vec3 c{b[0].get<double>(), b[1].get<double>(), b[2].get<double>()};
        Vec3 x = applyDir(n->transform, {b[3].get<double>(), b[4].get<double>(), b[5].get<double>()});
        Vec3 y = applyDir(n->transform, {b[6].get<double>(), b[7].get<double>(), b[8].get<double>()});
        Vec3 z = applyDir(n->transform, {b[9].get<double>(), b[10].get<double>(), b[11].get<double>()});
        n->center = apply(n->transform, c);
        n->radius = std::sqrt(dot(x, x) + dot(y, y) + dot(z, z));
    } else if (bv.contains("sphere") && bv["sphere"].size() == 4) {
        const auto& s = bv["sphere"];
        n->center = apply(n->transform, {s[0].get<double>(), s[1].get<double>(), s[2].get<double>()});
        double scale = std::max({length(applyDir(n->transform, {1, 0, 0})), length(applyDir(n->transform, {0, 1, 0})),
                                 length(applyDir(n->transform, {0, 0, 1}))});
        n->radius = s[3].get<double>() * scale;
    } else if (bv.contains("region") && bv["region"].size() == 6) {
        const auto& r = bv["region"];
        double w = r[0].get<double>() * kRadToDeg, s = r[1].get<double>() * kRadToDeg;
        double e = r[2].get<double>() * kRadToDeg, nn = r[3].get<double>() * kRadToDeg;
        double h0 = r[4].get<double>(), h1 = r[5].get<double>();
        n->center = geodeticToEcef((s + nn) * 0.5, (w + e) * 0.5, (h0 + h1) * 0.5);
        double rad = 0;
        for (double la : {s, nn})
            for (double lo : {w, e})
                for (double h : {h0, h1}) rad = std::max(rad, length(geodeticToEcef(la, lo, h) - n->center));
        n->radius = rad;
    }
    if (j.contains("children"))
        for (const auto& c : j["children"]) n->children.push_back(parse(&c, n->transform, base, n->add));
    return n;
}

Vec3 Tileset3D::center() const { return root_ ? root_->center : Vec3{}; }

bool Tileset3D::contentReady(const Node* n) const {
    if (n->content.empty()) return true;
    return n->state == 2 || n->state == 3;
}

void Tileset3D::request(Node* n, double priority) {
    if (n->content.empty() || n->state != 0) return;
    wanted_.push_back({n, TileSource::join(n->base, n->content), priority});
}

void Tileset3D::select(Node* n, const Camera& cam, const std::array<double, 16>& frustum, std::uint64_t frame) {
    Vec3 rel = n->center - cam.eye();
    for (int p = 0; p < 4; ++p) {
        const double* pl = &frustum[p * 4];
        if (pl[0] * rel.x + pl[1] * rel.y + pl[2] * rel.z + pl[3] < -n->radius) return;
    }
    n->used = frame;
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
    if (kidsReady || !contentReady(n)) {
        if (!contentReady(n) && !n->content.empty()) request(n, dist);
        for (auto& c : n->children) select(c.get(), cam, frustum, frame);
        return;
    }
    draw_.push_back(n);
}

void Tileset3D::update(const Camera& cam, const std::array<double, 16>& frustum, std::uint64_t frame) {
    if (!root_) return;
    {
        std::lock_guard lock(mutex_);
        while (!done_.empty()) {
            Done d = std::move(done_.front());
            done_.pop_front();
            if (d.ok && d.tileset) {
                const auto& j = *static_cast<const nlohmann::json*>(d.tileset.get());
                d.node->children.push_back(parse(&j["root"], d.node->transform, d.tilesetBase, d.node->add));
                d.node->state = 3;
            } else if (d.ok && d.model) {
                d.node->mesh = ModelLayer::upload(*d.model);
                d.node->state = 2;
                resident_ += d.node->mesh->bytes;
                loaded_.push_back(d.node);
            } else {
                d.node->state = 3;
            }
        }
    }
    draw_.clear();
    wanted_.clear();
    select(root_.get(), cam, frustum, frame);
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
        resident_ -= n->mesh ? n->mesh->bytes : 0;
        n->mesh.reset();
        n->state = 0;
        it = loaded_.erase(it);
    }
}

bool Tileset3D::busy() const {
    std::lock_guard lock(mutex_);
    return !queue_.empty() || inflight_ > 0 || !done_.empty();
}

void Tileset3D::draw(const Camera& cam, ModelLayer& models) {
    if (draw_.empty()) return;
    models.begin();
    Mat4 yUp = yUpToZUp();
    for (Node* n : draw_) {
        if (!n->mesh) continue;
        const GpuMesh& m = *n->mesh;
        Mat4 model = translation(Vec3{-cam.eye().x, -cam.eye().y, -cam.eye().z}) * n->transform *
                     translation({m.rtc[0], m.rtc[1], m.rtc[2]}) * yUp;
        Mat4 orient = n->transform;
        orient.at(0, 3) = orient.at(1, 3) = orient.at(2, 3) = 0;
        Vec3 c = n->center;
        Vec3 light = normalize(normalize(cam.eye() - c) + normalize(c) * 0.8);
        models.drawMesh(cam, m, model, orient * yUp, light);
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
            if (job.node->state != 0) continue;
            job.node->state = 1;
            ++inflight_;
        }
        Done d;
        d.node = job.node;
        std::vector<std::uint8_t> bytes = source_->read(job.path);
        if (!bytes.empty() && lowerExt(job.path) == ".json") {
            auto j = std::make_shared<nlohmann::json>(nlohmann::json::parse(bytes.begin(), bytes.end(), nullptr, false));
            d.ok = !j->is_discarded() && j->contains("root");
            d.tileset = j;
            d.tilesetBase = TileSource::dir(job.path);
        } else if (!bytes.empty()) {
            auto model = std::make_unique<CpuModel>();
            d.ok = parseB3dm(bytes.data(), bytes.size(), *model);
            d.model = std::move(model);
        }
        std::lock_guard lock(mutex_);
        --inflight_;
        done_.push_back(std::move(d));
    }
}

}
