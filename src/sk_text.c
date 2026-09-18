#include "sk_text.h"

#include <stdio.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_color.h"
#include "internal/sk_font.h"
#include "internal/sk_internal.h"
#include "internal/sk_render.h"
#include "sk_logger.h"

#include "fontstash.h"
#include "sokol_gfx.h"
#include "util/sokol_gl.h"
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

/* Framebuffer pixels per logical pixel where text is drawn now: the screen's DPI
 * scale, or 1 inside a render target. Glyphs are rasterized at size x this and drawn
 * scaled back, so they land 1:1 on the pixels instead of being magnified (blurry text
 * on high-DPI screens). Measurement divides it back out: sizes stay logical. */
static float pixel_scale(void)
{
    const float scale = sk_render_pixel_scale();
    return scale > 0.0f ? scale : 1.0f;
}

/* Select `font` (resolved) at `size` logical pixels, rasterized at `scale`; false if
 * there's no font at all (text not initialized). */
static bool use_font(sk_handle_t font, float size, float scale)
{
    FONScontext *fons = sk_font_context();
    const int id = sk_font_fons_id(sk_text_resolve_font(font));
    if (fons == NULL || id == FONS_INVALID) {
        return false;
    }
    fonsSetFont(fons, id);
    fonsSetSize(fons, (size > 0.0f ? size : SK_TEXT_DEFAULT_SIZE) * scale);
    fonsSetAlign(fons, FONS_ALIGN_LEFT | FONS_ALIGN_TOP);
    return true;
}

/* The end of `text`: `length` bytes, or up to the NUL when length < 0. */
static const char *text_end(const char *text, int length)
{
    return length < 0 ? text + strlen(text) : text + length;
}

/* ---- laid-out blocks (wrapping, alignment) ------------------------------ */

/* Advance width of text[start, end) in the selected font's pixels. */
static float span_width(FONScontext *fons, const char *start, const char *end)
{
    float bounds[4] = {0};
    return end > start ? fonsTextBounds(fons, 0.0f, 0.0f, start, end, bounds) : 0.0f;
}

typedef void (*line_fn)(const char *start, const char *end, int index, void *user);

static void emit_line(FONScontext *fons, const char *start, const char *end, int index, float *out_width,
                      line_fn fn, void *user)
{
    const float width = span_width(fons, start, end);
    if (width > *out_width) {
        *out_width = width;
    }
    if (fn != NULL) {
        fn(start, end, index, user);
    }
}

/* Walk the lines of text[text, end), breaking at newlines and, with max_width > 0,
 * before a word that would overflow (a word longer than the box keeps its own line).
 * Everything is in the selected font's pixels. Returns the line count; out_width gets
 * the widest line's width. */
static int walk_lines(FONScontext *fons, const char *text, const char *end, float max_width, float *out_width,
                      line_fn fn, void *user)
{
    const char *line_start = text;
    const char *line_end = text; /* end of what this line holds so far */
    const char *cursor = text;
    int count = 0;

    *out_width = 0.0f;
    if (text >= end) {
        return 0;
    }
    if (max_width <= 0.0f) {
        /* no wrapping: a line is exactly what's between newlines, spaces included, so
           measuring " " gives the space's advance (layout libraries such as Clay add
           that between the words they measure) */
        for (;;) {
            const char *newline = memchr(line_start, '\n', (size_t)(end - line_start));
            if (newline == NULL) {
                if (line_start < end || count == 0) {
                    emit_line(fons, line_start, end, count++, out_width, fn, user);
                }
                return count;
            }
            emit_line(fons, line_start, newline, count++, out_width, fn, user);
            line_start = newline + 1;
        }
    }
    for (;;) { /* wrapping: spaces at a break belong to neither line's width */
        const char *word_start, *word_end;
        while (cursor < end && (*cursor == ' ' || *cursor == '\t')) { /* spaces stay with the line before them */
            cursor++;
        }
        word_start = cursor;
        while (cursor < end && *cursor != '\n' && *cursor != ' ' && *cursor != '\t') {
            cursor++;
        }
        word_end = cursor;
        if (word_end > word_start) {
            if (line_end > line_start && span_width(fons, line_start, word_end) > max_width) {
                emit_line(fons, line_start, line_end, count++, out_width, fn, user);
                line_start = word_start;
            }
            line_end = word_end;
        }
        if (cursor < end && *cursor == '\n') {
            emit_line(fons, line_start, line_end, count++, out_width, fn, user);
            cursor++;
            line_start = line_end = cursor;
            continue;
        }
        if (cursor >= end) {
            break;
        }
    }
    if (line_end > line_start || count == 0) {
        emit_line(fons, line_start, line_end, count++, out_width, fn, user);
    }
    return count;
}

typedef struct {
    const char **starts;
    const char **ends;
    int max_lines;
    int count;
} split_t;

static void collect_line(const char *start, const char *end, int index, void *user)
{
    split_t *ctx = (split_t *)user;
    (void)index;
    if (ctx->count < ctx->max_lines) {
        ctx->starts[ctx->count] = start;
        ctx->ends[ctx->count] = end;
        ctx->count++;
    }
}

