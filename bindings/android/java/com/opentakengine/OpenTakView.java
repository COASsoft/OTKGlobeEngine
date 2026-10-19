package com.opentakengine;

import android.content.Context;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.view.KeyEvent;
import android.view.MotionEvent;

import java.util.ArrayList;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class OpenTakView extends GLSurfaceView implements GLSurfaceView.Renderer {
    public interface Ready {
        void onReady(OpenTakView view);
    }

    private long engine;
    private int width = 1;
    private int height = 1;
    private final float density;
    private final ArrayList<Runnable> pending = new ArrayList<>();
    private Ready ready;

    public OpenTakView(Context context) {
        this(context, null);
    }

    public OpenTakView(Context context, AttributeSet attrs) {
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
                OpenTakEngine.nativeSetView(engine, lat, lon, gsd, azimuth, tilt);
            }
        });
    }

    public void flyTo(final double lat, final double lon, final double gsd, final double azimuth, final double tilt, final double seconds) {
        run(new Runnable() {
            @Override
            public void run() {
                OpenTakEngine.nativeFlyTo(engine, lat, lon, gsd, azimuth, tilt, seconds);
            }
        });
    }

    public void loadFeatures(final String path) {
        run(new Runnable() {
            @Override
            public void run() {
                OpenTakEngine.nativeLoadFeatures(engine, path);
            }
        });
    }

    public void addLayer(final int type, final String name, final String urlOrPath, final int minZoom, final int maxZoom, final float opacity) {
        run(new Runnable() {
            @Override
            public void run() {
                OpenTakEngine.nativeAddLayer(engine, type, name, urlOrPath, minZoom, maxZoom, opacity);
            }
        });
    }

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        if (engine != 0) return;
        engine = OpenTakEngine.nativeCreate(1);
        OpenTakEngine.nativeSetCacheDirectory(engine, getContext().getCacheDir().getAbsolutePath());
        for (Runnable r : pending) r.run();
        pending.clear();
        if (ready != null) {
            post(new Runnable() {
                @Override
                public void run() {
                    ready.onReady(OpenTakView.this);
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
        OpenTakEngine.nativeFrame(engine, width, height, density);
        if (OpenTakEngine.nativeNeedsRedraw(engine)) requestRender();
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
                listenerHandle = OpenTakEngine.nativeSetTapListener(engine, listener == null ? null : new Bridge(), listenerHandle);
            }
        });
    }

    public void setHeadingLockedPan(final boolean enabled) {
        run(new Runnable() {
            @Override
            public void run() {
                OpenTakEngine.nativeSetHeadingLockedPan(engine, enabled);
            }
        });
    }

    private static int modifiers(int meta) {
        int m = 0;
        if ((meta & KeyEvent.META_SHIFT_ON) != 0) m |= OpenTakEngine.MOD_SHIFT;
        if ((meta & KeyEvent.META_CTRL_ON) != 0) m |= OpenTakEngine.MOD_CTRL;
        if ((meta & KeyEvent.META_ALT_ON) != 0) m |= OpenTakEngine.MOD_ALT;
        if ((meta & KeyEvent.META_META_ON) != 0) m |= OpenTakEngine.MOD_SUPER;
        return m;
    }

    private static int pointerType(MotionEvent e, int index) {
        int t = e.getToolType(index);
        if (t == MotionEvent.TOOL_TYPE_MOUSE) return OpenTakEngine.POINTER_MOUSE;
        if (t == MotionEvent.TOOL_TYPE_STYLUS || t == MotionEvent.TOOL_TYPE_ERASER) return OpenTakEngine.POINTER_PEN;
        return OpenTakEngine.POINTER_TOUCH;
    }

    private static int button(MotionEvent e) {
        int b = e.getButtonState();
        if ((b & MotionEvent.BUTTON_SECONDARY) != 0) return OpenTakEngine.BUTTON_RIGHT;
        if ((b & MotionEvent.BUTTON_TERTIARY) != 0) return OpenTakEngine.BUTTON_MIDDLE;
        return OpenTakEngine.BUTTON_LEFT;
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
            final int btn = type == OpenTakEngine.POINTER_MOUSE ? button(e) : OpenTakEngine.BUTTON_LEFT;
            run(new Runnable() {
                @Override
                public void run() {
                    OpenTakEngine.nativePointerDown(engine, id, x, y, type, btn, mods);
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
                    for (int i = 0; i < n; ++i) OpenTakEngine.nativePointerMove(engine, ids[i], xs[i], ys[i], mods);
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
                    OpenTakEngine.nativePointerUp(engine, id, x, y);
                }
            });
            break;
        }
        case MotionEvent.ACTION_CANCEL:
            run(new Runnable() {
                @Override
                public void run() {
                    OpenTakEngine.nativePointerCancelAll(engine);
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
                    OpenTakEngine.nativeWheel(engine, x, y, h, v, false, mods);
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
            return OpenTakEngine.KEY_LEFT;
        case KeyEvent.KEYCODE_DPAD_RIGHT:
        case KeyEvent.KEYCODE_D:
            return OpenTakEngine.KEY_RIGHT;
        case KeyEvent.KEYCODE_DPAD_UP:
        case KeyEvent.KEYCODE_W:
            return OpenTakEngine.KEY_UP;
        case KeyEvent.KEYCODE_DPAD_DOWN:
        case KeyEvent.KEYCODE_S:
            return OpenTakEngine.KEY_DOWN;
        case KeyEvent.KEYCODE_PLUS:
        case KeyEvent.KEYCODE_EQUALS:
        case KeyEvent.KEYCODE_NUMPAD_ADD:
        case KeyEvent.KEYCODE_ZOOM_IN:
            return OpenTakEngine.KEY_ZOOM_IN;
        case KeyEvent.KEYCODE_MINUS:
        case KeyEvent.KEYCODE_NUMPAD_SUBTRACT:
        case KeyEvent.KEYCODE_ZOOM_OUT:
            return OpenTakEngine.KEY_ZOOM_OUT;
        case KeyEvent.KEYCODE_Q:
            return OpenTakEngine.KEY_ROTATE_LEFT;
        case KeyEvent.KEYCODE_E:
            return OpenTakEngine.KEY_ROTATE_RIGHT;
        case KeyEvent.KEYCODE_PAGE_UP:
            return OpenTakEngine.KEY_TILT_UP;
        case KeyEvent.KEYCODE_PAGE_DOWN:
            return OpenTakEngine.KEY_TILT_DOWN;
        case KeyEvent.KEYCODE_N:
            return OpenTakEngine.KEY_RESET_NORTH;
        case KeyEvent.KEYCODE_T:
            return OpenTakEngine.KEY_RESET_TILT;
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
                OpenTakEngine.nativeKey(engine, k, down, mods);
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
