#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>
#include <windows.h>

#include "ote/ote.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "capi.png";
    const int w = 960, h = 540;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL level;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, &dev, &level, &ctx))) {
        std::printf("no d3d11 device\n");
        return 2;
    }
    D3D11_TEXTURE2D_DESC cd{};
    cd.Width = w;
    cd.Height = h;
    cd.MipLevels = 1;
    cd.ArraySize = 1;
    cd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    cd.SampleDesc.Count = 1;
    cd.BindFlags = D3D11_BIND_RENDER_TARGET;
    ID3D11Texture2D* color = nullptr;
    dev->CreateTexture2D(&cd, nullptr, &color);
    ID3D11RenderTargetView* rtv = nullptr;
    dev->CreateRenderTargetView(color, nullptr, &rtv);
    D3D11_TEXTURE2D_DESC dd = cd;
    dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ID3D11Texture2D* depth = nullptr;
    dev->CreateTexture2D(&dd, nullptr, &depth);
    ID3D11DepthStencilView* dsv = nullptr;
    dev->CreateDepthStencilView(depth, nullptr, &dsv);

    ote_engine* e = ote_create_d3d11(dev, ctx, 1);
    if (!e) {
        std::printf("engine create failed\n");
        return 3;
    }
    if (argc > 2) ote_set_cache_directory(e, argv[2]);
    ote_view v{46.8529, -121.7604, 20.0, 30.0, 55.0};
    ote_set_view(e, &v);
    ote_add_point(e, 46.8529, -121.7604, 0, "Summit", 0xFFFF3030);
    auto start = std::chrono::steady_clock::now();
    int frames = 0, quiet = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(60)) {
        ote_frame_d3d11(e, w, h, rtv, nullptr, dsv, 1.0f);
        ++frames;
        quiet = ote_needs_redraw(e) ? 0 : quiet + 1;
        if (frames > 30 && quiet > 10) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    cd.Usage = D3D11_USAGE_STAGING;
    cd.BindFlags = 0;
    cd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    dev->CreateTexture2D(&cd, nullptr, &staging);
    ctx->CopyResource(staging, color);
    D3D11_MAPPED_SUBRESOURCE map{};
    ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map);
    std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const unsigned char* s = static_cast<const unsigned char*>(map.pData) + static_cast<size_t>(y) * map.RowPitch + x * 4;
            unsigned char* d = &px[(static_cast<size_t>(y) * w + x) * 4];
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = 255;
        }
    ctx->Unmap(staging, 0);
    int ok = stbi_write_png(out, w, h, 4, px.data(), w * 4);
    char mgrs[32];
    ote_to_mgrs(46.8529, -121.7604, 5, mgrs, sizeof mgrs);
    std::printf("frames %d quiet %d png %d mgrs %s\n", frames, quiet, ok, mgrs);
    ote_destroy(e);
    staging->Release();
    dsv->Release();
    depth->Release();
    rtv->Release();
    color->Release();
    ctx->Release();
    dev->Release();
    return ok ? 0 : 1;
}