int sk_text_split_lines(const char *text, int length, float max_width, const char **starts, const char **ends,
                        int max_lines)
{
    FONScontext *fons = sk_font_context();
    split_t ctx = {.starts = starts, .ends = ends, .max_lines = max_lines, .count = 0};
    float width = 0.0f;

    if (fons == NULL || text == NULL || max_lines <= 0) {
        return 0;
    }
    walk_lines(fons, text, text_end(text, length), max_width, &width, collect_line, &ctx);
    return ctx.count;
}

vec2_t sk_text_block_size(sk_handle_t font, const char *text, int length, float size, float max_width)
{
    FONScontext *fons = sk_font_context();
    const float scale = pixel_scale();
    float ascender = 0.0f, descender = 0.0f, line_height = 0.0f, width = 0.0f;
    int lines;

    if (text == NULL || !use_font(font, size, scale)) {
        return (vec2_t){0.0f, 0.0f};
    }
    fonsVertMetrics(fons, &ascender, &descender, &line_height);
    lines = walk_lines(fons, text, text_end(text, length), max_width * scale, &width, NULL, NULL);
    return (vec2_t){width / scale, (float)lines * line_height / scale};
}

typedef struct {
    FONScontext *fons;
    float left, top, line_height, box_width; /* in the font's (rasterized) pixels */
    sk_text_align_t align_x;
} block_draw_t;

static void draw_line(const char *start, const char *end, int index, void *user)
{
    block_draw_t *ctx = (block_draw_t *)user;
    float x = ctx->left;

    if (end <= start) {
        return;
    }
    if (ctx->align_x == SK_TEXT_ALIGN_CENTER) {
        x += (ctx->box_width - span_width(ctx->fons, start, end)) * 0.5f;
    } else if (ctx->align_x == SK_TEXT_ALIGN_RIGHT) {
        x += ctx->box_width - span_width(ctx->fons, start, end);
    }
    fonsDrawText(ctx->fons, x, ctx->top + (float)index * ctx->line_height, start, end);
}

void sk_text_block_draw(sk_handle_t font, const char *text, int length, float left, float top, float size,
                        sk_color_t color, float max_width, float box_width, sk_text_align_t align_x)
{
    FONScontext *fons = sk_font_context();
    const sk_colorf_t c = sk_color_unpack(color);
    const float scale = pixel_scale();
    block_draw_t ctx;
    float ascender = 0.0f, descender = 0.0f, line_height = 0.0f, width = 0.0f;

    if (text == NULL || !use_font(font, size, scale)) {
        return;
    }
    fonsVertMetrics(fons, &ascender, &descender, &line_height);
    fonsSetColor(fons, sfons_rgba((uint8_t)(c.r * 255.0f), (uint8_t)(c.g * 255.0f), (uint8_t)(c.b * 255.0f),
                                  (uint8_t)(c.a * 255.0f)));
    ctx = (block_draw_t){
        .fons = fons, .left = left * scale, .top = top * scale, .line_height = line_height,
        .box_width = box_width * scale, .align_x = align_x};
    /* lay out and draw in the rasterized pixels, scaled back to logical ones: fontstash
       emits each call's vertices before returning, so they get this matrix */
    sgl_matrix_mode_modelview();
    sgl_push_matrix();
    sgl_scale(1.0f / scale, 1.0f / scale, 1.0f);
    walk_lines(fons, text, text_end(text, length), max_width * scale, &width, draw_line, &ctx);
    sgl_pop_matrix();
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
void sk_text_draw(const char *text, int x, int y, int font_size, sk_color_t color)
{
    sk_text_draw_n(0, text, -1, (float)x, (float)y, (float)font_size, color);
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
    sk_text_draw_n(0, buf, -1, (float)x, (float)y, SK_TEXT_DEFAULT_SIZE, 0x00FF00FFu);
}

SK_KEEP
void sk_text_draw_fps_ex(sk_handle_t font, float x, float y, float size, sk_color_t color)
{
    char buf[32];
    format_fps(buf, sizeof(buf));
    sk_text_draw_n(font, buf, -1, x, y, size, color);
}

SK_KEEP
int sk_text_measure(const char *text, int font_size)
{
    return (int)(sk_text_measure_ex(0, text, (float)font_size).x + 0.5f);
}

SK_KEEP
void sk_text_draw_n(sk_handle_t font, const char *text, int length, float x, float y, float size, sk_color_t color)
{
    sk_text_block_draw(font, text, length, x, y, size, color, 0.0f, 0.0f, SK_TEXT_ALIGN_LEFT);
}

SK_KEEP
vec2_t sk_text_measure_n(sk_handle_t font, const char *text, int length, float size)
{
    return sk_text_block_size(font, text, length, size, 0.0f);
}

SK_KEEP
void sk_text_draw_ex(sk_handle_t font, const char *text, float x, float y, float size, sk_color_t color)
{
    sk_text_draw_n(font, text, -1, x, y, size, color);
}

SK_KEEP
vec2_t sk_text_measure_ex(sk_handle_t font, const char *text, float size)
{
    return sk_text_measure_n(font, text, -1, size);
}
