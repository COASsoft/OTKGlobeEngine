#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <d3d11.h>
#include <windows.h>

#include "otk/otk.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    const bool poolStress = argc > 1 && std::strcmp(argv[1], "--pool-stress") == 0;
    if (poolStress) {
        _putenv_s("OTK_GFX_BUFFERS", "1024");
        _putenv_s("OTK_GFX_IMAGES", "768");
        _putenv_s("OTK_GFX_VIEWS", "8192");
        _putenv_s("OTK_HOLES", "1");
        ++argv;
        --argc;
    }
    const char* out = argc > 1 ? argv[1] : "capi.png";
    const int w = 960, h = 540;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL level;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, &dev, &level, &ctx)) &&
        FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
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

    otk_engine* e = otk_create_d3d11(dev, ctx, 1);
    if (!e) {
        std::printf("engine create failed\n");
        return 3;
    }
    if (argc > 2) otk_set_cache_directory(e, argv[2]);
    if (poolStress) {
        int worst = 0, worstStop = -1;
        auto holes = [&]() {
            otk_frame_d3d11(e, w, h, rtv, nullptr, dsv, 1.0f);
            D3D11_TEXTURE2D_DESC sd = cd;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.BindFlags = 0;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ID3D11Texture2D* st = nullptr;
            dev->CreateTexture2D(&sd, nullptr, &st);
            ctx->CopyResource(st, color);
            D3D11_MAPPED_SUBRESOURCE m{};
            ctx->Map(st, 0, D3D11_MAP_READ, 0, &m);
            int blocks = 0;
            std::vector<unsigned char> img(static_cast<size_t>(w) * h * 4);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const unsigned char* s = static_cast<const unsigned char*>(m.pData) + static_cast<size_t>(y) * m.RowPitch + x * 4;
                    unsigned char* d = &img[(static_cast<size_t>(y) * w + x) * 4];
                    d[0] = s[2];
                    d[1] = s[1];
                    d[2] = s[0];
                    d[3] = 255;
                }
            for (int by = 0; by + 8 <= h; by += 8)
                for (int bx = 0; bx + 8 <= w; bx += 8) {
                    int n = 0;
                    for (int y = by; y < by + 8; ++y)
                        for (int x = bx; x < bx + 8; ++x) {
                            const unsigned char* s = static_cast<const unsigned char*>(m.pData) + static_cast<size_t>(y) * m.RowPitch + x * 4;
                            if (s[2] > 240 && s[1] < 16 && s[0] > 240) ++n;
                        }
                    if (n > 32) ++blocks;
                }
            ctx->Unmap(st, 0);
            st->Release();
            if (blocks > worst) stbi_write_png(out, w, h, 4, img.data(), w * 4);
            return blocks;
        };
        otk_set_memory_budget(e, 4096);
        for (int stop = 0; stop < 36; ++stop) {
            otk_view at{44.0 + (stop % 6) * 0.9, -122.4 + (stop / 6) * 1.1, 12.0, stop * 37.0, 72.0};
            otk_set_view(e, &at);
            for (int i = 0; i < 12; ++i) {
                otk_pan(e, w * 0.5, h * 0.75, w * 0.5 + 6.0 * (i % 3 - 1), h * 0.75 - 18.0);
                otk_frame_d3d11(e, w, h, rtv, nullptr, dsv, 1.0f);
            }
            int b = holes();
            if (b > worst) {
                worst = b;
                worstStop = stop;
            }
        }
        auto settle = std::chrono::steady_clock::now();
        int calm = 0;
        while (std::chrono::steady_clock::now() - settle < std::chrono::seconds(30) && calm < 10) {
            otk_frame_d3d11(e, w, h, rtv, nullptr, dsv, 1.0f);
            calm = otk_needs_redraw(e) ? 0 : calm + 1;
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
        int final = holes();
        const int total = (w / 8) * (h / 8);
        std::printf("%s globe stays drawn while churning past small gpu pools (worst %d of %d blocks at stop %d)\n",
                    worst * 100 < total ? "PASS" : "FAIL", worst, total, worstStop);
        std::printf("%s settled view has no holes (%d blocks)\n", final == 0 ? "PASS" : "FAIL", final);
        otk_destroy(e);
        dsv->Release();
        depth->Release();
        rtv->Release();
        color->Release();
        ctx->Release();
        dev->Release();
        return worst * 100 < total && final == 0 ? 0 : 1;
    }
    otk_view v{46.8529, -121.7604, 20.0, 30.0, 55.0};
    otk_set_view(e, &v);
    otk_add_point(e, 46.8529, -121.7604, 0, "Summit", 0xFFFF3030);
    auto start = std::chrono::steady_clock::now();
    int frames = 0, quiet = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(60)) {
        otk_frame_d3d11(e, w, h, rtv, nullptr, dsv, 1.0f);
        ++frames;
        quiet = otk_needs_redraw(e) ? 0 : quiet + 1;
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
    int fails = 0;
    auto pump = [&](int n) {
        for (int i = 0; i < n; ++i) {
            otk_frame_d3d11(e, w, h, rtv, nullptr, dsv, 1.0f);
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    };
    auto check = [&](bool c, const char* what, double got) {
        std::printf("%s %s (%.4f)\n", c ? "PASS" : "FAIL", what, got);
        if (!c) ++fails;
    };
    const double cx = w * 0.5, cy = h * 0.5;
    otk_view a{}, b{};
    otk_view flat{47.0, -122.0, 20.0, 0.0, 0.0};
    otk_set_view(e, &flat);
    pump(3);
    otk_get_view(e, &a);
    otk_pointer_down(e, 1, cx - 50, cy, OTK_POINTER_TOUCH, OTK_BUTTON_LEFT, 0);
    otk_pointer_down(e, 2, cx + 50, cy, OTK_POINTER_TOUCH, OTK_BUTTON_LEFT, 0);
    for (int i = 1; i <= 10; ++i) {
        otk_pointer_move(e, 1, cx - 50 - 15 * i, cy, 0);
        otk_pointer_move(e, 2, cx + 50 + 15 * i, cy, 0);
    }
    otk_pointer_up(e, 1, cx - 200, cy);
    otk_pointer_up(e, 2, cx + 200, cy);
    otk_get_view(e, &b);
    check(std::abs(b.gsd / a.gsd - 0.25) < 0.03, "pinch zooms in by the finger spread ratio", b.gsd / a.gsd);

    otk_view rotated{47.0, -122.0, 20.0, 30.0, 0.0};
    otk_set_view(e, &rotated);
    pump(2);
    otk_get_view(e, &a);
    otk_pointer_down(e, 3, cx, cy, OTK_POINTER_TOUCH, OTK_BUTTON_LEFT, 0);
    for (int i = 1; i <= 10; ++i) otk_pointer_move(e, 3, cx + 20 * i, cy, 0);
    otk_pointer_up(e, 3, cx + 200, cy);
    otk_get_view(e, &b);
    check(std::abs(b.azimuth - a.azimuth) < 0.01 && std::abs(b.lon - a.lon) > 1e-5, "one finger pan keeps heading", b.azimuth);

    otk_set_view(e, &flat);
    pump(2);
    otk_get_view(e, &a);
    otk_pointer_down(e, 4, cx - 100, cy, OTK_POINTER_TOUCH, OTK_BUTTON_LEFT, 0);
    otk_pointer_down(e, 5, cx + 100, cy, OTK_POINTER_TOUCH, OTK_BUTTON_LEFT, 0);
    for (int i = 1; i <= 9; ++i) {
        double t = -0.0872664626 * i;
        otk_pointer_move(e, 4, cx - 100 * std::cos(t), cy - 100 * std::sin(t), 0);
        otk_pointer_move(e, 5, cx + 100 * std::cos(t), cy + 100 * std::sin(t), 0);
    }
    otk_pointer_up(e, 4, cx - 70.71, cy + 70.71);
    otk_pointer_up(e, 5, cx + 70.71, cy - 70.71);
    otk_get_view(e, &b);
    double turned = std::fmod(b.azimuth - a.azimuth + 540.0, 360.0) - 180.0;
    check(turned > 30.0 && turned < 40.0, "two finger twist rotates with the fingers past the threshold", turned);

    otk_set_view(e, &flat);
    pump(2);
    otk_get_view(e, &a);
    otk_pointer_down(e, 6, cx - 80, cy + 100, OTK_POINTER_TOUCH, OTK_BUTTON_LEFT, 0);
    otk_pointer_down(e, 7, cx + 80, cy + 100, OTK_POINTER_TOUCH, OTK_BUTTON_LEFT, 0);
    for (int i = 1; i <= 10; ++i) {
        otk_pointer_move(e, 6, cx - 80, cy + 100 - 15 * i, 0);
        otk_pointer_move(e, 7, cx + 80, cy + 100 - 15 * i, 0);
    }
    otk_pointer_up(e, 6, cx - 80, cy - 50);
    otk_pointer_up(e, 7, cx + 80, cy - 50);
    otk_get_view(e, &b);
    check(b.tilt > 20.0 && std::abs(b.gsd / a.gsd - 1.0) < 0.05, "two finger vertical drag tilts without zooming", b.tilt);

    otk_set_view(e, &flat);
    pump(2);
    otk_get_view(e, &a);
    otk_key(e, OTK_KEY_ZOOM_IN, 1, 0);
    otk_key(e, OTK_KEY_ZOOM_IN, 0, 0);
    pump(45);
    otk_get_view(e, &b);
    check(std::abs(b.gsd / a.gsd - 0.5) < 0.02, "zoom key animates a 2x zoom", b.gsd / a.gsd);

    otk_set_view(e, &flat);
    pump(2);
    otk_get_view(e, &a);
    otk_pointer_down(e, 8, cx + 40, cy + 20, OTK_POINTER_TOUCH, OTK_BUTTON_LEFT, 0);
    otk_pointer_up(e, 8, cx + 40, cy + 20);
    otk_pointer_down(e, 9, cx + 42, cy + 21, OTK_POINTER_TOUCH, OTK_BUTTON_LEFT, 0);
    otk_pointer_up(e, 9, cx + 42, cy + 21);
    pump(45);
    otk_get_view(e, &b);
    check(std::abs(b.gsd / a.gsd - 0.5) < 0.02, "double tap animates a 2x zoom", b.gsd / a.gsd);

    otk_set_view(e, &flat);
    pump(2);
    otk_get_view(e, &a);
    otk_wheel(e, cx, cy, 0, 1, 0, 0);
    pump(30);
    otk_get_view(e, &b);
    check(b.gsd < a.gsd * 0.8, "mouse wheel zooms in", b.gsd / a.gsd);

    otk_view polar{88.5, 10.0, 20000.0, 0.0, 0.0};
    otk_set_view(e, &polar);
    pump(2);
    double minLat = 90, maxLat = -90;
    for (int k = 0; k < 6; ++k) {
        otk_pointer_down(e, 20 + k, cx, cy - 100, OTK_POINTER_MOUSE, OTK_BUTTON_LEFT, 0);
        for (int i = 1; i <= 10; ++i) {
            otk_pointer_move(e, 20 + k, cx, cy - 100 + 25 * i, 0);
            otk_get_view(e, &b);
            if (std::getenv("OTK_TEST_TRACE")) std::printf("drag %d.%d lat %.5f lon %.3f az %.2f gsd %.0f\n", k, i, b.lat, b.lon, b.azimuth, b.gsd);
            minLat = std::min(minLat, b.lat);
            maxLat = std::max(maxLat, b.lat);
        }
        otk_pointer_up(e, 20 + k, cx, cy + 150);
    }
    otk_get_view(e, &b);
    check(maxLat > 89.9 && maxLat < 90.0 && minLat > 88.0 && std::abs(b.azimuth) < 0.01, "pan reaches the pole without flipping or turning", maxLat);
    double lon0 = b.lon;
    otk_pointer_down(e, 40, cx - 100, cy, OTK_POINTER_MOUSE, OTK_BUTTON_LEFT, 0);
    otk_pointer_move(e, 40, cx + 100, cy, 0);
    otk_pointer_up(e, 40, cx + 100, cy);
    otk_get_view(e, &b);
    double dLon = std::abs(std::fmod(b.lon - lon0 + 540.0, 360.0) - 180.0);
    check(dLon <= 180.0 * 200.0 / std::min(w, h) + 0.01 && b.lat > 89.0, "sideways pan at the pole turns at a bounded rate", dLon);

    otk_view steep{44.47, -119.69, 8.0, 352.0, 78.0};
    otk_set_view(e, &steep);
    pump(3);
    double worst = 0;
    for (int k = 0; k < 4; ++k) {
        otk_get_view(e, &a);
        otk_pointer_down(e, 90 + k, cx, 20.0 + k * 4, OTK_POINTER_MOUSE, OTK_BUTTON_LEFT, 0);
        for (int i = 1; i <= 5; ++i) otk_pointer_move(e, 90 + k, cx, 20.0 + k * 4 + 4 * i, 0);
        otk_pointer_up(e, 90 + k, cx, 40.0 + k * 4);
        otk_get_view(e, &b);
        double dn = (b.lat - a.lat) * 111320.0, de = (b.lon - a.lon) * 111320.0 * std::cos(a.lat * 0.0174532925);
        worst = std::max(worst, std::hypot(dn, de));
    }
    check(worst < 2000.0, "drag near the horizon at high tilt stays bounded", worst);

    otk_view poleClose{89.99, 30.0, 20.0, 0.0, 0.0};
    otk_set_view(e, &poleClose);
    pump(2);
    otk_get_view(e, &a);
    for (int k = 0; k < 3; ++k) {
        otk_pointer_down(e, 50 + k, cx - 200, cy, OTK_POINTER_TOUCH, OTK_BUTTON_LEFT, 0);
        otk_pointer_down(e, 60 + k, cx + 200, cy, OTK_POINTER_TOUCH, OTK_BUTTON_LEFT, 0);
        for (int i = 1; i <= 10; ++i) {
            otk_pointer_move(e, 50 + k, cx - 200 + 15 * i, cy, 0);
            otk_pointer_move(e, 60 + k, cx + 200 - 15 * i, cy, 0);
        }
        otk_pointer_up(e, 50 + k, cx - 50, cy);
        otk_pointer_up(e, 60 + k, cx + 50, cy);
    }
    otk_get_view(e, &b);
    double spin = std::abs(std::fmod(b.lon - a.lon + 540.0, 360.0) - 180.0);
    check(b.gsd > a.gsd * 10.0 && spin < 5.0, "pinch at the pole zooms without spinning the view", spin);

    char mgrs[32];
    otk_to_mgrs(46.8529, -121.7604, 5, mgrs, sizeof mgrs);
    std::printf("frames %d quiet %d png %d mgrs %s\n", frames, quiet, ok, mgrs);
    otk_destroy(e);
    staging->Release();
    dsv->Release();
    depth->Release();
    rtv->Release();
    color->Release();
    ctx->Release();
    dev->Release();
    return ok && fails == 0 ? 0 : 1;
}
