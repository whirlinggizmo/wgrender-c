#include "sk_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_font.h"
#include "internal/sk_handle_pool.h"
#include "sk_logger.h"

#include "fonts/sk_default_font.h"
#include "fontstash.h"
#include "sokol_gfx.h"
#include "util/sokol_gl.h"
#include "util/sokol_fontstash.h"

#define MAX_FONTS 64
#define SK_FONT_ATLAS_DIM 1024

/* Font resource: refcounted and deduped by path, like textures. fontstash can't
 * remove a font, so when the last reference goes the fontstash font is parked by
 * path and reused if that path is created again: memory is bounded by the distinct
 * font files, not by how often fonts are created. */
typedef struct {
    int fons_id;
    int ref_count;
    char path[256];
} sk_font_t;

typedef struct {
    int fons_id;
    char path[256];
} sk_font_parked_t;

static sk_font_t sk_fonts[MAX_FONTS];
static sk_handle_pool_t sk_font_pool;
static uint16_t sk_font_free_indices[MAX_FONTS];
static uint16_t sk_font_generations[MAX_FONTS];
static unsigned char sk_font_occupied[MAX_FONTS];
static sk_font_parked_t sk_font_parked[MAX_FONTS];
static int sk_font_parked_count;
static int sk_font_added; /* fonts added to fontstash (their names) */
static FONScontext *sk_fons = NULL;

static sk_font_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_font_pool, handle, &index)) {
        return NULL;
    }
    return &sk_fonts[index];
}

static unsigned char *read_file(const char *path, int *out_size)
{
    FILE *f;
    long size;
    unsigned char *bytes;

    *out_size = 0;
    if (path == NULL || (f = fopen(path, "rb")) == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return NULL; }
    bytes = (unsigned char *)malloc((size_t)size);
    if (bytes == NULL) { fclose(f); return NULL; }
    if (fread(bytes, 1, (size_t)size, f) != (size_t)size) { free(bytes); fclose(f); return NULL; }
    fclose(f);
    *out_size = (int)size;
    return bytes;
}

static sk_handle_t find_by_path(const char *path)
{
    for (uint16_t i = 1; i < MAX_FONTS; i++) {
        if (sk_font_occupied[i] && strcmp(sk_fonts[i].path, path) == 0) {
            return sk_handle_pool_handle_from_index(&sk_font_pool, i);
        }
    }
    return 0;
}

/* Keep a fontstash font for a later create of `path`. When the list is full (more
 * than MAX_FONTS distinct files released) it isn't reused: a later create loads the
 * file again. */
static void park(int fons_id, const char *path)
{
    if (sk_font_parked_count < MAX_FONTS) {
        sk_font_parked[sk_font_parked_count].fons_id = fons_id;
        snprintf(sk_font_parked[sk_font_parked_count++].path, sizeof(sk_font_parked[0].path), "%s", path);
    }
}

/* The parked fontstash font for `path` (removed from the parked list), or FONS_INVALID. */
static int take_parked(const char *path)
{
    for (int i = 0; i < sk_font_parked_count; i++) {
        if (strcmp(sk_font_parked[i].path, path) == 0) {
            const int fid = sk_font_parked[i].fons_id;
            sk_font_parked[i] = sk_font_parked[--sk_font_parked_count];
            return fid;
        }
    }
    return FONS_INVALID;
}

/* Load `path` into fontstash, or FONS_INVALID. */
static int add_font(const char *path)
{
    int size = 0;
    char name[32];
    /* fontstash keeps the TTF bytes: freeData = 1 hands it the buffer to free on
     * context destroy */
    unsigned char *bytes = read_file(path, &size);
    int fid;

    if (bytes == NULL) {
        log_error("Failed to read font: %s", path);
        return FONS_INVALID;
    }
    snprintf(name, sizeof(name), "font%d", sk_font_added++);
    fid = fonsAddFontMem(sk_fons, name, bytes, size, 1);
    if (fid == FONS_INVALID) {
        log_error("fontstash failed to add font: %s", path);
    }
    return fid;
}

