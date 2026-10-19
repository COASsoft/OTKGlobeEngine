using System;
using System.Runtime.InteropServices;

namespace OpenTakEngine
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
        const string Lib = "opentakengine_c";

        [DllImport(Lib)] public static extern IntPtr ote_create_gl(int gles, int sampleCount);
        [DllImport(Lib)] public static extern IntPtr ote_create_d3d11(IntPtr device, IntPtr context, int sampleCount);
        [DllImport(Lib)] public static extern IntPtr ote_create_metal(IntPtr device, int sampleCount);
        [DllImport(Lib)] public static extern void ote_destroy(IntPtr e);
        [DllImport(Lib)] public static extern void ote_frame_gl(IntPtr e, int width, int height, uint framebuffer, float dpi);
        [DllImport(Lib)] public static extern void ote_frame_d3d11(IntPtr e, int width, int height, IntPtr renderView, IntPtr resolveView, IntPtr depthStencilView, float dpi);
        [DllImport(Lib)] public static extern void ote_frame_metal(IntPtr e, int width, int height, IntPtr drawable, IntPtr depth, IntPtr msaa, float dpi);
        [DllImport(Lib)] public static extern int ote_needs_redraw(IntPtr e);
        [DllImport(Lib)] public static extern void ote_set_cache_directory(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string dir);
        [DllImport(Lib)] public static extern void ote_set_offline(IntPtr e, int offline);
        [DllImport(Lib)] public static extern void ote_set_memory_budget(IntPtr e, int megabytes);
        [DllImport(Lib)] public static extern void ote_clear_layers(IntPtr e);
        [DllImport(Lib)] public static extern int ote_add_layer(IntPtr e, Source type, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, [MarshalAs(UnmanagedType.LPUTF8Str)] string urlOrPath, int minZoom, int maxZoom, float opacity);
        [DllImport(Lib)] public static extern int ote_add_wms_layer(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, [MarshalAs(UnmanagedType.LPUTF8Str)] string url, [MarshalAs(UnmanagedType.LPUTF8Str)] string layers, int srid, [MarshalAs(UnmanagedType.LPUTF8Str)] string version, float opacity);
        [DllImport(Lib)] public static extern int ote_load_mobac(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string xmlOrPath);
        [DllImport(Lib)] public static extern void ote_use_default_layers(IntPtr e);
        [DllImport(Lib)] public static extern void ote_set_terrain(IntPtr e, int enabled);
        [DllImport(Lib)] public static extern void ote_set_terrain_exaggeration(IntPtr e, double factor);
        [DllImport(Lib)] public static extern void ote_set_depth_perception(IntPtr e, int enabled);
        [DllImport(Lib)] public static extern void ote_set_terrain_transparency(IntPtr e, float alpha);
        [DllImport(Lib)] public static extern void ote_add_elevation_directory(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(Lib)] public static extern double ote_elevation(IntPtr e, double lat, double lon);
        [DllImport(Lib)] public static extern void ote_set_view(IntPtr e, ref View v);
        [DllImport(Lib)] public static extern void ote_get_view(IntPtr e, out View v);
        [DllImport(Lib)] public static extern void ote_fly_to(IntPtr e, ref View v, double seconds);
        [DllImport(Lib)] public static extern int ote_pick(IntPtr e, double x, double y, out double lat, out double lon);
        [DllImport(Lib)] public static extern void ote_pan(IntPtr e, double x0, double y0, double x1, double y1);
        [DllImport(Lib)] public static extern void ote_zoom_at(IntPtr e, double x, double y, double factor);
        [DllImport(Lib)] public static extern void ote_rotate(IntPtr e, double degrees);
        [DllImport(Lib)] public static extern void ote_tilt(IntPtr e, double degrees);
        [DllImport(Lib)] public static extern void ote_rotate_at(IntPtr e, double x, double y, double degrees);
        [DllImport(Lib)] public static extern void ote_set_heading_locked_pan(IntPtr e, int enabled);
        [DllImport(Lib)] public static extern void ote_pointer_down(IntPtr e, int id, double x, double y, PointerType type, MouseButton button, Modifiers modifiers);
        [DllImport(Lib)] public static extern void ote_pointer_move(IntPtr e, int id, double x, double y, Modifiers modifiers);
        [DllImport(Lib)] public static extern void ote_pointer_up(IntPtr e, int id, double x, double y);
        [DllImport(Lib)] public static extern void ote_pointer_cancel(IntPtr e, int id);
        [DllImport(Lib)] public static extern void ote_pointer_cancel_all(IntPtr e);
        [DllImport(Lib)] public static extern void ote_wheel(IntPtr e, double x, double y, double dx, double dy, int precise, Modifiers modifiers);
        [DllImport(Lib)] public static extern void ote_magnify(IntPtr e, double x, double y, double scale);
        [DllImport(Lib)] public static extern void ote_rotate_gesture(IntPtr e, double x, double y, double degrees);
        [DllImport(Lib)] public static extern void ote_key(IntPtr e, MapKey key, int down, Modifiers modifiers);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void TapCallback(IntPtr user, double x, double y, int taps, PointerType type);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)] public delegate void PressCallback(IntPtr user, double x, double y, PointerType type);
        [DllImport(Lib)] public static extern void ote_set_tap_callback(IntPtr e, TapCallback callback, IntPtr user);
        [DllImport(Lib)] public static extern void ote_set_long_press_callback(IntPtr e, PressCallback callback, IntPtr user);
        [DllImport(Lib)] public static extern void ote_set_first_person(IntPtr e, int enabled, double heightAboveGround);
        [DllImport(Lib)] public static extern void ote_walk(IntPtr e, double forward, double right);
        [DllImport(Lib)] public static extern void ote_look(IntPtr e, double heading, double pitch);
        [DllImport(Lib)] public static extern void ote_set_3d(IntPtr e, int enabled);
        [DllImport(Lib)] public static extern int ote_load_features(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);
        [DllImport(Lib)] public static extern ulong ote_add_point(IntPtr e, double lat, double lon, double alt, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, uint argb);
        [DllImport(Lib)] public static extern ulong ote_add_line(IntPtr e, double[] latLonPairs, int count, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, uint argb, float width);
        [DllImport(Lib)] public static extern int ote_remove_feature(IntPtr e, ulong id);
        [DllImport(Lib)] public static extern void ote_clear_features(IntPtr e);
        [DllImport(Lib)] public static extern ulong ote_hit_test(IntPtr e, double x, double y, double radiusPx);
        [DllImport(Lib)] public static extern ulong ote_add_model(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string path, double lat, double lon, double heading, double scale);
        [DllImport(Lib)] public static extern ulong ote_add_tileset(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string pathOrUrl, double maxSse);
        [DllImport(Lib)] public static extern int ote_remove_tileset(IntPtr e, ulong id);
        [DllImport(Lib)] public static extern ulong ote_add_point_cloud(IntPtr e, [MarshalAs(UnmanagedType.LPUTF8Str)] string lasPath, ulong maxPoints, float pointSizePx);
        [DllImport(Lib)] public static extern void ote_set_grid(IntPtr e, int enabled);
        [DllImport(Lib)] public static extern ulong ote_viewshed(IntPtr e, double lat, double lon, double height, double radius);
        [DllImport(Lib)] public static extern int ote_to_mgrs(double lat, double lon, int digits, byte[] output, int capacity);
        [DllImport(Lib)] public static extern int ote_from_mgrs([MarshalAs(UnmanagedType.LPUTF8Str)] string mgrs, out double lat, out double lon);
    }

    public sealed class Engine : IDisposable
    {
        public IntPtr Handle { get; private set; }

        private Engine(IntPtr handle)
        {
            if (handle == IntPtr.Zero) throw new InvalidOperationException("OpenTAKEngine could not initialize the graphics device");
            Handle = handle;
        }

        public static Engine CreateD3D11(IntPtr device, IntPtr context, int samples = 1) => new Engine(Native.ote_create_d3d11(device, context, samples));
        public static Engine CreateOpenGL(bool gles = false, int samples = 1) => new Engine(Native.ote_create_gl(gles ? 1 : 0, samples));

        public bool NeedsRedraw => Native.ote_needs_redraw(Handle) != 0;

        public void FrameD3D11(int width, int height, IntPtr renderView, IntPtr depthStencilView, float dpi = 1f) =>
            Native.ote_frame_d3d11(Handle, width, height, renderView, IntPtr.Zero, depthStencilView, dpi);

        public void FrameOpenGL(int width, int height, uint framebuffer = 0, float dpi = 1f) =>
            Native.ote_frame_gl(Handle, width, height, framebuffer, dpi);

        public string CacheDirectory { set => Native.ote_set_cache_directory(Handle, value); }
        public bool Offline { set => Native.ote_set_offline(Handle, value ? 1 : 0); }
        public bool Terrain { set => Native.ote_set_terrain(Handle, value ? 1 : 0); }
        public bool Grid { set => Native.ote_set_grid(Handle, value ? 1 : 0); }

        public View View
        {
            get { Native.ote_get_view(Handle, out var v); return v; }
            set { var v = value; Native.ote_set_view(Handle, ref v); }
        }

        public void FlyTo(View target, double seconds = 2) => Native.ote_fly_to(Handle, ref target, seconds);
        public int AddLayer(Source type, string name, string urlOrPath, int minZoom = 0, int maxZoom = 19, float opacity = 1f) =>
            Native.ote_add_layer(Handle, type, name, urlOrPath, minZoom, maxZoom, opacity);
        public int LoadFeatures(string path) => Native.ote_load_features(Handle, path);
        public ulong AddTileset(string pathOrUrl, double maxSse = 16) => Native.ote_add_tileset(Handle, pathOrUrl, maxSse);
        public ulong HitTest(double x, double y, double radiusPx = 16) => Native.ote_hit_test(Handle, x, y, radiusPx);
        public void Pan(double x0, double y0, double x1, double y1) => Native.ote_pan(Handle, x0, y0, x1, y1);
        public void ZoomAt(double x, double y, double factor) => Native.ote_zoom_at(Handle, x, y, factor);
        public void Rotate(double degrees) => Native.ote_rotate(Handle, degrees);
        public void Tilt(double degrees) => Native.ote_tilt(Handle, degrees);
        public void RotateAt(double x, double y, double degrees) => Native.ote_rotate_at(Handle, x, y, degrees);
        public void SetHeadingLockedPan(bool enabled) => Native.ote_set_heading_locked_pan(Handle, enabled ? 1 : 0);
        public void PointerDown(int id, double x, double y, PointerType type = PointerType.Touch, MouseButton button = MouseButton.Left, Modifiers modifiers = Modifiers.None) => Native.ote_pointer_down(Handle, id, x, y, type, button, modifiers);
        public void PointerMove(int id, double x, double y, Modifiers modifiers = Modifiers.None) => Native.ote_pointer_move(Handle, id, x, y, modifiers);
        public void PointerUp(int id, double x, double y) => Native.ote_pointer_up(Handle, id, x, y);
        public void PointerCancel(int id) => Native.ote_pointer_cancel(Handle, id);
        public void PointerCancelAll() => Native.ote_pointer_cancel_all(Handle);
        public void Wheel(double x, double y, double dx, double dy, bool precise = false, Modifiers modifiers = Modifiers.None) => Native.ote_wheel(Handle, x, y, dx, dy, precise ? 1 : 0, modifiers);
        public void Magnify(double x, double y, double scale) => Native.ote_magnify(Handle, x, y, scale);
        public void RotateGesture(double x, double y, double degrees) => Native.ote_rotate_gesture(Handle, x, y, degrees);
        public void Key(MapKey key, bool down, Modifiers modifiers = Modifiers.None) => Native.ote_key(Handle, key, down ? 1 : 0, modifiers);

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
                    Native.ote_set_tap_callback(Handle, tapThunk, IntPtr.Zero);
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
                    Native.ote_set_long_press_callback(Handle, pressThunk, IntPtr.Zero);
                }
            }
            remove { longPressed -= value; }
        }

        Action<double, double, int, PointerType> tapped;
        Action<double, double, PointerType> longPressed;

        public bool Pick(double x, double y, out double lat, out double lon) => Native.ote_pick(Handle, x, y, out lat, out lon) != 0;

        public static string ToMgrs(double lat, double lon, int digits = 5)
        {
            var buf = new byte[64];
            int n = Native.ote_to_mgrs(lat, lon, digits, buf, buf.Length);
            return System.Text.Encoding.UTF8.GetString(buf, 0, Math.Min(n, buf.Length - 1));
        }

        public void Dispose()
        {
            if (Handle != IntPtr.Zero) Native.ote_destroy(Handle);
            Handle = IntPtr.Zero;
        }
    }
}
