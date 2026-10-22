package com.otkglobeengine;

public final class OTKGlobeEngine {
    static {
        System.loadLibrary("otkglobeengine_jni");
    }

    public static final int SOURCE_URL = 0;
    public static final int SOURCE_WMS = 1;
    public static final int SOURCE_WMTS = 2;
    public static final int SOURCE_MBTILES = 3;
    public static final int SOURCE_GEOPACKAGE = 4;
    public static final int SOURCE_OSMDROID = 5;
    public static final int SOURCE_GDAL = 6;
    public static final int SOURCE_MVT = 7;

    public static final int POINTER_MOUSE = 0;
    public static final int POINTER_TOUCH = 1;
    public static final int POINTER_PEN = 2;
    public static final int BUTTON_NONE = 0;
    public static final int BUTTON_LEFT = 1;
    public static final int BUTTON_RIGHT = 2;
    public static final int BUTTON_MIDDLE = 3;
    public static final int MOD_SHIFT = 1;
    public static final int MOD_CTRL = 2;
    public static final int MOD_ALT = 4;
    public static final int MOD_SUPER = 8;
    public static final int KEY_LEFT = 0;
    public static final int KEY_RIGHT = 1;
    public static final int KEY_UP = 2;
    public static final int KEY_DOWN = 3;
    public static final int KEY_ZOOM_IN = 4;
    public static final int KEY_ZOOM_OUT = 5;
    public static final int KEY_ROTATE_LEFT = 6;
    public static final int KEY_ROTATE_RIGHT = 7;
    public static final int KEY_TILT_UP = 8;
    public static final int KEY_TILT_DOWN = 9;
    public static final int KEY_RESET_NORTH = 10;
    public static final int KEY_RESET_TILT = 11;

    private OTKGlobeEngine() {}

    static native long nativeCreate(int samples);
    static native void nativeDestroy(long h);
    static native void nativeFrame(long h, int width, int height, float dpi);
    static native boolean nativeNeedsRedraw(long h);
    static native void nativeSetCacheDirectory(long h, String dir);
    static native void nativeSetOffline(long h, boolean offline);
    static native void nativeSetMemoryBudget(long h, int megabytes);
    static native void nativeSetCacheLimit(long h, int megabytes);
    static native void nativeNetworkChanged(long h);
    static native int nativeDownloadRegion(long h, int layer, double west, double south, double east, double north, int minZoom, int maxZoom);
    static native long[] nativeDownloadStatus(long h, int id);
    static native void nativeCancelDownload(long h, int id);
    static native long nativeExportTiles(long h, int layer, double west, double south, double east, double north, int minZoom, int maxZoom, String path);
    static native long nativeImportTiles(long h, int layer, String path);
    static native long nativeAddSymbol(long h, double lat, double lon, double alt, String sidcOrCotType, String name);
    static native int nativeAddRangeRings(long h, double lat, double lon, double spacingM, int count, int radials, int argb);
    static native int nativeAddBullseye(long h, double lat, double lon, double radiusM, int rings, double bearingStep, int reference, int unit, int argb);
    static native int nativeAddRangeBearing(long h, double lat1, double lon1, double lat2, double lon2, int reference, int unit, int distanceUnit, int argb);
    static native int nativeAddContoursInView(long h, double interval, int majorEvery, int argb);
    static native void nativeSetTerrainSlope(long h, boolean enabled, float opacity, double maxSlopeDegrees);
    static native void nativeSetGridType(long h, int type);
    public static native String nativeFormatCoordinate(double lat, double lon, int format, int precision);
    public static native double[] nativeParseCoordinate(String text);
    public static native double nativeMagneticDeclination(double lat, double lon, double altitudeM, double decimalYear);
    public static native double[] nativeSunMoon(double unixSeconds, double lat, double lon);
    public static native double[] nativeSunMoonTimes(double startUnixSeconds, double lat, double lon, double hours);
    static native long nativeAddTilesetEx(long h, String pathOrUrl, double maxSse, String headers, int lighting, boolean cacheOffline);
    static native String nativeCredits(long h);
    static native void nativeSetSunLighting(long h, boolean enabled, double unixSeconds);
    static native void nativeSetXrAnchor(long h, double lat, double lon, double alt, double headingDeg, double scale);
    static native void nativeFrameXr(long h, double[] poses, int[] rects, int[] framebuffers, int targetWidth, int targetHeight, float dpiScale);

