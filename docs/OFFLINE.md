# Offline and airgapped operation

OpenTAKEngine is built to keep showing maps when the network is slow, gone, or was never there. This page describes what the engine does on its own and how to prepare data for a network without internet access.

## What happens without a network

| Situation | Behaviour |
| --- | --- |
| Tile is in the cache and fresh | Served from the local loader pool, no network request. |
| Tile is in the cache but expired | Served immediately. A low-priority background refresh replaces it in the cache when the server is reachable; nothing waits for it. |
| Tile is not cached and the host is unreachable | After three transport failures (DNS, connect or timeout) with no success for 2 s, the host is marked down. Further requests to it fail at once instead of waiting for timeouts. The renderer falls back to the deepest cached ancestor ("overzoom"), so the view stays sharp where data exists. |
| Host comes back | Idle loader threads probe down hosts in the background (backoff 10 s, doubling to 2 min). The first successful response clears every "unavailable" mark, and visible tiles reload without user action. Android, Apple and .NET hosts also call `networkChanged()` on OS connectivity changes, which retries at once. |
| No imagery at all (empty cache, first start in an airgapped network) | A built-in NASA Blue Marble world image (2048 x 1024, public domain, 350 KB) is drawn wherever imagery is unavailable, including the poles. Disable with `setOfflineBackdrop(false)` or `-DOTE_EMBED_BACKDROP=OFF`. |
| Elevation unavailable | Terrain tiles complete with whatever elevation is cached (flat where none), so imagery refinement is never blocked by missing terrain. |
| 3D Tiles content missing or undecodable | The parent tile stays drawn instead of leaving a hole. Network failures are retried after 30 s or as soon as a down host answers again; missing files, HTTP 4xx and unsupported content (WebP or AVIF textures without a fallback) are not. `Stats::tilesetFailed` counts them. `.3tz` archives are read from disk on demand, not loaded into memory. |
| `setOffline(true)` | No network requests at all; misses are reported as unavailable until `setOffline(false)`. |

Connect timeouts are 5 s on every platform (WinHTTP, libcurl, Android, NSURLSession request timeout 15 s).

## Cache

- Location: the host's choice via `setCacheDirectory`, otherwise a default per platform: `%LOCALAPPDATA%\OpenTAKEngine` (Windows), `~/Library/Application Support/OpenTAKEngine` (macOS, iOS), `$XDG_CACHE_HOME/OpenTAKEngine` or `~/.cache/OpenTAKEngine` (Linux). The Android view uses `getNoBackupFilesDir()/opentakengine` and the iOS view Application Support (excluded from backup), so the OS never purges downloaded regions.
- Format: one SQLite file `tiles.sqlite` (WAL). Imagery, elevation, vector tiles, TileJSON documents and 3D Tiles content fetched over HTTP all live in it.
- Keys ignore credentials in the URL (`key`, `apikey`, `access_token`, `token`, ...), so rotating an API key keeps the cache.
- Expiry: 7 days by default, per layer with `ImageryLayer::expirySeconds` (negative never expires). MOBAC `<tileUpdate>` is honoured (milliseconds; `0` caches indefinitely).
- Size: unlimited by default. `setCacheLimit(megabytes)` evicts the oldest tiles; tiles from region downloads and imports are pinned and never evicted.
- A corrupt cache file is moved aside (`tiles.sqlite.corrupt-<time>`) and a new one is created.

## Preparing data for an airgapped network

1. On a connected machine, download the area: `downloadRegion(layer, region, minZoom, maxZoom)` (viewer: `--download w,s,e,n,minz,maxz`). Downloads pause while the server is unreachable and retry failed tiles up to 8 times. `countRegionTiles` gives the size first.
2. Export it: `exportTiles(layer, region, minZoom, maxZoom, "area.mbtiles")` (viewer: `--export w,s,e,n,minz,maxz,path`). Web Mercator layers export to MBTiles or GeoPackage, geographic layers (EPSG:4326) to GeoPackage. Exporting to an existing file appends.
3. Carry the file across, then either
   - add it as its own layer (`SourceType::MBTiles` / `GeoPackage`), or
   - import it into the cache of the matching online layer: `importTiles(layer, "area.mbtiles")` (viewer: `--import path`). The layer then works unchanged, with the same name and stacking, without network access. Imported tiles are pinned.

The same works for elevation sources and vector tiles, since they share the cache.

## Checking it

- `Stats::unreachableHosts`, `Stats::staleTiles`, `Engine::cacheBytes()`; the viewer writes them to the `.txt` next to a `--shot`.
- Simulate an airgapped network with `OTE_NET_UNREACHABLE=1` (add `OTE_NET_DELAY_MS=5000` to make every request hang for 5 s first, like a blackholed route).

Measured on the development PC (GTX 950), Olympia at 3 m/px, cache older than its expiry:

| Run | Result |
| --- | --- |
| Blackholed network, expired cache | Full imagery, settled in 13.6 s (5 s spent on the first timeouts) |
| `--offline 1`, same cache | Full imagery, settled in 6.9 s |
| Blackholed network, empty cache | Blue Marble globe, no blank surfaces, settled in 10.5 s |
| Blackholed network, empty cache + imported 9 MB MBTiles | Full imagery, settled in 6.2 s |
| Online, expired cache | Full imagery in 8.1 s, 898 tiles refreshed in the background meanwhile |
