# Photorealistic 3D

Photorealism in OTKGlobeEngine comes from photogrammetry and photorealistic 3D Tiles drawn on top of the imagery-draped terrain. Imagery and photogrammetry already contain real lighting, so they are drawn without extra shading; CAD-style models get physically based materials and optional sun lighting.

## Sources

| Source | How to load | Offline | Notes |
| --- | --- | --- | --- |
| Your own photogrammetry or LiDAR mesh (drone, aircraft, satellite stereo) | `addTileset("area.3tz")` or a `tileset.json` folder | Yes | The airgapped option. Build 3D Tiles with OpenDroneMap, Pix4D, Agisoft Metashape, RealityCapture or Cesium's tilers; Draco meshes and KTX2 textures keep packages small. |
| Google Photorealistic 3D Tiles | `addTileset("https://tile.googleapis.com/v1/3dtiles/root.json?key=KEY")` | No | Needs the Map Tiles API. Session tokens and the key are carried to every child request. Google's terms do not allow storing the tiles, so they are not written to the offline cache. Show `Engine::credits()` and the Google logo on screen. |
| Cesium ion assets | `addTileset("ion://ASSET_ID?access_token=TOKEN")` | Partly | The endpoint, its bearer token and token refresh are handled. Content is cached, and the endpoint answer is cached too so a cached asset opens without a network. |
| Any 3D Tiles server needing headers | `TilesetOptions::headers`, `otk_add_tileset_ex` | Yes | For example `Authorization: Bearer ...` or an API-key header. |

`Engine::credits()` (`otk_credits`) returns the attributions for what is on screen, most frequent first, from glTF `asset.copyright` and ion attributions. Providers such as Google require them to be visible.

## Rendering

- Content: b3dm, i3dm, pnts, cmpt and glTF tiles, explicit and implicit tilesets, Draco, meshopt and KTX2 compression, GPU instancing.
- Materials: metallic-roughness with normal, emissive and occlusion maps, `KHR_materials_unlit`, `KHR_texture_transform`, flat normals where a mesh has none.
- `TilesetOptions::lighting`: `Auto` follows the materials (photogrammetry exported with `KHR_materials_unlit` stays unlit), `Unlit` forces unshaded rendering for photogrammetry that was exported without it, `Lit` forces shading.
- `Engine::setSunLighting(true, unixSeconds)`: models and 3D Tiles are lit by the real sun for that time and place; after sunset only ambient light remains.
- Textures get mipmaps and anisotropic filtering; KTX2 stays block-compressed on the GPU (BC7, ASTC or ETC2).

## Not yet

Shadows, image-based lighting, screen-space ambient occlusion and atmospheric haze over 3D Tiles are not implemented. Terrain shading is a fixed relief shade, not sun-driven.
