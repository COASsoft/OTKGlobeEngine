#include "ote/image.hpp"

#include <stb_image.h>

#include <cstring>

#if defined(OTE_WITH_WEBP)
#include <webp/decode.h>
#endif

namespace ote {

bool isWebp(const std::uint8_t* data, std::size_t size) {
    return data && size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0;
}

bool webpSupported() {
#if defined(OTE_WITH_WEBP)
    return true;
#else
    return false;
#endif
}

bool decodeImage(const std::uint8_t* data, std::size_t size, Image& out) {
    out = Image{};
    if (!data || size == 0 || size > 0x7fffffff) return false;
    if (isWebp(data, size)) {
#if defined(OTE_WITH_WEBP)
        int w = 0, h = 0;
        if (!WebPGetInfo(data, size, &w, &h) || w <= 0 || h <= 0) return false;
        out.rgba.resize(static_cast<std::size_t>(w) * h * 4);
        if (!WebPDecodeRGBAInto(data, size, out.rgba.data(), out.rgba.size(), w * 4)) {
            out = Image{};
            return false;
        }
        out.width = w;
        out.height = h;
        return true;
#else
        return false;
#endif
    }
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &n, 4);
    if (!px) return false;
    out.width = w;
    out.height = h;
    out.rgba.assign(px, px + static_cast<std::size_t>(w) * h * 4);
    stbi_image_free(px);
    return true;
}

bool imageSize(const std::uint8_t* data, std::size_t size, int& width, int& height) {
    width = height = 0;
    if (!data || size == 0 || size > 0x7fffffff) return false;
    if (isWebp(data, size)) {
#if defined(OTE_WITH_WEBP)
        return WebPGetInfo(data, size, &width, &height) != 0;
#else
        return false;
#endif
    }
    int n = 0;
    return stbi_info_from_memory(data, static_cast<int>(size), &width, &height, &n) != 0;
}

}
