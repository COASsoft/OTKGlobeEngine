package com.opentakengine;

public final class OpenTakEngine {
    static {
        System.loadLibrary("opentakengine_jni");
    }

    public static final int SOURCE_URL = 0;
    public static final int SOURCE_WMS = 1;
    public static final int SOURCE_WMTS = 2;
    public static final int SOURCE_MBTILES = 3;
    public static final int SOURCE_GEOPACKAGE = 4;
    public static final int SOURCE_OSMDROID = 5;
    public static final int SOURCE_GDAL = 6;
    public static final int SOURCE_MVT = 7;

    private OpenTakEngine() {}

    static native long nativeCreate(int samples);
    static native void nativeDestroy(long h);
    static native void nativeFrame(long h, int width, int height, float dpi);
    static native boolean nativeNeedsRedraw(long h);
    static native void nativeSetCacheDirectory(long h, String dir);
    static native void nativeSetOffline(long h, boolean offline);
    static native void nativeSetMemoryBudget(long h, int megabytes);
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
    static native void nativeSetFirstPerson(long h, boolean enabled, double height);
    static native void nativeSet3D(long h, boolean enabled);
    static native int nativeLoadFeatures(long h, String path);
    static native long nativeAddPoint(long h, double lat, double lon, double alt, String name, int argb);
    static native boolean nativeRemoveFeature(long h, long id);
    static native long nativeHitTest(long h, double x, double y, double radius);
    static native long nativeAddTileset(long h, String pathOrUrl, double maxSse);
    static native void nativeSetGrid(long h, boolean enabled);
    public static native String nativeToMgrs(double lat, double lon, int digits);
}
