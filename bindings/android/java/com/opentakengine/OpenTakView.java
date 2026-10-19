package com.opentakengine;

import android.content.Context;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
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
    private float lastX, lastY, lastX1, lastY1;
    private int lastCount;

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

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        final int count = e.getPointerCount();
        final float x = e.getX(0), y = e.getY(0);
        final float x1 = count > 1 ? e.getX(1) : 0, y1 = count > 1 ? e.getY(1) : 0;
        int action = e.getActionMasked();
        if (action == MotionEvent.ACTION_MOVE && count == lastCount) {
            final float px = lastX, py = lastY, px1 = lastX1, py1 = lastY1;
            run(new Runnable() {
                @Override
                public void run() {
                    if (count == 1) {
                        OpenTakEngine.nativePan(engine, px, py, x, y);
                    } else {
                        double d0 = Math.hypot(px1 - px, py1 - py), d1 = Math.hypot(x1 - x, y1 - y);
                        double a0 = Math.atan2(py1 - py, px1 - px), a1 = Math.atan2(y1 - y, x1 - x);
                        double dy0 = y - py, dy1 = y1 - py1;
                        if (dy0 * dy1 > 0 && Math.abs(d1 - d0) < 6 * density && Math.abs(dy0) > Math.abs(x - px)) {
                            OpenTakEngine.nativeTilt(engine, -(dy0 + dy1) * 0.5 * 0.12 / density);
                        } else {
                            if (d0 > 1 && d1 > 1) OpenTakEngine.nativeZoomAt(engine, (x + x1) * 0.5, (y + y1) * 0.5, d0 / d1);
                            OpenTakEngine.nativeRotate(engine, Math.toDegrees(a1 - a0));
                        }
                    }
                }
            });
        }
        lastCount = (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) ? 0
                  : (action == MotionEvent.ACTION_POINTER_UP ? count - 1 : count);
        if (action == MotionEvent.ACTION_POINTER_UP && count == 2) {
            int keep = e.getActionIndex() == 0 ? 1 : 0;
            lastX = e.getX(keep);
            lastY = e.getY(keep);
        } else {
            lastX = x;
            lastY = y;
            lastX1 = x1;
            lastY1 = y1;
        }
        return true;
    }
}
