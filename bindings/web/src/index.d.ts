export declare const Controls: {
  readonly None: 0;
  readonly Zoom: 1;
  readonly Compass: 2;
  readonly Tilt: 4;
  readonly Layers: 8;
  readonly Scale: 16;
  readonly Attribution: 32;
  readonly All: 63;
};

export declare const SourceType: {
  readonly XYZ: 0;
  readonly TMS: 1;
  readonly WMS: 2;
  readonly MBTiles: 3;
  readonly GeoPackage: 4;
  readonly OSMDroid: 5;
  readonly GDAL: 6;
  readonly VectorTiles: 7;
};

export interface View {
  lat: number;
  lon: number;
  metersPerPixel?: number;
  heading?: number;
  tilt?: number;
}

export interface Region {
  west: number;
  south: number;
  east: number;
  north: number;
}

export interface CreateOptions {
  view?: View;
  controls?: number;
  input?: boolean;
  antialias?: boolean;
  baseUrl?: string;
  cacheDirectory?: string;
  log?: (text: string) => void;
}

export interface LayerOptions {
  type?: number;
  name?: string;
  url: string;
  minZoom?: number;
  maxZoom?: number;
  opacity?: number;
}

export interface OpenResult {
  ok: boolean;
  items: number;
  region?: Region;
}

export declare class OTKGlobe {
  static create(canvas: HTMLCanvasElement | string, options?: CreateOptions): Promise<OTKGlobe>;
  readonly canvas: HTMLCanvasElement;
  setView(view: View): void;
  flyTo(view: View, seconds?: number): void;
  getView(): Required<View>;
  viewRegion(): Region;
  setControls(mask: number): void;
  setLabels(on: boolean): void;
  setBuildings(on: boolean): void;
  setTerrain(on: boolean): void;
  basemaps(): string[];
  setBasemap(index: number): void;
  addLayer(layer: LayerOptions): number;
  clearLayers(): void;
  downloadRoads(region?: Region): number;
  open(file: Blob | ArrayBuffer | Uint8Array | string, name?: string): Promise<OpenResult>;
  credits(): string;
  destroy(): void;
}

export default OTKGlobe;
