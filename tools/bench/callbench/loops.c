// callbench: the same calls as bench.js makes from JS, made from inside the wasm.
#include <emscripten.h>

int cb_set_tint(int h, int color);
int cb_set_transform(int h, float px, float py, float pz, float rx, float ry, float rz, float sx,
                     float sy, float sz);
void cb_get_mouse(int *out);
int cb_measure(const char *text, int size);

volatile int cb_sink;

EMSCRIPTEN_KEEPALIVE void cb_loop_tint(int n, int h) {
    int s = 0;
    for (int k = 0; k < n; k++) s += cb_set_tint(h, k);
    cb_sink = s;
}

EMSCRIPTEN_KEEPALIVE void cb_loop_transform(int n, int h) {
    int s = 0;
    for (int k = 0; k < n; k++) s += cb_set_transform(h, k, 1, 2, 3, 4, 5, 6, 7, 8);
    cb_sink = s;
}

EMSCRIPTEN_KEEPALIVE void cb_loop_mouse(int n) {
    int out[12];
    int s = 0;
    for (int k = 0; k < n; k++) {
        cb_get_mouse(out);
        s += out[3];
    }
    cb_sink = s;
}

EMSCRIPTEN_KEEPALIVE void cb_loop_measure(int n) {
    int s = 0;
    for (int k = 0; k < n; k++) s += cb_measure("Hello, wgrender", 20);
    cb_sink = s;
}