    public static final int LIGHTING_AUTO = 0;
    public static final int LIGHTING_LIT = 1;
    public static final int LIGHTING_UNLIT = 2;

    public static final int NORTH_TRUE = 0;
    public static final int NORTH_MAGNETIC = 1;
    public static final int NORTH_GRID = 2;
    public static final int ANGLE_DEGREES = 0;
    public static final int ANGLE_MILS = 1;
    public static final int COORD_DECIMAL = 0;
    public static final int COORD_DM = 1;
    public static final int COORD_DMS = 2;
    public static final int COORD_MGRS = 3;
    public static final int COORD_UTM = 4;
    public static final int COORD_GARS = 5;
    public static final int GRID_MGRS = 0;
    public static final int GRID_LATLON = 1;
    public static final int GRID_GARS = 2;
    static native void nativeClearLayers(long h);
    static native int nativeAddLayer(long h, int type, String name, String urlOrPath, int minZoom, int maxZoom, float opacity);
    static native int nativeLoadMobac(long h, String xmlOrPath);
    static native void nativeUseDefaultLayers(long h);
    static native void nativeSetTerrain(long h, boolean enabled);
    static native void nativeAddElevationDirectory(long h, String dir);
    static native double nativeElevation(long h, double lat, double lon);
    static native void nativeSetView(long h, double lat, double lon, double gsd, double azimuth, double tilt);
    static native void nativeFlyTo(long h, double lat, double lon, double gsd, double azimuth, double tilt, double seconds);
    static native void nativeGetView(long h, double[] out);
    static native boolean nativePick(long h, double x, double y, double[] out);
    static native void nativePan(long h, double x0, double y0, double x1, double y1);
    static native void nativeZoomAt(long h, double x, double y, double factor);
    static native void nativeRotate(long h, double degrees);
    static native void nativeTilt(long h, double degrees);
    static native void nativePointerDown(long h, int id, double x, double y, int type, int button, int mods);
    static native void nativePointerMove(long h, int id, double x, double y, int mods);
    static native void nativePointerUp(long h, int id, double x, double y);
    static native void nativePointerCancelAll(long h);
    static native void nativeWheel(long h, double x, double y, double dx, double dy, boolean precise, int mods);
    static native void nativeKey(long h, int key, boolean down, int mods);
    static native void nativeSetHeadingLockedPan(long h, boolean enabled);
    static native long nativeSetTapListener(long h, Object listener, long previous);
    static native void nativeSetFirstPerson(long h, boolean enabled, double height);
    static native void nativeSet3D(long h, boolean enabled);
    static native int nativeLoadFeatures(long h, String path);
    static native long nativeAddPoint(long h, double lat, double lon, double alt, String name, int argb);
    static native boolean nativeRemoveFeature(long h, long id);
    static native long nativeHitTest(long h, double x, double y, double radius);
    static native long nativeAddTileset(long h, String pathOrUrl, double maxSse);
    static native void nativeSetGrid(long h, boolean enabled);
    static native void nativeSetLabels(long h, boolean enabled);
    static native void nativeSetContours(long h, boolean enabled, double interval, int unit);
    static native void nativeSetLabelsSource(long h, String urlOrPath);
    static native int nativeDownloadLabels(long h, double west, double south, double east, double north, int maxZoom);
    public static native String nativeToMgrs(double lat, double lon, int digits);
}
