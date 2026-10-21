# VR, AR and stereo

OTKGlobeEngine does not talk to headsets itself. It renders any number of eye views from poses and fields of view a host hands it, which is exactly what an OpenXR (or other XR runtime) loop produces. The host owns the XR session, swapchains and input; the engine owns the map.

## What the engine provides

| API | C ABI | Purpose |
| --- | --- | --- |
| `Engine::setXrAnchor({lat, lon, alt, heading, scale})` | `otk_set_xr_anchor` | Places the XR space on the globe. XR +X points right, +Y up, -Z toward `heading` (degrees from true north). `alt` is where the XR origin sits (ground elevation for a STAGE space whose floor is y = 0). |
| `Engine::frameXr(views, targets, count)` | `otk_frame_xr` | Renders every view. Each view has an XR-space pose (position in metres, orientation quaternion x, y, z, w) and OpenXR-style angles (left, right, up, down in radians, left and down negative). |
| `Engine::frameStereo(swapchain, separation)` | - | Side-by-side stereo from the normal camera (3D displays, cross-eye checks, recording). `separation <= 0` picks hyper-stereo from the camera height. |

Tile selection, loading and uploads run once per frame for a camera that covers all eyes; each eye then only re-draws the same tiles with its own matrices. Stereo costs one update plus two draws, not two full frames.

`scale` turns the world into a sand table: with `scale = 2000`, one metre in the room is two kilometres on the map and eye separation grows with it, so terrain looks like a physical model on the table.

## OpenXR host outline (Windows, D3D11)

1. Create the D3D11 device for the OpenXR system (`XR_KHR_D3D11_enable`), then `otk_create_d3d11(device, context, 1)`.
2. Create one colour swapchain per eye with `DXGI_FORMAT_B8G8R8A8_UNORM` (the engine's D3D11 colour format) and a matching `DXGI_FORMAT_D24_UNORM_S8_UINT` depth texture per eye. Make a render-target view for every swapchain image and a depth-stencil view for each depth texture.
3. `otk_set_xr_anchor(e, lat, lon, groundAlt, heading, 1.0)` once, and again whenever the user teleports or changes scale.
4. Every frame:

```c
xrWaitFrame(...); xrBeginFrame(...);
xrLocateViews(session, &locateInfo, &viewState, 2, &n, xrViews);
otk_xr_view v[2] = {0};
for (int i = 0; i < 2; ++i) {
    uint32_t image; xrAcquireSwapchainImage(swapchain[i], NULL, &image); xrWaitSwapchainImage(swapchain[i], &wait);
    v[i].position[0] = xrViews[i].pose.position.x;  /* y, z likewise */
    v[i].orientation[0] = xrViews[i].pose.orientation.x;  /* y, z, w likewise */
    v[i].angle_left = xrViews[i].fov.angleLeft;  /* right, up, down likewise */
    v[i].width = eyeWidth; v[i].height = eyeHeight;
    v[i].render_view = rtv[i][image];
    v[i].depth_stencil_view = dsv[i];
}
otk_frame_xr(e, v, 2, eyeWidth, eyeHeight, 1.0f);
/* release images, xrEndFrame with an XrCompositionLayerProjection */
```

For a single double-wide swapchain, give both views the same `render_view` and set `x`/`width` per eye; the engine clears once and draws each eye into its viewport.

On Android headsets (Quest and other OpenXR devices) the same flow uses `XR_KHR_opengl_es_enable`, `otk_create_gl(1, 1)` and `gl_framebuffer` instead of the D3D11 views; the Java binding has `nativeFrameXr` for it. visionOS (Metal and CompositorServices) is not covered yet.

## Checking without a headset

`otk_viewer --stereo 0` renders side-by-side stereo in the window or with `--shot`. This exercises the same multi-view path as `frameXr`. Headset rendering itself has not been tested on hardware yet.

## Limits

- No reprojection or foveated rendering; keep frame times inside the headset's budget (11 ms at 90 Hz) by raising `maxScreenSpaceError` for 3D Tiles and using the memory budget.
- Labels and icons are drawn per eye at their projected position, so they sit at the right depth, but they are not yet billboarded toward the head for close range.
- Hand and controller input belongs to the host; map interactions can be driven through the normal engine API (fly-to, picking with `hitTest`).
