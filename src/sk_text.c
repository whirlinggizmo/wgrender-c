#include "sk_text.h"

#include <stdio.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_font.h"
#include "internal/sk_internal.h"
#include "sk_logger.h"

#include "fontstash.h"
#include "sokol_gfx.h"
#include "util/sokol_fontstash.h"

/* All text is TrueType (fontstash). Font handle 0 means the default font: the one
 * set with sk_text_set_default_font, else the built-in font (an ASCII subset of
 * JetBrains Mono embedded in the library; src/fonts/sk_default_font.h). A font that
 * isn't loaded (invalid or freed handle) falls back to the built-in font. */

#define SK_TEXT_DEFAULT_SIZE 16.0f /* pixel size when a size <= 0 is given */

static sk_handle_t sk_text_builtin_font; /* referenced; 0 if it couldn't be created */
static sk_handle_t sk_text_default_font; /* referenced; 0 = the built-in font */

void sk_text_init(void)
{
    sk_text_default_font = 0;
    sk_text_builtin_font = sk_font_create_builtin();
    if (sk_text_builtin_font == 0) {
        log_error("text: the built-in font couldn't be created");
    }
}

void sk_text_deinit(void)
{
    sk_font_release(sk_text_default_font);
    sk_font_release(sk_text_builtin_font);
    sk_text_default_font = 0;
    sk_text_builtin_font = 0;
}

sk_handle_t sk_text_resolve_font(sk_handle_t font)
{
    if (font == 0) {
        font = sk_text_default_font != 0 ? sk_text_default_font : sk_text_builtin_font;
    }
    return sk_font_fons_id(font) != FONS_INVALID ? font : sk_text_builtin_font;
}

/* Select `font` (resolved) at `size` for drawing or measuring; false if there's no
 * font at all (text not initialized). */
static bool use_font(sk_handle_t font, float size)
{
    FONScontext *fons = sk_font_context();
    const int id = sk_font_fons_id(sk_text_resolve_font(font));
    if (fons == NULL || id == FONS_INVALID) {
        return false;
    }
    fonsSetFont(fons, id);
    fonsSetSize(fons, size > 0.0f ? size : SK_TEXT_DEFAULT_SIZE);
    fonsSetAlign(fons, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
    return true;
}

static void draw_text(sk_handle_t font, const char *text, float x, float y, float size, color_t c)
{
    FONScontext *fons = sk_font_context();
    if (text == NULL || !use_font(font, size)) {
        return;
    }
    fonsSetColor(fons, sfons_rgba((uint8_t)(c.r * 255.0f), (uint8_t)(c.g * 255.0f), (uint8_t)(c.b * 255.0f),
                                  (uint8_t)(c.a * 255.0f)));
    fonsDrawText(fons, x, y, text, NULL);
}

SK_KEEP
bool sk_text_set_default_font(sk_handle_t font)
{
    if (font != 0 && sk_font_fons_id(font) == FONS_INVALID) {
        log_warn("sk_text_set_default_font: %u isn't a loaded font", (unsigned int)font);
        return false;
    }
    sk_font_retain(font); /* the default font holds a reference; no-op for 0 */
    sk_font_release(sk_text_default_font);
    sk_text_default_font = font;
    return true;
}

SK_KEEP
sk_handle_t sk_text_get_default_font(void)
{
    return sk_text_default_font;
}

SK_KEEP
void sk_text_draw(const char *text, int x, int y, int font_size, sk_handle_t color)
{
    draw_text(0, text, (float)x, (float)y, (float)font_size, sk_color_get(color));
}

static void format_fps(char *buf, size_t size)
{
    const double dt = sk_get_fps_delta(); /* frames that ran, not display refreshes */
    snprintf(buf, size, "%d FPS", dt > 0.0 ? (int)(1.0 / dt + 0.5) : 0);
}

SK_KEEP
void sk_text_draw_fps(int x, int y)
{
    char buf[32];
    format_fps(buf, sizeof(buf));
    draw_text(0, buf, (float)x, (float)y, SK_TEXT_DEFAULT_SIZE, (color_t){0.0f, 1.0f, 0.0f, 1.0f});
}

SK_KEEP
void sk_text_draw_fps_ex(sk_handle_t font, float x, float y, float size, sk_handle_t color)
{
    char buf[32];
    format_fps(buf, sizeof(buf));
    draw_text(font, buf, x, y, size, sk_color_get(color));
}

SK_KEEP
int sk_text_measure(const char *text, int font_size)
{
    return (int)(sk_text_measure_ex(0, text, (float)font_size).x + 0.5f);
}

SK_KEEP
void sk_text_draw_ex(sk_handle_t font, const char *text, float x, float y, float size, sk_handle_t color)
{
    draw_text(font, text, x, y, size, sk_color_get(color));
}

SK_KEEP
vec2_t sk_text_measure_ex(sk_handle_t font, const char *text, float size)
{
    float bounds[4] = {0};

    if (text == NULL || !use_font(font, size)) {
        return (vec2_t){0.0f, 0.0f};
    }
    fonsTextBounds(sk_font_context(), 0.0f, 0.0f, text, NULL, bounds);
    return (vec2_t){bounds[2] - bounds[0], bounds[3] - bounds[1]};
}
