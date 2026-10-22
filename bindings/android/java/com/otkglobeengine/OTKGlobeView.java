package com.otkglobeengine;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;

import java.util.ArrayList;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class OTKGlobeView extends GLSurfaceView implements GLSurfaceView.Renderer {
    public interface Ready {
        void onReady(OTKGlobeView view);
    }

    private long engine;
    private int width = 1;
    private int height = 1;
    private final float density;
    private boolean autoPadding = true;
    private final ArrayList<Runnable> pending = new ArrayList<>();
    private Ready ready;
    private ConnectivityManager.NetworkCallback networkCallback;

    public interface Result {
        void onResult(long value);
    }

    public OTKGlobeView(Context context) {
        this(context, null);
    }

    public OTKGlobeView(Context context, AttributeSet attrs) {
        super(context, attrs);
        density = context.getResources().getDisplayMetrics().density;
        setEGLContextClientVersion(3);
        setEGLConfigChooser(8, 8, 8, 8, 24, 8);
        setPreserveEGLContextOnPause(true);
        setRenderer(this);
        setRenderMode(RENDERMODE_WHEN_DIRTY);
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    public void setOnReady(Ready r) {
        ready = r;
    }

    public void run(final Runnable r) {
        queueEvent(new Runnable() {
            @Override
            public void run() {
                if (engine == 0) {
                    pending.add(r);
                } else {
                    r.run();
                }
                requestRender();
            }
        });
    }

    public long handle() {
        return engine;
    }

    public void setView(final double lat, final double lon, final double gsd, final double azimuth, final double tilt) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeSetView(engine, lat, lon, gsd, azimuth, tilt);
            }
        });
    }

    public void flyTo(final double lat, final double lon, final double gsd, final double azimuth, final double tilt, final double seconds) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeFlyTo(engine, lat, lon, gsd, azimuth, tilt, seconds);
            }
        });
    }

    public void loadFeatures(final String path) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeLoadFeatures(engine, path);
            }
        });
    }

    public void addLayer(final int type, final String name, final String urlOrPath, final int minZoom, final int maxZoom, final float opacity) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeAddLayer(engine, type, name, urlOrPath, minZoom, maxZoom, opacity);
            }
        });
    }

    @Override
    protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        ConnectivityManager cm = (ConnectivityManager) getContext().getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null || networkCallback != null) return;
        networkCallback = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onAvailable(Network network) {
                run(new Runnable() {
                    @Override
                    public void run() {
                        OTKGlobeEngine.nativeNetworkChanged(engine);
                    }
                });
            }
        };
        try {
            cm.registerDefaultNetworkCallback(networkCallback);
        } catch (RuntimeException e) {
            networkCallback = null;
        }
    }

    @Override
    protected void onDetachedFromWindow() {
        ConnectivityManager cm = (ConnectivityManager) getContext().getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm != null && networkCallback != null) {
            try {
                cm.unregisterNetworkCallback(networkCallback);
            } catch (RuntimeException ignored) {
            }
        }
        networkCallback = null;
        super.onDetachedFromWindow();
    }

    public void setOffline(final boolean offline) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeSetOffline(engine, offline);
            }
        });
    }

    public void setCacheLimit(final int megabytes) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeSetCacheLimit(engine, megabytes);
            }
        });
    }

    public void downloadRegion(final int layer, final double west, final double south, final double east, final double north,
                               final int minZoom, final int maxZoom, final Result result) {
        run(new Runnable() {
            @Override
            public void run() {
                final long id = OTKGlobeEngine.nativeDownloadRegion(engine, layer, west, south, east, north, minZoom, maxZoom);
                if (result != null) post(new Runnable() {
                    @Override
                    public void run() {
                        result.onResult(id);
                    }
                });
            }
        });
    }

    public long[] downloadStatus(int id) {
        return engine == 0 ? new long[6] : OTKGlobeEngine.nativeDownloadStatus(engine, id);
    }

    public void cancelDownload(final int id) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeCancelDownload(engine, id);
            }
        });
    }

    public void exportTiles(final int layer, final double west, final double south, final double east, final double north,
                            final int minZoom, final int maxZoom, final String path, final Result result) {
        run(new Runnable() {
            @Override
            public void run() {
                final long n = OTKGlobeEngine.nativeExportTiles(engine, layer, west, south, east, north, minZoom, maxZoom, path);
                if (result != null) post(new Runnable() {
                    @Override
                    public void run() {
                        result.onResult(n);
                    }
                });
            }
        });
    }

    public void addSymbol(final double lat, final double lon, final String sidcOrCotType, final String name) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeAddSymbol(engine, lat, lon, 0, sidcOrCotType, name);
                requestRender();
            }
        });
    }

    public void addBullseye(final double lat, final double lon, final double radiusM, final int rings, final double bearingStep, final int northReference,
                            final int angleUnit) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeAddBullseye(engine, lat, lon, radiusM, rings, bearingStep, northReference, angleUnit, 0);
                requestRender();
            }
        });
    }

    public void addRangeRings(final double lat, final double lon, final double spacingM, final int count, final int radials) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeAddRangeRings(engine, lat, lon, spacingM, count, radials, 0);
                requestRender();
            }
        });
    }

    public void addRangeBearing(final double lat1, final double lon1, final double lat2, final double lon2, final int northReference, final int angleUnit) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeAddRangeBearing(engine, lat1, lon1, lat2, lon2, northReference, angleUnit, 0, 0);
                requestRender();
            }
        });
    }

    public void addContoursInView(final double interval, final int majorEvery) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeAddContoursInView(engine, interval, majorEvery, 0);
                requestRender();
            }
        });
    }

    public void setTerrainSlope(final boolean enabled) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeSetTerrainSlope(engine, enabled, 0.6f, 45.0);
                requestRender();
            }
        });
    }

    public void setGridType(final int type) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeSetGridType(engine, type);
                requestRender();
            }
        });
    }

    public void setLabels(final boolean enabled) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeSetLabels(engine, enabled);
                requestRender();
            }
        });
    }

    public void setContours(final boolean enabled, final double interval, final int unit) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeSetContours(engine, enabled, interval, unit);
                requestRender();
            }
        });
    }

    public void downloadLabels(final double west, final double south, final double east, final double north, final int maxZoom,
                               final Result result) {
        run(new Runnable() {
            @Override
            public void run() {
                final long id = OTKGlobeEngine.nativeDownloadLabels(engine, west, south, east, north, maxZoom);
                requestRender();
                if (result != null) post(new Runnable() {
                    @Override
                    public void run() {
                        result.onResult(id);
                    }
                });
            }
        });
    }

    public static final int CONTROL_ZOOM = 1;
    public static final int CONTROL_COMPASS = 2;
    public static final int CONTROL_TILT = 4;
    public static final int CONTROL_LAYERS = 8;
    public static final int CONTROL_SCALE = 16;
    public static final int CONTROL_ATTRIBUTION = 32;
    public static final int CONTROL_ALL = 63;

    public void setBuildings(final boolean enabled) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeSetBuildings(engine, enabled);
                requestRender();
            }
        });
    }

    public void setControls(final int controls) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeSetControls(engine, controls);
                requestRender();
            }
        });
    }

    public void setControlsPadding(final float left, final float top, final float right, final float bottom) {
        autoPadding = false;
        applyControlsPadding(left, top, right, bottom);
    }

    private void applyControlsPadding(final float left, final float top, final float right, final float bottom) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeSetControlsPadding(engine, left, top, right, bottom);
                requestRender();
            }
        });
    }

    @Override
    public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        if (autoPadding) {
            int[] at = new int[2];
            getLocationInWindow(at);
            View root = getRootView();
            float left = Math.max(0, insets.getSystemWindowInsetLeft() - at[0]);
            float top = Math.max(0, insets.getSystemWindowInsetTop() - at[1]);
            float right = Math.max(0, insets.getSystemWindowInsetRight() - (root.getWidth() - at[0] - getWidth()));
            float bottom = Math.max(0, insets.getSystemWindowInsetBottom() - (root.getHeight() - at[1] - getHeight()));
            applyControlsPadding(left / density, top / density, right / density, bottom / density);
        }
        return super.onApplyWindowInsets(insets);
    }

    public void setBasemap(final int index) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeSetBasemap(engine, index);
                requestRender();
            }
        });
    }

    public void setLabelsSource(final String urlOrPath) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeSetLabelsSource(engine, urlOrPath == null ? "" : urlOrPath);
                requestRender();
            }
        });
    }

    public void importTiles(final int layer, final String path, final Result result) {
        run(new Runnable() {
            @Override
            public void run() {
                final long n = OTKGlobeEngine.nativeImportTiles(engine, layer, path);
                requestRender();
                if (result != null) post(new Runnable() {
                    @Override
                    public void run() {
                        result.onResult(n);
                    }
                });
            }
        });
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        if (engine != 0) return;
        engine = OTKGlobeEngine.nativeCreate(1);
        java.io.File tiles = new java.io.File(getContext().getNoBackupFilesDir(), "otkglobeengine");
        if (!tiles.isDirectory()) tiles.mkdirs();
        OTKGlobeEngine.nativeSetCacheDirectory(engine, tiles.getAbsolutePath());
        for (Runnable r : pending) r.run();
        pending.clear();
        if (ready != null) {
            post(new Runnable() {
                @Override
                public void run() {
                    ready.onReady(OTKGlobeView.this);
                }
            });
        }
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int w, int h) {
        width = w;
        height = h;
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        if (engine == 0) return;
        OTKGlobeEngine.nativeFrame(engine, width, height, density);
        if (OTKGlobeEngine.nativeNeedsRedraw(engine)) requestRender();
    }

    public interface MapListener {
        void onTap(double x, double y, int taps, int pointerType);

        void onLongPress(double x, double y, int pointerType);
    }

    private final class Bridge {
        @SuppressWarnings("unused")
        public void onTap(final double x, final double y, final int taps, final int type) {
            post(new Runnable() {
                @Override
                public void run() {
                    MapListener l = mapListener;
                    if (l != null) l.onTap(x, y, taps, type);
                }
            });
        }

        @SuppressWarnings("unused")
        public void onLongPress(final double x, final double y, final int type) {
            post(new Runnable() {
                @Override
                public void run() {
                    MapListener l = mapListener;
                    if (l != null) l.onLongPress(x, y, type);
                }
            });
        }
    }

    private volatile MapListener mapListener;
    private long listenerHandle;

    public void setMapListener(final MapListener listener) {
        mapListener = listener;
        run(new Runnable() {
            @Override
            public void run() {
                listenerHandle = OTKGlobeEngine.nativeSetTapListener(engine, listener == null ? null : new Bridge(), listenerHandle);
            }
        });
    }

    public void setHeadingLockedPan(final boolean enabled) {
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeSetHeadingLockedPan(engine, enabled);
            }
        });
    }

    private static int modifiers(int meta) {
        int m = 0;
        if ((meta & KeyEvent.META_SHIFT_ON) != 0) m |= OTKGlobeEngine.MOD_SHIFT;
        if ((meta & KeyEvent.META_CTRL_ON) != 0) m |= OTKGlobeEngine.MOD_CTRL;
        if ((meta & KeyEvent.META_ALT_ON) != 0) m |= OTKGlobeEngine.MOD_ALT;
        if ((meta & KeyEvent.META_META_ON) != 0) m |= OTKGlobeEngine.MOD_SUPER;
        return m;
    }

    private static int pointerType(MotionEvent e, int index) {
        int t = e.getToolType(index);
        if (t == MotionEvent.TOOL_TYPE_MOUSE) return OTKGlobeEngine.POINTER_MOUSE;
        if (t == MotionEvent.TOOL_TYPE_STYLUS || t == MotionEvent.TOOL_TYPE_ERASER) return OTKGlobeEngine.POINTER_PEN;
        return OTKGlobeEngine.POINTER_TOUCH;
    }

    private static int button(MotionEvent e) {
        int b = e.getButtonState();
        if ((b & MotionEvent.BUTTON_SECONDARY) != 0) return OTKGlobeEngine.BUTTON_RIGHT;
        if ((b & MotionEvent.BUTTON_TERTIARY) != 0) return OTKGlobeEngine.BUTTON_MIDDLE;
        return OTKGlobeEngine.BUTTON_LEFT;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        final int action = e.getActionMasked();
        final int index = e.getActionIndex();
        final int mods = modifiers(e.getMetaState());
        switch (action) {
        case MotionEvent.ACTION_DOWN:
        case MotionEvent.ACTION_POINTER_DOWN: {
            final int id = e.getPointerId(index);
            final float x = e.getX(index), y = e.getY(index);
            final int type = pointerType(e, index);
            final int btn = type == OTKGlobeEngine.POINTER_MOUSE ? button(e) : OTKGlobeEngine.BUTTON_LEFT;
            run(new Runnable() {
                @Override
                public void run() {
                    OTKGlobeEngine.nativePointerDown(engine, id, x, y, type, btn, mods);
                }
            });
            break;
        }
        case MotionEvent.ACTION_MOVE: {
            final int n = e.getPointerCount();
            final int[] ids = new int[n];
            final float[] xs = new float[n];
            final float[] ys = new float[n];
            for (int i = 0; i < n; ++i) {
                ids[i] = e.getPointerId(i);
                xs[i] = e.getX(i);
                ys[i] = e.getY(i);
            }
            run(new Runnable() {
                @Override
                public void run() {
                    for (int i = 0; i < n; ++i) OTKGlobeEngine.nativePointerMove(engine, ids[i], xs[i], ys[i], mods);
                }
            });
            break;
        }
        case MotionEvent.ACTION_UP:
        case MotionEvent.ACTION_POINTER_UP: {
            final int id = e.getPointerId(index);
            final float x = e.getX(index), y = e.getY(index);
            run(new Runnable() {
                @Override
                public void run() {
                    OTKGlobeEngine.nativePointerUp(engine, id, x, y);
                }
            });
            break;
        }
        case MotionEvent.ACTION_CANCEL:
            run(new Runnable() {
                @Override
                public void run() {
                    OTKGlobeEngine.nativePointerCancelAll(engine);
                }
            });
            break;
        default:
            break;
        }
        return true;
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent e) {
        if (e.getActionMasked() == MotionEvent.ACTION_SCROLL) {
            final float x = e.getX(), y = e.getY();
            final float h = e.getAxisValue(MotionEvent.AXIS_HSCROLL), v = e.getAxisValue(MotionEvent.AXIS_VSCROLL);
            final int mods = modifiers(e.getMetaState());
            run(new Runnable() {
                @Override
                public void run() {
                    OTKGlobeEngine.nativeWheel(engine, x, y, h, v, false, mods);
                }
            });
            return true;
        }
        return super.onGenericMotionEvent(e);
    }

    private static int mapKey(int code) {
        switch (code) {
        case KeyEvent.KEYCODE_DPAD_LEFT:
        case KeyEvent.KEYCODE_A:
            return OTKGlobeEngine.KEY_LEFT;
        case KeyEvent.KEYCODE_DPAD_RIGHT:
        case KeyEvent.KEYCODE_D:
            return OTKGlobeEngine.KEY_RIGHT;
        case KeyEvent.KEYCODE_DPAD_UP:
        case KeyEvent.KEYCODE_W:
            return OTKGlobeEngine.KEY_UP;
        case KeyEvent.KEYCODE_DPAD_DOWN:
        case KeyEvent.KEYCODE_S:
            return OTKGlobeEngine.KEY_DOWN;
        case KeyEvent.KEYCODE_PLUS:
        case KeyEvent.KEYCODE_EQUALS:
        case KeyEvent.KEYCODE_NUMPAD_ADD:
        case KeyEvent.KEYCODE_ZOOM_IN:
            return OTKGlobeEngine.KEY_ZOOM_IN;
        case KeyEvent.KEYCODE_MINUS:
        case KeyEvent.KEYCODE_NUMPAD_SUBTRACT:
        case KeyEvent.KEYCODE_ZOOM_OUT:
            return OTKGlobeEngine.KEY_ZOOM_OUT;
        case KeyEvent.KEYCODE_Q:
            return OTKGlobeEngine.KEY_ROTATE_LEFT;
        case KeyEvent.KEYCODE_E:
            return OTKGlobeEngine.KEY_ROTATE_RIGHT;
        case KeyEvent.KEYCODE_PAGE_UP:
            return OTKGlobeEngine.KEY_TILT_UP;
        case KeyEvent.KEYCODE_PAGE_DOWN:
            return OTKGlobeEngine.KEY_TILT_DOWN;
        case KeyEvent.KEYCODE_N:
            return OTKGlobeEngine.KEY_RESET_NORTH;
        case KeyEvent.KEYCODE_T:
            return OTKGlobeEngine.KEY_RESET_TILT;
        default:
            return -1;
        }
    }

    private boolean key(final int code, final KeyEvent e, final boolean down) {
        final int k = mapKey(code);
        if (k < 0) return false;
        if (down && e.getRepeatCount() > 0) return true;
        final int mods = modifiers(e.getMetaState());
        run(new Runnable() {
            @Override
            public void run() {
                OTKGlobeEngine.nativeKey(engine, k, down, mods);
            }
        });
        return true;
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent e) {
        return key(keyCode, e, true) || super.onKeyDown(keyCode, e);
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent e) {
        return key(keyCode, e, false) || super.onKeyUp(keyCode, e);
    }
}
