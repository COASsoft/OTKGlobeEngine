#include "raster/tile_loader.hpp"

#include "net/http.hpp"

#include <stb_image.h>

#include <algorithm>

namespace ote {

namespace {

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size()))
        s.replace(pos, from.size(), to);
}

std::string quadkey(int z, int x, int y) {
    std::string q;
    for (int i = z; i > 0; --i) {
        int mask = 1 << (i - 1);
        char d = '0';
        if (x & mask) d += 1;
        if (y & mask) d += 2;
        q.push_back(d);
    }
    return q;
}

}

std::string tileUrl(const ImageryLayer& layer, int z, int x, int y) {
    std::string url = layer.url;
    int yy = layer.tms ? (1 << z) - 1 - y : y;
    std::string server = layer.servers.empty() ? std::string() : layer.servers[(x + y) % layer.servers.size()];
    for (const char* k : {"{$z}", "{z}"}) replaceAll(url, k, std::to_string(z));
    for (const char* k : {"{$x}", "{x}"}) replaceAll(url, k, std::to_string(x));
    for (const char* k : {"{$y}", "{y}"}) replaceAll(url, k, std::to_string(yy));
    replaceAll(url, "{-y}", std::to_string((1 << z) - 1 - y));
    for (const char* k : {"{$q}", "{q}"}) replaceAll(url, k, quadkey(z, x, y));
    for (const char* k : {"{$serverpart}", "{s}"}) replaceAll(url, k, server);
    return url;
}

TileLoader::TileLoader(int threads) {
    for (int i = 0; i < threads; ++i) workers_.emplace_back([this] { run(); });
}

TileLoader::~TileLoader() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& t : workers_) t.join();
}

void TileLoader::setLayers(const std::vector<ImageryLayer>& layers) {
    std::lock_guard lock(mutex_);
    layers_ = layers;
    queue_.clear();
    done_.clear();
    ++generation_;
}

void TileLoader::submit(std::vector<std::pair<TileId, double>>& requests) {
    std::sort(requests.begin(), requests.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    std::lock_guard lock(mutex_);
    queue_.clear();
    queue_.reserve(requests.size());
    for (const auto& r : requests)
        if (!inflight_.count(r.first)) queue_.push_back(r.first);
    if (!queue_.empty()) cv_.notify_all();
}

std::vector<DecodedTile> TileLoader::take(size_t max) {
    std::lock_guard lock(mutex_);
    std::vector<DecodedTile> out;
    while (!done_.empty() && out.size() < max) {
        if (done_.front().generation == generation_) out.push_back(std::move(done_.front()));
        done_.pop_front();
    }
    return out;
}

int TileLoader::pending() const {
    std::lock_guard lock(mutex_);
    return static_cast<int>(queue_.size() + inflight_.size());
}

void TileLoader::run() {
    for (;;) {
        TileId id;
        ImageryLayer layer;
        int gen = 0;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            id = queue_.back();
            queue_.pop_back();
            if (id.layer < 0 || id.layer >= static_cast<int>(layers_.size())) continue;
            layer = layers_[id.layer];
            gen = generation_;
            inflight_.insert(id);
        }
        DecodedTile tile;
        tile.id = id;
        tile.generation = gen;
        net::Response res = net::get(tileUrl(layer, id.z, id.x, id.y), "OpenTAKEngine/0.1");
        if (res.status == 200 && !res.body.empty()) {
            int w = 0, h = 0, n = 0;
            unsigned char* px = stbi_load_from_memory(res.body.data(), static_cast<int>(res.body.size()), &w, &h, &n, 4);
            if (px) {
                tile.ok = true;
                tile.width = w;
                tile.height = h;
                tile.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
                stbi_image_free(px);
            } else {
                tile.permanent = true;
            }
        } else if (res.status == 404 || res.status == 204 || res.status == 400 || (res.status == 200 && res.body.empty())) {
            tile.permanent = true;
        }
        std::lock_guard lock(mutex_);
        inflight_.erase(id);
        done_.push_back(std::move(tile));
    }
}

}
