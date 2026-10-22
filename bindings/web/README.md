# otkglobeengine

A 3D map of the whole Earth for your web page. It shows satellite imagery, terrain, roads, place names and 3D buildings, and it runs in the browser with WebAssembly and WebGL 2.

## Install

```
npm install otkglobeengine
```

## Show a map

```html
<canvas id="map" style="width: 100%; height: 100vh"></canvas>
<script type="module">
  import { OTKGlobe } from 'otkglobeengine';

  const globe = await OTKGlobe.create('#map', {
    view: { lat: 47.61, lon: -122.35, metersPerPixel: 2, tilt: 45 },
  });
</script>
```

That is all you need. Drag to move, scroll to zoom, right-drag to tilt and turn. Zoom, compass, 2D/3D and layer buttons sit in the bottom-right corner.

## One server setting you must add

The engine uses several threads, and browsers only allow that on pages sent with these two headers:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

Examples:

- **Vite** (`vite.config.js`): `server: { headers: { 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp' } }`, and add `optimizeDeps: { exclude: ['otkglobeengine'] }`.
- **Netlify** (`_headers` file): `/*` followed by the two header lines.
- **nginx**: `add_header Cross-Origin-Opener-Policy same-origin;` and `add_header Cross-Origin-Embedder-Policy require-corp;`

Without them `OTKGlobe.create` stops with an error that says what is missing.

## Where the engine files are loaded from

The package has three files that must be served together: `index.js`, `otkglobeengine.js` and `otkglobeengine.wasm`. Bundlers such as Vite handle this for you. If you copy the files somewhere else, pass that folder:

```js
await OTKGlobe.create('#map', { baseUrl: '/static/otk/' });
```

## Common tasks

```js
globe.flyTo({ lat: 46.85, lon: -121.76, metersPerPixel: 20, tilt: 60 }, 3);

globe.setLabels(false);
globe.setBuildings(false);
globe.setTerrain(false);

globe.basemaps();
globe.setBasemap(1);

globe.addLayer({ name: 'OpenStreetMap', url: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png' });

await globe.open(fileFromAnInputOrDrop);
await globe.open('/data/route.gpx');

globe.downloadRoads();

globe.setControls(0);

globe.destroy();
```

- `open` reads KML/KMZ, GeoJSON, GPX, Shapefile (zipped), CoT, CSV, MBTiles, GeoPackage, glTF models and more.
- Roads, street names and 3D buildings show up for an area after `downloadRoads()` (or the "Download roads" button in the layers panel). They are kept, so the area keeps working without internet.
- `setControls` takes a mask from `Controls` (`Controls.Zoom | Controls.Compass`, for example) to keep only some buttons.

TypeScript types are included.

## Browser support

Current Chrome, Edge, Firefox and Safari (16.4 or newer) on desktop and mobile. WebGL 2 is required.

## License

Apache 2.0. Map data shown by default comes from Esri, OpenStreetMap contributors and others; the credits line at the bottom of the map shows them, so keep it visible (or show `globe.credits()` yourself).

More: https://grangedevgroup-code.github.io/OTKGlobeEngine/
