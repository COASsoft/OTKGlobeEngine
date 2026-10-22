#include "otk/otk.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

extern "C" {

EMSCRIPTEN_KEEPALIVE otk_engine* otk_web_create(const char* canvas, int antialias) {
    EmscriptenWebGLContextAttributes a;
    emscripten_webgl_init_context_attributes(&a);
    a.majorVersion = 2;
    a.minorVersion = 0;
    a.alpha = false;
    a.depth = true;
    a.stencil = true;
    a.antialias = antialias != 0;
    a.premultipliedAlpha = false;
    a.preserveDrawingBuffer = false;
    a.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context(canvas, &a);
    if (ctx <= 0) return nullptr;
    emscripten_webgl_make_context_current(ctx);
    return otk_create_gl(1, antialias ? 4 : 1);
}

}
