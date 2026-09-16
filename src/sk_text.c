#include "sk_text.h"

#include <stdio.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_font.h"
#include "internal/sk_internal.h"

#include "fontstash.h"
#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "util/sokol_debugtext.h"
#include "util/sokol_fontstash.h"

/* First-milestone text uses sokol_debugtext's built-in 8x8 bitmap fonts.
 * Geometry is baked per draw call, so changing the canvas between calls lets us
 * support different pixel sizes within a frame. Real TTF/fontstash fonts (and
 * the font-handle API) come in a later phase. */

#define SK_TEXT_GLYPH_BASE 8.0f /* native glyph cell size in canvas units */

void sk_text_init(void)
{
    sdtx_setup(&(sdtx_desc_t){
        .fonts[0] = sdtx_font_kc853(),
        .logger.func = slog_func,
    });
}

void sk_text_deinit(void)
{
    sdtx_shutdown();
}

void sk_text_flush(void)
{
    sdtx_draw();
}

static float size_to_scale(int font_size)
{
    float fs = font_size > 0 ? (float)font_size : SK_TEXT_GLYPH_BASE;
    return fs / SK_TEXT_GLYPH_BASE;
}

static void draw_at(const char *text, int x, int y, int font_size, color_t c)
{
    const float scale = size_to_scale(font_size);
    const float fs = scale * SK_TEXT_GLYPH_BASE; /* pixel height of a glyph */

    sdtx_canvas((float)sapp_width() / scale, (float)sapp_height() / scale);
    sdtx_font(0);
    /* pixel position -> character grid: 1 char == fs pixels */
    sdtx_pos((float)x / fs, (float)y / fs);
    sdtx_color4f(c.r, c.g, c.b, c.a);
    sdtx_puts(text);
}

SK_KEEP
void sk_text_draw(const char *text, int x, int y, int font_size, sk_handle_t color)
{
    if (text == NULL) {
        return;
    }
    draw_at(text, x, y, font_size, sk_color_get(color));
}

SK_KEEP
void sk_text_draw_fps(int x, int y)
{
    double dt = sk_get_fps_delta(); /* frames that ran, not display refreshes */
    int fps = dt > 0.0 ? (int)(1.0 / dt + 0.5) : 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d FPS", fps);
    draw_at(buf, x, y, 16, (color_t){0.0f, 1.0f, 0.0f, 1.0f});
}

SK_KEEP
int sk_text_measure(const char *text, int font_size)
{
    int len = text != NULL ? (int)strlen(text) : 0;
    float fs = font_size > 0 ? (float)font_size : SK_TEXT_GLYPH_BASE;
    /* built-in font is monospaced with square 8x8 cells */
    return (int)(len * fs);
}

/* ----------------------------------------------- TrueType (fontstash) ----- */

static bool setup_fons(sk_handle_t font, float size)
{
    FONScontext *fons = sk_font_context();
    int fid = sk_font_fons_id(font);
    if (fons == NULL || fid == FONS_INVALID) {
        return false;
    }
    fonsSetFont(fons, fid);
    fonsSetSize(fons, size);
    fonsSetAlign(fons, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
    return true;
}

SK_KEEP
void sk_text_draw_ex(sk_handle_t font, const char *text, float x, float y,
                     float size, sk_handle_t color)
{
    FONScontext *fons = sk_font_context();
    color_t c;

    if (text == NULL) {
        return;
    }
    if (!setup_fons(font, size)) {
        /* fall back to the built-in bitmap font */
        sk_text_draw(text, (int)x, (int)y, (int)size, color);
        return;
    }
    c = sk_color_get(color);
    fonsSetColor(fons, sfons_rgba((uint8_t)(c.r * 255.0f), (uint8_t)(c.g * 255.0f),
                                  (uint8_t)(c.b * 255.0f), (uint8_t)(c.a * 255.0f)));
    fonsDrawText(fons, x, y, text, NULL);
}

SK_KEEP
vec2_t sk_text_measure_ex(sk_handle_t font, const char *text, float size)
{
    FONScontext *fons = sk_font_context();
    float bounds[4] = {0};

    if (text == NULL || !setup_fons(font, size)) {
        return (vec2_t){0.0f, 0.0f};
    }
    fonsTextBounds(fons, 0.0f, 0.0f, text, NULL, bounds);
    return (vec2_t){bounds[2] - bounds[0], bounds[3] - bounds[1]};
}
