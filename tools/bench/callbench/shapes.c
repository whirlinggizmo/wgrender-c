// callbench: the shapes of wgrender's C API, without wgrender. Each checks its handle
// against a pool the way wgri_handle_pool_resolve does, then does a little work, so
// neither side can fold the call away. The loops that call these from inside the wasm
// are in loops.c, a separate translation unit (and no LTO), so the calls stay calls.
#include <emscripten.h>
#include <string.h>

#define POOL 1024
static unsigned generation[POOL];
static float transform[POOL][9];
static int tint[POOL];

static inline int resolve(int h) {
    unsigned i = (unsigned)h & 0xffff;
    return i < POOL && generation[i] == ((unsigned)h >> 16);
}

EMSCRIPTEN_KEEPALIVE int cb_make_handle(void) {
    generation[7] = 3;
    return (3 << 16) | 7;
}

// wgr_model_set_tint: handle + int -> bool
EMSCRIPTEN_KEEPALIVE int cb_set_tint(int h, int color) {
    if (!resolve(h)) return 0;
    tint[h & 0xffff] = color;
    return 1;
}

// wgr_model_set_transform: handle + 9 floats -> bool
EMSCRIPTEN_KEEPALIVE int cb_set_transform(int h, float px, float py, float pz, float rx, float ry,
                                          float rz, float sx, float sy, float sz) {
    if (!resolve(h)) return 0;
    float *t = transform[h & 0xffff];
    t[0] = px; t[1] = py; t[2] = pz; t[3] = rx; t[4] = ry; t[5] = rz; t[6] = sx; t[7] = sy; t[8] = sz;
    return 1;
}

// wgr_input_get_mouse_state: a 48-byte struct returned through an out pointer
EMSCRIPTEN_KEEPALIVE void cb_get_mouse(int *out) {
    for (int k = 0; k < 12; k++) out[k] = k + tint[k];
}

// wgr_text_measure: a C string in
EMSCRIPTEN_KEEPALIVE int cb_measure(const char *text, int size) {
    return (int)strlen(text) * size;
}
