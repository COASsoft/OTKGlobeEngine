import createOTKModule from './otkglobeengine.js';

export const Controls = Object.freeze({
  None: 0,
  Zoom: 1,
  Compass: 2,
  Tilt: 4,
  Layers: 8,
  Scale: 16,
  Attribution: 32,
  All: 63,
});

export const SourceType = Object.freeze({
  XYZ: 0,
  TMS: 1,
  WMS: 2,
  MBTiles: 3,
  GeoPackage: 4,
  OSMDroid: 5,
  GDAL: 6,
  VectorTiles: 7,
});

let modulePromise = null;

function loadModule(options) {
  if (!modulePromise) {
    const base = options.baseUrl ?? new URL('.', import.meta.url).href;
    modulePromise = createOTKModule({
      locateFile: (path) => new URL(path, base).href,
      mainScriptUrlOrBlob: new URL('otkglobeengine.js', base).href,
      print: options.log ?? (() => {}),
      printErr: options.log ?? (() => {}),
    });
  }
  return modulePromise;
}

function modifiers(e) {
  return (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0) | (e.metaKey ? 8 : 0);
}

const keyCodes = {
  ArrowLeft: 0,
  ArrowRight: 1,
  ArrowUp: 2,
  ArrowDown: 3,
  '+': 4,
  '=': 4,
  '-': 5,
  q: 6,
  e: 7,
  PageUp: 8,
  PageDown: 9,
  n: 10,
  t: 11,
};

export class OTKGlobe {
  static async create(canvas, options = {}) {
    if (typeof canvas === 'string') canvas = document.querySelector(canvas);
    if (!(canvas instanceof HTMLCanvasElement)) throw new Error('OTKGlobe needs a <canvas> element');
    if (!globalThis.crossOriginIsolated) {
      throw new Error('OTKGlobe needs a cross-origin isolated page: serve it with the headers ' +
        '"Cross-Origin-Opener-Policy: same-origin" and "Cross-Origin-Embedder-Policy: require-corp"');
    }
    const M = await loadModule(options);
    if (!canvas.id) canvas.id = `otk-canvas-${Math.random().toString(36).slice(2)}`;
    const selector = `#${canvas.id}`;
    const engine = M.ccall('otk_web_create', 'number', ['string', 'number'], [selector, options.antialias === false ? 0 : 1]);
    if (!engine) throw new Error('WebGL 2 is not available in this browser');
    return new OTKGlobe(M, engine, canvas, options);
  }

  constructor(M, engine, canvas, options) {
    this.M = M;
    this.engine = engine;
    this.canvas = canvas;
    this.running = true;
    this.listeners = [];
    this.viewPtr = M._malloc(40);
    M.ccall('otk_set_cache_directory', null, ['number', 'string'], [engine, options.cacheDirectory ?? '/otk-cache']);
    if (options.view) this.setView(options.view);
    if (options.controls !== undefined) this.setControls(options.controls);
    if (options.input !== false) this.attachInput();
    this.resize();
    if (typeof ResizeObserver !== 'undefined') {
      this.resizeObserver = new ResizeObserver(() => this.resize());
      this.resizeObserver.observe(canvas);
    }
    const tick = () => {
      if (!this.running) return;
      this.resize();
      M._otk_frame_gl(this.engine, canvas.width, canvas.height, 0, this.dpr);
      requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  }

  resize() {
    this.dpr = window.devicePixelRatio || 1;
    const w = Math.max(1, Math.round(this.canvas.clientWidth * this.dpr));
    const h = Math.max(1, Math.round(this.canvas.clientHeight * this.dpr));
    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = w;
      this.canvas.height = h;
    }
  }

  point(e) {
    const r = this.canvas.getBoundingClientRect();
    return [(e.clientX - r.left) * this.dpr, (e.clientY - r.top) * this.dpr];
  }

  listen(target, type, fn, opts) {
    target.addEventListener(type, fn, opts);
    this.listeners.push(() => target.removeEventListener(type, fn, opts));
  }

  attachInput() {
    const c = this.canvas;
    const M = this.M;
    c.style.touchAction = 'none';
    if (!c.hasAttribute('tabindex')) c.tabIndex = 0;
    const kind = (e) => (e.pointerType === 'touch' ? 1 : e.pointerType === 'pen' ? 2 : 0);
    const button = (e) => (e.button === 0 ? 1 : e.button === 2 ? 2 : e.button === 1 ? 3 : 0);
    this.listen(c, 'pointerdown', (e) => {
      c.setPointerCapture(e.pointerId);
      c.focus({ preventScroll: true });
      const [x, y] = this.point(e);
      M._otk_pointer_down(this.engine, e.pointerId, x, y, kind(e), button(e), modifiers(e));
      e.preventDefault();
    });
    this.listen(c, 'pointermove', (e) => {
      const [x, y] = this.point(e);
      M._otk_pointer_move(this.engine, e.pointerId, x, y, modifiers(e));
    });
    this.listen(c, 'pointerup', (e) => {
      const [x, y] = this.point(e);
      M._otk_pointer_up(this.engine, e.pointerId, x, y);
    });
    this.listen(c, 'pointercancel', (e) => M._otk_pointer_cancel(this.engine, e.pointerId));
    this.listen(c, 'contextmenu', (e) => e.preventDefault());
    this.listen(c, 'wheel', (e) => {
      const [x, y] = this.point(e);
      const scale = e.deltaMode === 1 ? 1 / 3 : e.deltaMode === 2 ? 3 : 1 / 100;
      const precise = e.deltaMode === 0 && Math.abs(e.deltaY) < 50 ? 1 : 0;
      M._otk_wheel(this.engine, x, y, -e.deltaX * scale, -e.deltaY * scale, precise, modifiers(e));
      e.preventDefault();
    }, { passive: false });
    const key = (down) => (e) => {
      const code = keyCodes[e.key] ?? keyCodes[e.key.toLowerCase()];
      if (code === undefined) return;
      M._otk_key(this.engine, code, down ? 1 : 0, modifiers(e));
      e.preventDefault();
    };
    this.listen(c, 'keydown', key(true));
    this.listen(c, 'keyup', key(false));
  }