SK_KEEP
sk_handle_t sk_font_create(const char *path)
{
    sk_handle_t handle;
    uint16_t index = 0;
    int fid;

    if (sk_fons == NULL || path == NULL) {
        return 0;
    }
    handle = find_by_path(path);
    if (handle != 0) {
        sk_font_retain(handle);
        return handle;
    }
    if (strlen(path) >= sizeof(sk_fonts[0].path)) {
        log_error("Font path too long: %s", path);
        return 0;
    }
    fid = take_parked(path);
    if (fid == FONS_INVALID) {
        fid = add_font(path);
        if (fid == FONS_INVALID) {
            return 0;
        }
    }
    handle = sk_handle_pool_alloc(&sk_font_pool);
    if (handle == 0) {
        log_error("MAX_FONTS reached (%d)", MAX_FONTS);
        park(fid, path); /* keep it for later */
        return 0;
    }
    sk_handle_pool_resolve(&sk_font_pool, handle, &index);
    sk_fonts[index].fons_id = fid;
    sk_fonts[index].ref_count = 1;
    snprintf(sk_fonts[index].path, sizeof(sk_fonts[index].path), "%s", path);
    return handle;
}

/* The embedded default font, deduped under a path no file can have. */
sk_handle_t sk_font_create_builtin(void)
{
    static const char *const path = "<built-in>";
    sk_handle_t handle;
    uint16_t index = 0;
    int fid;

    if (sk_fons == NULL) {
        return 0;
    }
    handle = find_by_path(path);
    if (handle != 0) {
        sk_font_retain(handle);
        return handle;
    }
    fid = take_parked(path);
    if (fid == FONS_INVALID) {
        /* static data: fontstash only reads it, and mustn't free it (freeData = 0) */
        fid = fonsAddFontMem(sk_fons, "builtin", (unsigned char *)sk_default_font_ttf,
                             (int)sizeof(sk_default_font_ttf), 0);
        if (fid == FONS_INVALID) {
            return 0;
        }
    }
    handle = sk_handle_pool_alloc(&sk_font_pool);
    if (handle == 0) {
        park(fid, path);
        return 0;
    }
    sk_handle_pool_resolve(&sk_font_pool, handle, &index);
    sk_fonts[index].fons_id = fid;
    sk_fonts[index].ref_count = 1;
    snprintf(sk_fonts[index].path, sizeof(sk_fonts[index].path), "%s", path);
    return handle;
}

void sk_font_retain(sk_handle_t handle)
{
    sk_font_t *font_ptr = resolve(handle);
    if (font_ptr != NULL) {
        font_ptr->ref_count++;
    }
}

SK_KEEP
void sk_font_release(sk_handle_t handle)
{
    sk_font_t *font_ptr = resolve(handle);
    if (font_ptr == NULL || --font_ptr->ref_count > 0) {
        return;
    }
    park(font_ptr->fons_id, font_ptr->path);
    memset(font_ptr, 0, sizeof(*font_ptr));
    sk_handle_pool_free(&sk_font_pool, handle);
}

/* Drops the caller's reference; the font stays while text objects (or the default
 * font) still use it. */
FONScontext *sk_font_context(void)
{
    return sk_fons;
}

int sk_font_fons_id(sk_handle_t handle)
{
    sk_font_t *font_ptr = resolve(handle);
    return font_ptr != NULL ? font_ptr->fons_id : FONS_INVALID;
}

void sk_font_flush(void)
{
    if (sk_fons != NULL) {
        sfons_flush(sk_fons);
    }
}

void sk_font_init(void)
{
    memset(sk_fonts, 0, sizeof(sk_fonts));
    sk_font_parked_count = 0;
    sk_font_added = 0;
    sk_handle_pool_init(&sk_font_pool,
                        SK_HANDLE_KIND_FONT,
                        MAX_FONTS,
                        sk_font_free_indices,
                        MAX_FONTS,
                        sk_font_generations,
                        sk_font_occupied);

    sk_fons = sfons_create(&(sfons_desc_t){
        .width = SK_FONT_ATLAS_DIM,
        .height = SK_FONT_ATLAS_DIM,
    });
    if (sk_fons == NULL) {
        log_error("failed to create fontstash context");
    }
}

void sk_font_deinit(void)
{
    if (sk_fons != NULL) {
        sfons_destroy(sk_fons);
        sk_fons = NULL;
    }
    sk_handle_pool_reset(&sk_font_pool);
}
