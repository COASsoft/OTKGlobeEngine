using System;
using System.Runtime.InteropServices;

namespace OTKGlobeEngine
{
    public enum Source
    {
        Url = 0,
        Wms = 1,
        Wmts = 2,
        MBTiles = 3,
        GeoPackage = 4,
        Osmdroid = 5,
        Gdal = 6,
        Mvt = 7,
    }

    public enum TilesetLighting
    {
        Auto = 0,
        Lit = 1,
        Unlit = 2,
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct SunMoon
    {
        public double SunAzimuth, SunElevation;
        public double MoonAzimuth, MoonElevation, MoonIllumination, MoonPhaseAngle;
        public int MoonWaxing;
        public double SunLux, MoonLux, TotalLux;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct Twilight
    {
        public double AstronomicalDawn, NauticalDawn, CivilDawn, Sunrise, Sunset, CivilDusk, NauticalDusk, AstronomicalDusk, Moonrise, Moonset;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct XrView
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)] public double[] Position;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)] public double[] Orientation;
        public double AngleLeft, AngleRight, AngleUp, AngleDown;
        public int X, Y, Width, Height;
        public IntPtr RenderView;
        public IntPtr ResolveView;
        public IntPtr DepthStencilView;
        public uint GlFramebuffer;
    }

    public enum PointerType
    {
        Mouse = 0,
        Touch = 1,
        Pen = 2,
    }

    public enum MouseButton
    {
        None = 0,
        Left = 1,
        Right = 2,
        Middle = 3,
    }

    [Flags]
    public enum Modifiers
    {
        None = 0,
        Shift = 1,
        Ctrl = 2,
        Alt = 4,
        Super = 8,
    }

    public enum MapKey
    {
        Left = 0,
        Right,
        Up,
        Down,
        ZoomIn,
        ZoomOut,
        RotateLeft,
        RotateRight,
        TiltUp,
        TiltDown,
        ResetNorth,
        ResetTilt,
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct DownloadStatus
    {
        public long Total;
        public long Done;
        public long Failed;
        public long Retrying;
        public int Finished;
        public int Cancelled;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct View
    {
        public double Lat;
        public double Lon;
        public double Gsd;
        public double Azimuth;
        public double Tilt;
    }

    public static class Native
    {
        const string Lib = "otkglobeengine_c";

        [DllImport(Lib)] public static extern IntPtr otk_create_gl(int gles, int sampleCount);
        [DllImport(Lib)] public static extern IntPtr otk_create_d3d11(IntPtr device, IntPtr context, int sampleCount);
        [DllImport(Lib)] public static extern IntPtr otk_create_metal(IntPtr device, int sampleCount);
        [DllImport(Lib)] public static extern void otk_destroy(IntPtr e);
        [DllImport(Lib)] public static extern void otk_frame_gl(IntPtr e, int width, int height, uint framebuffer, float dpi);
        [DllImport(Lib)] public static extern void otk_frame_d3d11(IntPtr e, int width, int height, IntPtr renderView, IntPtr resolveView, IntPtr depthStencilView, float dpi);
        [DllImport(Lib)] public static extern void otk_frame_metal(IntPtr e, int width, int height, IntPtr drawable, IntPtr depth, IntPtr msaa, float dpi);
        [DllImport(Lib)] public static extern int otk_needs_redraw(IntPtr e);
        [DllImport(Lib)] public static extern double otk_next_frame_delay(IntPtr e);
        [DllImport(Lib)] public static extern void otk_set_cache_directory(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string dir);
        [DllImport(Lib)] public static extern void otk_set_offline(IntPtr e, int offline);
        [DllImport(Lib)] public static extern void otk_set_cache_limit(IntPtr e, int megabytes);
        [DllImport(Lib)] public static extern long otk_cache_bytes(IntPtr e);
        [DllImport(Lib)] public static extern void otk_network_changed(IntPtr e);
        [DllImport(Lib)] public static extern void otk_set_offline_backdrop(IntPtr e, int enabled);
        [DllImport(Lib)] public static extern long otk_count_region_tiles(IntPtr e, int layer, double west, double south, double east, double north, int minZoom, int maxZoom);
        [DllImport(Lib)] public static extern int otk_download_region(IntPtr e, int layer, double west, double south, double east, double north, int minZoom, int maxZoom);
        [DllImport(Lib)] public static extern void otk_get_download_status(IntPtr e, int id, out DownloadStatus status);
        [DllImport(Lib)] public static extern void otk_cancel_download(IntPtr e, int id);
        [DllImport(Lib)] public static extern long otk_export_tiles(IntPtr e, int layer, double west, double south, double east, double north, int minZoom, int maxZoom, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(Lib)] public static extern long otk_import_tiles(IntPtr e, int layer, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(Lib)] public static extern double otk_geodesic_inverse(double lat1, double lon1, double lat2, double lon2, out double initialBearing, out double finalBearing);
        [DllImport(Lib)] public static extern void otk_geodesic_direct(double lat, double lon, double bearing, double distance, out double outLat, out double outLon);
        [DllImport(Lib)] public static extern double otk_magnetic_declination(double lat, double lon, double altitudeM, double decimalYear);
        [DllImport(Lib)] public static extern void otk_sun_moon_at(double unixSeconds, double lat, double lon, out SunMoon result);
        [DllImport(Lib)] public static extern void otk_sun_moon_times(double startUnixSeconds, double lat, double lon, double hours, out Twilight result);
        [DllImport(Lib)] public static extern ulong otk_add_tileset_ex(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string pathOrUrl, double maxSse,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string headers, int lighting, int cacheOffline);
        [DllImport(Lib)] public static extern int otk_credits(IntPtr e, byte[] buffer, int size);
        [DllImport(Lib)] public static extern void otk_set_sun_lighting(IntPtr e, int enabled, double unixSeconds);
        [DllImport(Lib)] public static extern void otk_set_xr_anchor(IntPtr e, double lat, double lon, double alt, double headingDeg, double scale);
        [DllImport(Lib)] public static extern void otk_frame_xr(IntPtr e, [In] XrView[] views, int count, int targetWidth, int targetHeight, float dpiScale);
        [DllImport(Lib)] public static extern double otk_grid_convergence(double lat, double lon);
        [DllImport(Lib)] public static extern int otk_format_coordinate(double lat, double lon, int format, int precision, byte[] output, int capacity);
        [DllImport(Lib)] public static extern int otk_parse_coordinate([MarshalAs(UnmanagedType.LPUTF8Str)] string text, out double lat, out double lon);
        [DllImport(Lib)] public static extern ulong otk_add_symbol(IntPtr e, double lat, double lon, double alt, [MarshalAs(UnmanagedType.LPUTF8Str)] string sidcOrCotType, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);
        [DllImport(Lib)] public static extern int otk_add_range_rings(IntPtr e, double lat, double lon, double spacingM, int count, int radials, uint argb);
        [DllImport(Lib)] public static extern int otk_add_bullseye(IntPtr e, double lat, double lon, double radiusM, int rings, double bearingStep, int reference, int unit, uint argb);
        [DllImport(Lib)] public static extern int otk_add_range_bearing(IntPtr e, double lat1, double lon1, double lat2, double lon2, int reference, int unit, int distanceUnit, uint argb);
        [DllImport(Lib)] public static extern int otk_add_contours_in_view(IntPtr e, double interval, int majorEvery, uint argb);
        [DllImport(Lib)] public static extern void otk_set_contours(IntPtr e, int enabled, double interval, int unit);
        [DllImport(Lib)] public static extern int otk_contours(IntPtr e);
        [DllImport(Lib)] public static extern void otk_set_terrain_slope(IntPtr e, int enabled, float opacity, double maxSlopeDegrees);
        [DllImport(Lib)] public static extern void otk_set_grid_type(IntPtr e, int type);
        [DllImport(Lib)] public static extern void otk_set_memory_budget(IntPtr e, int megabytes);
        [DllImport(Lib)] public static extern void otk_clear_layers(IntPtr e);
        [DllImport(Lib)] public static extern int otk_add_layer(IntPtr e, Source type, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, [MarshalAs(UnmanagedType.LPUTF8Str)] string urlOrPath, int minZoom, int maxZoom, float opacity);
        [DllImport(Lib)] public static extern int otk_add_wms_layer(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, [MarshalAs(UnmanagedType.LPUTF8Str)] string url, [MarshalAs(UnmanagedType.LPUTF8Str)] string layers, int srid, [MarshalAs(UnmanagedType.LPUTF8Str)] string version, float opacity);
        [DllImport(Lib)] public static extern int otk_load_mobac(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string xmlOrPath);
        [DllImport(Lib)] public static extern void otk_use_default_layers(IntPtr e);
        [DllImport(Lib)] public static extern void otk_set_terrain(IntPtr e, int enabled);
        [DllImport(Lib)] public static extern void otk_set_terrain_exaggeration(IntPtr e, double factor);
        [DllImport(Lib)] public static extern void otk_set_depth_perception(IntPtr e, int enabled);
        [DllImport(Lib)] public static extern void otk_set_terrain_transparency(IntPtr e, float alpha);
        [DllImport(Lib)] public static extern void otk_add_elevation_directory(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(Lib)] public static extern double otk_elevation(IntPtr e, double lat, double lon);
        [DllImport(Lib)] public static extern void otk_set_view(IntPtr e, ref View v);
        [DllImport(Lib)] public static extern void otk_get_view(IntPtr e, out View v);
        [DllImport(Lib)] public static extern void otk_fly_to(IntPtr e, ref View v, double seconds);
        [DllImport(Lib)] public static extern int otk_pick(IntPtr e, double x, double y, out double lat, out double lon);
        [DllImport(Lib)] public static extern void otk_pan(IntPtr e, double x0, double y0, double x1, double y1);
        [DllImport(Lib)] public static extern void otk_zoom_at(IntPtr e, double x, double y, double factor);
        [DllImport(Lib)] public static extern void otk_rotate(IntPtr e, double degrees);
        [DllImport(Lib)] public static extern void otk_tilt(IntPtr e, double degrees);
        [DllImport(Lib)] public static extern void otk_rotate_at(IntPtr e, double x, double y, double degrees);
        [DllImport(Lib)] public static extern void otk_set_heading_locked_pan(IntPtr e, int enabled);
        [DllImport(Lib)] public static extern void otk_pointer_down(IntPtr e, int id, double x, double y, PointerType type, MouseButton button, Modifiers modifiers);
        [DllImport(Lib)] public static extern void otk_pointer_move(IntPtr e, int id, double x, double y, Modifiers modifiers);
        [DllImport(Lib)] public static extern void otk_pointer_up(IntPtr e, int id, double x, double y);
        [DllImport(Lib)] public static extern void otk_pointer_cancel(IntPtr e, int id);
        [DllImport(Lib)] public static extern void otk_pointer_cancel_all(IntPtr e);
        [DllImport(Lib)] public static extern void otk_wheel(IntPtr e, double x, double y, double dx, double dy, int precise, Modifiers modifiers);
        [DllImport(Lib)] public static extern void otk_magnify(IntPtr e, double x, double y, double scale);
        [DllImport(Lib)] public static extern void otk_rotate_gesture(IntPtr e, double x, double y, double degrees);
        [DllImport(Lib)] public static extern void otk_key(IntPtr e, MapKey key, int down, Modifiers modifiers);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void TapCallback(IntPtr user, double x, double y, int taps, PointerType type);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void PressCallback(IntPtr user, double x, double y, PointerType type);
        [DllImport(Lib)] public static extern void otk_set_tap_callback(IntPtr e, TapCallback callback, IntPtr user);
        [DllImport(Lib)] public static extern void otk_set_long_press_callback(IntPtr e, PressCallback callback, IntPtr user);
        [DllImport(Lib)] public static extern void otk_set_first_person(IntPtr e, int enabled, double heightAboveGround);
        [DllImport(Lib)] public static extern void otk_walk(IntPtr e, double forward, double right);
        [DllImport(Lib)] public static extern void otk_look(IntPtr e, double heading, double pitch);
        [DllImport(Lib)] public static extern void otk_set_3d(IntPtr e, int enabled);
        [DllImport(Lib)] public static extern int otk_load_features(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(Lib)] public static extern ulong otk_add_point(IntPtr e, double lat, double lon, double alt, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, uint argb);
        [DllImport(Lib)] public static extern ulong otk_add_line(IntPtr e, double[] latLonPairs, int count, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, uint argb, float width);
        [DllImport(Lib)] public static extern int otk_remove_feature(IntPtr e, ulong id);
        [DllImport(Lib)] public static extern void otk_clear_features(IntPtr e);
        [DllImport(Lib)] public static extern ulong otk_hit_test(IntPtr e, double x, double y, double radiusPx);
        [DllImport(Lib)] public static extern ulong otk_add_model(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string path, double lat, double lon, double heading, double scale);
        [DllImport(Lib)] public static extern ulong otk_add_tileset(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string pathOrUrl, double maxSse);
        [DllImport(Lib)] public static extern int otk_remove_tileset(IntPtr e, ulong id);
        [DllImport(Lib)] public static extern ulong otk_add_point_cloud(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string lasPath, ulong maxPoints, float pointSizePx);
        [DllImport(Lib)] public static extern void otk_set_grid(IntPtr e, int enabled);
        [DllImport(Lib)] public static extern void otk_set_labels(IntPtr e, int enabled);
        [DllImport(Lib)] public static extern int otk_labels(IntPtr e);
        [DllImport(Lib)] public static extern void otk_set_labels_source(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string urlOrPath);
        [DllImport(Lib)] public static extern int otk_labels_layer(IntPtr e);
        [DllImport(Lib)] public static extern void otk_set_buildings(IntPtr e, int enabled);
        [DllImport(Lib)] public static extern int otk_buildings(IntPtr e);
        [DllImport(Lib)] public static extern void otk_set_controls(IntPtr e, uint controls);
        [DllImport(Lib)] public static extern uint otk_controls(IntPtr e);
        [DllImport(Lib)] public static extern void otk_set_controls_padding(IntPtr e, double left, double top, double right, double bottom);
        [DllImport(Lib)] public static extern int otk_basemap_count(IntPtr e);
        [DllImport(Lib)] public static extern int otk_basemap_name(IntPtr e, int index, byte[] output, int capacity);
        [DllImport(Lib)] public static extern void otk_set_basemap(IntPtr e, int index);
        [DllImport(Lib)] public static extern int otk_basemap(IntPtr e);
        [DllImport(Lib)] public static extern int otk_download_labels(IntPtr e, double west, double south, double east, double north, int maxZoom);
        [DllImport(Lib)] public static extern ulong otk_viewshed(IntPtr e, double lat, double lon, double height, double radius);
        [DllImport(Lib)] public static extern int otk_to_mgrs(double lat, double lon, int digits, byte[] output, int capacity);
        [DllImport(Lib)] public static extern int otk_from_mgrs([MarshalAs(UnmanagedType.LPUTF8Str)] string mgrs, out double lat, out double lon);
    }

    public sealed class Engine : IDisposable
    {
        public IntPtr Handle { get; private set; }
        private readonly object gate = new object();

        private Engine(IntPtr handle)
        {
            if (handle == IntPtr.Zero) throw new InvalidOperationException("OTKGlobeEngine could not initialize the graphics device");
            Handle = handle;
            System.Net.NetworkInformation.NetworkChange.NetworkAvailabilityChanged += OnNetworkAvailability;
            System.Net.NetworkInformation.NetworkChange.NetworkAddressChanged += OnNetworkAddress;
        }

        private void OnNetworkAvailability(object sender, System.Net.NetworkInformation.NetworkAvailabilityEventArgs e)
        {
            lock (gate) if (e.IsAvailable && Handle != IntPtr.Zero) Native.otk_network_changed(Handle);
        }

        private void OnNetworkAddress(object sender, EventArgs e)
        {
            lock (gate) if (Handle != IntPtr.Zero) Native.otk_network_changed(Handle);
        }

        public static Engine CreateD3D11(IntPtr device, IntPtr context, int samples = 1) => new Engine(Native.otk_create_d3d11(device, context, samples));
        public static Engine CreateOpenGL(bool gles = false, int samples = 1) => new Engine(Native.otk_create_gl(gles ? 1 : 0, samples));

        public bool NeedsRedraw => Native.otk_needs_redraw(Handle) != 0;
        public double NextFrameDelay => Native.otk_next_frame_delay(Handle);

        public void FrameD3D11(int width, int height, IntPtr renderView, IntPtr depthStencilView, float dpi = 1f) =>
            Native.otk_frame_d3d11(Handle, width, height, renderView, IntPtr.Zero, depthStencilView, dpi);

        public void FrameOpenGL(int width, int height, uint framebuffer = 0, float dpi = 1f) =>
            Native.otk_frame_gl(Handle, width, height, framebuffer, dpi);

        public string CacheDirectory { set => Native.otk_set_cache_directory(Handle, value); }
        public bool Offline { set => Native.otk_set_offline(Handle, value ? 1 : 0); }
        public int CacheLimitMegabytes { set => Native.otk_set_cache_limit(Handle, value); }
        public long CacheBytes => Native.otk_cache_bytes(Handle);
        public bool OfflineBackdrop { set => Native.otk_set_offline_backdrop(Handle, value ? 1 : 0); }
        public void NetworkChanged() => Native.otk_network_changed(Handle);
        public long CountRegionTiles(int layer, double west, double south, double east, double north, int minZoom, int maxZoom) =>
            Native.otk_count_region_tiles(Handle, layer, west, south, east, north, minZoom, maxZoom);
        public int DownloadRegion(int layer, double west, double south, double east, double north, int minZoom, int maxZoom) =>
            Native.otk_download_region(Handle, layer, west, south, east, north, minZoom, maxZoom);
        public DownloadStatus GetDownloadStatus(int id) { Native.otk_get_download_status(Handle, id, out var s); return s; }
        public void CancelDownload(int id) => Native.otk_cancel_download(Handle, id);
        public long ExportTiles(int layer, double west, double south, double east, double north, int minZoom, int maxZoom, string path) =>
            Native.otk_export_tiles(Handle, layer, west, south, east, north, minZoom, maxZoom, path);
        public long ImportTiles(int layer, string path) => Native.otk_import_tiles(Handle, layer, path);
        public ulong AddSymbol(double lat, double lon, string sidcOrCotType, string name = "", double alt = 0) =>
            Native.otk_add_symbol(Handle, lat, lon, alt, sidcOrCotType, name);
        public int AddRangeRings(double lat, double lon, double spacingM, int count, int radials = 0, uint argb = 0) =>
            Native.otk_add_range_rings(Handle, lat, lon, spacingM, count, radials, argb);
        public int AddBullseye(double lat, double lon, double radiusM, int rings = 3, double bearingStep = 30, int reference = 1, int unit = 0, uint argb = 0) =>
            Native.otk_add_bullseye(Handle, lat, lon, radiusM, rings, bearingStep, reference, unit, argb);
        public int AddRangeBearing(double lat1, double lon1, double lat2, double lon2, int reference = 1, int unit = 0, int distanceUnit = 0, uint argb = 0) =>
            Native.otk_add_range_bearing(Handle, lat1, lon1, lat2, lon2, reference, unit, distanceUnit, argb);
        public int AddContoursInView(double interval, int majorEvery = 5, uint argb = 0) => Native.otk_add_contours_in_view(Handle, interval, majorEvery, argb);
        public void SetContours(bool enabled, double interval = 0, int unit = 0) => Native.otk_set_contours(Handle, enabled ? 1 : 0, interval, unit);
        public bool Contours => Native.otk_contours(Handle) != 0;
        public void SetTerrainSlope(bool enabled, float opacity = 0.6f, double maxSlopeDegrees = 45) => Native.otk_set_terrain_slope(Handle, enabled ? 1 : 0, opacity, maxSlopeDegrees);
        public int GridType { set => Native.otk_set_grid_type(Handle, value); }

        public static string FormatCoordinate(double lat, double lon, int format, int precision = -1)
        {
            var buffer = new byte[128];
            int n = Native.otk_format_coordinate(lat, lon, format, precision, buffer, buffer.Length);
            return System.Text.Encoding.UTF8.GetString(buffer, 0, Math.Min(n, buffer.Length - 1));
        }

        public static bool ParseCoordinate(string text, out double lat, out double lon) => Native.otk_parse_coordinate(text, out lat, out lon) != 0;
        public static double MagneticDeclination(double lat, double lon, double altitudeM = 0, double decimalYear = 0) =>
            Native.otk_magnetic_declination(lat, lon, altitudeM, decimalYear);
        public static SunMoon SunMoonAt(DateTimeOffset time, double lat, double lon)
        {
            Native.otk_sun_moon_at(time.ToUnixTimeMilliseconds() / 1000.0, lat, lon, out var r);
            return r;
        }
        public static Twilight SunMoonTimes(DateTimeOffset start, double lat, double lon, double hours = 24)
        {
            Native.otk_sun_moon_times(start.ToUnixTimeMilliseconds() / 1000.0, lat, lon, hours, out var r);
            return r;
        }
        public ulong AddTileset(string pathOrUrl, double maxSse, string headers, TilesetLighting lighting = TilesetLighting.Auto, bool cacheOffline = true) =>
            Native.otk_add_tileset_ex(Handle, pathOrUrl, maxSse, headers ?? "", (int)lighting, cacheOffline ? 1 : 0);
        public string[] Credits()
        {
            int n = Native.otk_credits(Handle, null, 0);
            var buffer = new byte[n + 1];
            Native.otk_credits(Handle, buffer, buffer.Length);
            string text = System.Text.Encoding.UTF8.GetString(buffer, 0, n);
            return text.Length == 0 ? Array.Empty<string>() : text.Split('\n');
        }
        public void SetSunLighting(bool enabled, DateTimeOffset? time = null) =>
            Native.otk_set_sun_lighting(Handle, enabled ? 1 : 0, time.HasValue ? time.Value.ToUnixTimeMilliseconds() / 1000.0 : 0);
        public void SetXrAnchor(double lat, double lon, double alt, double headingDeg, double scale = 1) =>
            Native.otk_set_xr_anchor(Handle, lat, lon, alt, headingDeg, scale);
        public void FrameXr(XrView[] views, int targetWidth, int targetHeight, float dpiScale = 1f) =>
            Native.otk_frame_xr(Handle, views, views.Length, targetWidth, targetHeight, dpiScale);
        public bool Terrain { set => Native.otk_set_terrain(Handle, value ? 1 : 0); }
        public bool Grid { set => Native.otk_set_grid(Handle, value ? 1 : 0); }
        public bool Labels
        {
            get => Native.otk_labels(Handle) != 0;
            set => Native.otk_set_labels(Handle, value ? 1 : 0);
        }
        public string LabelsSource { set => Native.otk_set_labels_source(Handle, value ?? ""); }
        public int LabelsLayer => Native.otk_labels_layer(Handle);
        public bool Buildings
        {
            get => Native.otk_buildings(Handle) != 0;
            set => Native.otk_set_buildings(Handle, value ? 1 : 0);
        }
        public uint Controls
        {
            get => Native.otk_controls(Handle);
            set => Native.otk_set_controls(Handle, value);
        }
        public int Basemap
        {
            get => Native.otk_basemap(Handle);
            set => Native.otk_set_basemap(Handle, value);
        }
        public int BasemapCount => Native.otk_basemap_count(Handle);
        public void SetControlsPadding(double left, double top, double right, double bottom) =>
            Native.otk_set_controls_padding(Handle, left, top, right, bottom);
        public string BasemapName(int index)
        {
            var buffer = new byte[256];
            int n = Native.otk_basemap_name(Handle, index, buffer, buffer.Length);
            return System.Text.Encoding.UTF8.GetString(buffer, 0, Math.Max(0, Math.Min(n, buffer.Length - 1)));
        }
        public int DownloadLabels(double west, double south, double east, double north, int maxZoom = 14) =>
            Native.otk_download_labels(Handle, west, south, east, north, maxZoom);

        public View View
        {
            get { Native.otk_get_view(Handle, out var v); return v; }
            set { var v = value; Native.otk_set_view(Handle, ref v); }
        }

        public void FlyTo(View target, double seconds = 2) => Native.otk_fly_to(Handle, ref target, seconds);
        public int AddLayer(Source type, string name, string urlOrPath, int minZoom = 0, int maxZoom = 19, float opacity = 1f) =>
            Native.otk_add_layer(Handle, type, name, urlOrPath, minZoom, maxZoom, opacity);
        public int LoadFeatures(string path) => Native.otk_load_features(Handle, path);
        public ulong AddTileset(string pathOrUrl, double maxSse = 16) => Native.otk_add_tileset(Handle, pathOrUrl, maxSse);
        public ulong HitTest(double x, double y, double radiusPx = 16) => Native.otk_hit_test(Handle, x, y, radiusPx);
        public void Pan(double x0, double y0, double x1, double y1) => Native.otk_pan(Handle, x0, y0, x1, y1);
        public void ZoomAt(double x, double y, double factor) => Native.otk_zoom_at(Handle, x, y, factor);
        public void Rotate(double degrees) => Native.otk_rotate(Handle, degrees);
        public void Tilt(double degrees) => Native.otk_tilt(Handle, degrees);
        public void RotateAt(double x, double y, double degrees) => Native.otk_rotate_at(Handle, x, y, degrees);
        public void SetHeadingLockedPan(bool enabled) => Native.otk_set_heading_locked_pan(Handle, enabled ? 1 : 0);
        public void PointerDown(int id, double x, double y, PointerType type = PointerType.Touch, MouseButton button = MouseButton.Left, Modifiers modifiers = Modifiers.None) => Native.otk_pointer_down(Handle, id, x, y, type, button, modifiers);
        public void PointerMove(int id, double x, double y, Modifiers modifiers = Modifiers.None) => Native.otk_pointer_move(Handle, id, x, y, modifiers);
        public void PointerUp(int id, double x, double y) => Native.otk_pointer_up(Handle, id, x, y);
        public void PointerCancel(int id) => Native.otk_pointer_cancel(Handle, id);
        public void PointerCancelAll() => Native.otk_pointer_cancel_all(Handle);
        public void Wheel(double x, double y, double dx, double dy, bool precise = false, Modifiers modifiers = Modifiers.None) => Native.otk_wheel(Handle, x, y, dx, dy, precise ? 1 : 0, modifiers);
        public void Magnify(double x, double y, double scale) => Native.otk_magnify(Handle, x, y, scale);
        public void RotateGesture(double x, double y, double degrees) => Native.otk_rotate_gesture(Handle, x, y, degrees);
        public void Key(MapKey key, bool down, Modifiers modifiers = Modifiers.None) => Native.otk_key(Handle, key, down ? 1 : 0, modifiers);

        Native.TapCallback tapThunk;
        Native.PressCallback pressThunk;

        public event Action<double, double, int, PointerType> Tapped
        {
            add
            {
                tapped += value;
                if (tapThunk == null)
                {
                    tapThunk = (u, x, y, taps, type) => tapped?.Invoke(x, y, taps, type);
                    Native.otk_set_tap_callback(Handle, tapThunk, IntPtr.Zero);
                }
            }
            remove { tapped -= value; }
        }

        public event Action<double, double, PointerType> LongPressed
        {
            add
            {
                longPressed += value;
                if (pressThunk == null)
                {
                    pressThunk = (u, x, y, type) => longPressed?.Invoke(x, y, type);
                    Native.otk_set_long_press_callback(Handle, pressThunk, IntPtr.Zero);
                }
            }
            remove { longPressed -= value; }
        }

        Action<double, double, int, PointerType> tapped;
        Action<double, double, PointerType> longPressed;

        public bool Pick(double x, double y, out double lat, out double lon) => Native.otk_pick(Handle, x, y, out lat, out lon) != 0;

        public static string ToMgrs(double lat, double lon, int digits = 5)
        {
            var buf = new byte[64];
            int n = Native.otk_to_mgrs(lat, lon, digits, buf, buf.Length);
            return System.Text.Encoding.UTF8.GetString(buf, 0, Math.Min(n, buf.Length - 1));
        }

        public void Dispose()
        {
            System.Net.NetworkInformation.NetworkChange.NetworkAvailabilityChanged -= OnNetworkAvailability;
            System.Net.NetworkInformation.NetworkChange.NetworkAddressChanged -= OnNetworkAddress;
            lock (gate)
            {
                if (Handle != IntPtr.Zero) Native.otk_destroy(Handle);
                Handle = IntPtr.Zero;
            }
        }
    }
}