  writeView(v) {
    const H = this.M.HEAPF64;
    const i = this.viewPtr / 8;
    H[i] = v.lat;
    H[i + 1] = v.lon;
    H[i + 2] = v.metersPerPixel ?? v.gsd ?? 100;
    H[i + 3] = v.heading ?? v.azimuth ?? 0;
    H[i + 4] = v.tilt ?? 0;
    return this.viewPtr;
  }

  setView(view) {
    this.M._otk_set_view(this.engine, this.writeView(view));
  }

  flyTo(view, seconds = 2) {
    this.M._otk_fly_to(this.engine, this.writeView(view), seconds);
  }

  getView() {
    this.M._otk_get_view(this.engine, this.viewPtr);
    const H = this.M.HEAPF64;
    const i = this.viewPtr / 8;
    return { lat: H[i], lon: H[i + 1], metersPerPixel: H[i + 2], heading: H[i + 3], tilt: H[i + 4] };
  }

  setControls(mask) {
    this.M._otk_set_controls(this.engine, mask >>> 0);
  }

  setLabels(on) {
    this.M._otk_set_labels(this.engine, on ? 1 : 0);
  }

  setBuildings(on) {
    this.M._otk_set_buildings(this.engine, on ? 1 : 0);
  }

  setTerrain(on) {
    this.M._otk_set_terrain(this.engine, on ? 1 : 0);
  }

  setBasemap(index) {
    this.M._otk_set_basemap(this.engine, index);
  }

  basemaps() {
    const n = this.M._otk_basemap_count(this.engine);
    const buf = this.M._malloc(256);
    const out = [];
    for (let i = 0; i < n; i++) {
      this.M._otk_basemap_name(this.engine, i, buf, 256);
      out.push(this.M.UTF8ToString(buf));
    }
    this.M._free(buf);
    return out;
  }

  addLayer({ type = SourceType.XYZ, name = 'Layer', url, minZoom = 0, maxZoom = 19 }) {
    return this.M.ccall('otk_add_layer', 'number', ['number', 'number', 'string', 'string', 'number', 'number'],
      [this.engine, type, name, url, minZoom, maxZoom]);
  }

  clearLayers() {
    this.M._otk_clear_layers(this.engine);
  }

  downloadRoads(region) {
    const r = region ?? this.viewRegion();
    return this.M._otk_download_labels(this.engine, r.west, r.south, r.east, r.north, 14);
  }

  viewRegion() {
    const p = this.M._malloc(32);
    this.M._otk_view_region(this.engine, p, p + 8, p + 16, p + 24);
    const i = p / 8;
    const H = this.M.HEAPF64;
    const r = { west: H[i], south: H[i + 1], east: H[i + 2], north: H[i + 3] };
    this.M._free(p);
    return r;
  }

  async open(file, name) {
    let bytes;
    if (file instanceof Blob) {
      name = name ?? file.name ?? 'data';
      bytes = new Uint8Array(await file.arrayBuffer());
    } else if (typeof file === 'string') {
      const res = await fetch(file);
      if (!res.ok) throw new Error(`Could not fetch ${file}: ${res.status}`);
      name = name ?? decodeURIComponent(new URL(file, location.href).pathname.split('/').pop() || 'data');
      bytes = new Uint8Array(await res.arrayBuffer());
    } else {
      bytes = file instanceof Uint8Array ? file : new Uint8Array(file);
      name = name ?? 'data';
    }
    const FS = this.M.FS;
    try { FS.mkdir('/otk-files'); } catch {}
    const path = `/otk-files/${name.replace(/[\\/]/g, '_')}`;
    FS.writeFile(path, bytes);
    const out = this.M._malloc(64);
    const ok = this.M.ccall('otk_open', 'number', ['number', 'string', 'number'], [this.engine, path, out]);
    const H = this.M.HEAP32;
    const D = this.M.HEAPF64;
    const result = { ok: ok !== 0, items: H[out / 4 + 2] };
    if (H[out / 4 + 3]) {
      result.region = { west: D[out / 8 + 2], south: D[out / 8 + 3], east: D[out / 8 + 4], north: D[out / 8 + 5] };
    }
    this.M._free(out);
    return result;
  }

  credits() {
    const size = 4096;
    const buf = this.M._malloc(size);
    this.M._otk_credits(this.engine, buf, size);
    const text = this.M.UTF8ToString(buf);
    this.M._free(buf);
    return text;
  }

  destroy() {
    this.running = false;
    this.resizeObserver?.disconnect();
    for (const off of this.listeners) off();
    this.listeners = [];
    this.M._otk_destroy(this.engine);
    this.M._free(this.viewPtr);
    this.engine = 0;
  }
}

export default OTKGlobe;
