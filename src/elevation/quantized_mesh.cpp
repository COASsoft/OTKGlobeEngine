#include "elevation/quantized_mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace ote {

namespace {

int zigzag(int v) { return (v >> 1) ^ (-(v & 1)); }

}

bool decodeQuantizedMesh(const std::vector<std::uint8_t>& data, double west, double south, double east, double north,
                         int gridSize, HeightGrid& out) {
    const std::uint8_t* p = data.data();
    size_t size = data.size();
    if (size < 92) return false;
    float minH, maxH;
    std::memcpy(&minH, p + 24, 4);
    std::memcpy(&maxH, p + 28, 4);
    size_t pos = 88;
    std::uint32_t count;
    std::memcpy(&count, p + pos, 4);
    pos += 4;
    if (count == 0 || pos + static_cast<size_t>(count) * 6 > size) return false;
    std::vector<double> u(count), v(count), h(count);
    const std::uint8_t* arrays = p + pos;
    int cu = 0, cv = 0, ch = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint16_t du, dv, dh;
        std::memcpy(&du, arrays + i * 2, 2);
        std::memcpy(&dv, arrays + (count + i) * 2, 2);
        std::memcpy(&dh, arrays + (2 * count + i) * 2, 2);
        cu += zigzag(du);
        cv += zigzag(dv);
        ch += zigzag(dh);
        u[i] = cu / 32767.0;
        v[i] = cv / 32767.0;
        h[i] = minH + (maxH - minH) * (ch / 32767.0);
    }
    pos += static_cast<size_t>(count) * 6;
    bool wide = count > 65536;
    if (wide && pos % 4) pos += 4 - pos % 4;
    else if (!wide && pos % 2) pos += 1;
    if (pos + 4 > size) return false;
    std::uint32_t triangles;
    std::memcpy(&triangles, p + pos, 4);
    pos += 4;
    size_t stride = wide ? 4 : 2;
    if (pos + static_cast<size_t>(triangles) * 3 * stride > size) return false;
    std::vector<std::uint32_t> idx(static_cast<size_t>(triangles) * 3);
    std::uint32_t highest = 0;
    for (size_t i = 0; i < idx.size(); ++i) {
        std::uint32_t code = 0;
        if (wide) std::memcpy(&code, p + pos + i * 4, 4);
        else {
            std::uint16_t c16;
            std::memcpy(&c16, p + pos + i * 2, 2);
            code = c16;
        }
        idx[i] = highest - code;
        if (code == 0) ++highest;
    }
    out.width = out.height = gridSize;
    out.minX = west;
    out.maxX = east;
    out.minY = south;
    out.maxY = north;
    out.values.assign(static_cast<size_t>(gridSize) * gridSize, std::numeric_limits<float>::quiet_NaN());
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        std::uint32_t a = idx[t], b = idx[t + 1], c = idx[t + 2];
        if (a >= count || b >= count || c >= count) continue;
        double ax = u[a], ay = v[a], bx = u[b], by = v[b], cx = u[c], cy = v[c];
        double den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        if (std::abs(den) < 1e-15) continue;
        int i0 = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx}) * gridSize - 0.5)));
        int i1 = std::min(gridSize - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx}) * gridSize - 0.5)));
        int j0 = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy}) * gridSize - 0.5)));
        int j1 = std::min(gridSize - 1, static_cast<int>(std::ceil(std::max({ay, by, cy}) * gridSize - 0.5)));
        for (int j = j0; j <= j1; ++j) {
            double py = (j + 0.5) / gridSize;
            for (int i = i0; i <= i1; ++i) {
                double px = (i + 0.5) / gridSize;
                double w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den;
                double w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den;
                double w2 = 1.0 - w0 - w1;
                if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
                int row = gridSize - 1 - j;
                out.values[static_cast<size_t>(row) * gridSize + i] = static_cast<float>(w0 * h[a] + w1 * h[b] + w2 * h[c]);
            }
        }
    }
    return true;
}

}
