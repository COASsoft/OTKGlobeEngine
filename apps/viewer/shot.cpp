#include "shot.hpp"

#include <sokol_gfx.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cstring>
#include <vector>

#if defined(SOKOL_D3D11)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>

bool saveImagePng(sg_image image, int width, int height, const void* device, const void* context, const char* path) {
    auto* dev = static_cast<ID3D11Device*>(const_cast<void*>(device));
    auto* ctx = static_cast<ID3D11DeviceContext*>(const_cast<void*>(context));
    auto* tex = static_cast<ID3D11Texture2D*>(const_cast<void*>(sg_d3d11_query_image_info(image).tex2d));
    if (!dev || !ctx || !tex) return false;
    D3D11_TEXTURE2D_DESC desc{};
    tex->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(dev->CreateTexture2D(&desc, nullptr, &staging))) return false;
    ctx->CopyResource(staging, tex);
    D3D11_MAPPED_SUBRESOURCE map{};
    bool ok = false;
    if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map))) {
        bool bgra = desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        std::vector<unsigned char> px(static_cast<size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y) {
            const auto* row = static_cast<const unsigned char*>(map.pData) + static_cast<size_t>(y) * map.RowPitch;
            for (int x = 0; x < width; ++x) {
                unsigned char* d = &px[(static_cast<size_t>(y) * width + x) * 4];
                const unsigned char* s = row + x * 4;
                d[0] = bgra ? s[2] : s[0];
                d[1] = s[1];
                d[2] = bgra ? s[0] : s[2];
                d[3] = 255;
            }
        }
        ctx->Unmap(staging, 0);
        ok = stbi_write_png(path, width, height, 4, px.data(), width * 4) != 0;
    }
    staging->Release();
    return ok;
}
#elif defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#elif defined(__APPLE__)
#include <OpenGL/gl3.h>
#elif defined(__ANDROID__)
#include <GLES3/gl3.h>
#else
#include <GL/gl.h>
#endif

bool saveImagePng(sg_image image, int width, int height, const void*, const void*, const char* path) {
    sg_gl_image_info info = sg_gl_query_image_info(image);
    std::vector<unsigned char> px(static_cast<size_t>(width) * height * 4);
#if defined(SOKOL_GLES3)
    GLuint fb = 0;
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, info.tex[info.active_slot], 0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fb);
#else
    glBindTexture(GL_TEXTURE_2D, info.tex[info.active_slot]);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindTexture(GL_TEXTURE_2D, 0);
#endif
    std::vector<unsigned char> flipped(px.size());
    for (int y = 0; y < height; ++y)
        std::memcpy(&flipped[static_cast<size_t>(y) * width * 4], &px[static_cast<size_t>(height - 1 - y) * width * 4],
                    static_cast<size_t>(width) * 4);
    for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;
    return stbi_write_png(path, width, height, 4, flipped.data(), width * 4) != 0;
}
#else
bool saveImagePng(sg_image, int, int, const void*, const void*, const char*) { return false; }
#endif
