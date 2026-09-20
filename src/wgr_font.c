#include "wgr_asset.h"
#include "wgr_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_font_internal.h"
#include "internal/wgr_handle_pool_internal.h"
#include "wgr_logger.h"

#include "fonts/wgr_default_font.h"
#include "fontstash.h"
#include "sokol_gfx.h"
#include "util/sokol_gl.h"
#include "util/sokol_fontstash.h"

#define FONTS_INITIAL 16 /* slots to start with; the pool doubles as needed */
#define MAX_PARKED 64 /* released fonts fontstash keeps for a later create */
#define WGR_FONT_ATLAS_DIM 1024
#define WGR_FONT_ATLAS_MAX 4096 /* 16 MB of 8-bit coverage at most */

/* Font resource: refcounted and deduped by path, like textures. fontstash can't
 * remove a font, so when the last reference goes the fontstash font is parked by
 * path and reused if that path is created again: memory is bounded by the distinct
 * font files, not by how often fonts are created. */
typedef struct {
    int fons_id;
    int ref_count;
    char path[256];
} wgr_font_t;

typedef struct {
    int fons_id;
    char path[256];
} wgr_font_parked_t;

static wgr_font_t *wgr_fonts; /* grown by the pool: don't hold a pointer across a create */
static wgri_handle_pool_t wgr_font_pool;
static wgr_font_parked_t wgr_font_parked[MAX_PARKED];
static int wgr_font_parked_count;
static int wgr_font_added; /* fonts added to fontstash (their names) */
static FONScontext *wgr_fons = NULL;

static wgr_font_t *resolve(wgr_handle_t handle)
{
    uint16_t index = 0;
    if (!wgri_handle_pool_resolve(&wgr_font_pool, handle, &index)) {
        return NULL;
    }
    return &wgr_fonts[index];
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

static wgr_handle_t find_by_path(const char *path)
{
    for (uint16_t i = 1; i < wgr_font_pool.capacity; i++) {
        if (wgr_font_pool.occupied[i] && strcmp(wgr_fonts[i].path, path) == 0) {
            return wgri_handle_pool_handle_from_index(&wgr_font_pool, i);
        }
    }
    return 0;
}

/* Keep a fontstash font for a later create of `path`. When the list is full (more
 * than MAX_PARKED distinct files released) it isn't reused: a later create loads the
 * file again. */
static void park(int fons_id, const char *path)
{
    if (wgr_font_parked_count < MAX_PARKED) {
        wgr_font_parked[wgr_font_parked_count].fons_id = fons_id;
        snprintf(wgr_font_parked[wgr_font_parked_count++].path, sizeof(wgr_font_parked[0].path), "%s", path);
    }
}

/* The parked fontstash font for `path` (removed from the parked list), or FONS_INVALID. */
static int take_parked(const char *path)
{
    for (int i = 0; i < wgr_font_parked_count; i++) {
        if (strcmp(wgr_font_parked[i].path, path) == 0) {
            const int fid = wgr_font_parked[i].fons_id;
            wgr_font_parked[i] = wgr_font_parked[--wgr_font_parked_count];
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
    snprintf(name, sizeof(name), "font%d", wgr_font_added++);
    fid = fonsAddFontMem(wgr_fons, name, bytes, size, 1);
    if (fid == FONS_INVALID) {
        log_error("fontstash failed to add font: %s", path);
    }
    return fid;
}

WGRI_KEEP
wgr_handle_t wgr_font_create(const char *path)
{
    wgr_handle_t handle;
    uint16_t index = 0;
    int fid;

    if (wgr_fons == NULL || path == NULL) {
        return 0;
    }
    handle = find_by_path(path);
    if (handle != 0) {
        wgri_font_retain(handle);
        return handle;
    }
    if (strlen(path) >= sizeof(wgr_fonts[0].path)) {
        log_error("Font path too long: %s", path);
        return 0;
    }
    fid = take_parked(path);
    if (fid == FONS_INVALID) {
        fid = add_font(path);
        if (fid == FONS_INVALID) {
            /* The file may be a bad cached copy rather than a bad font -- a host that
               compresses once served gzip bytes under a .ttf's name. Forget it, so the
               next attempt fetches it again. A .ttf has no registered loader, so the
               asset layer's own retry (refetch_once) never sees this. */
            wgr_asset_evict(path);
            return 0;
        }
    }
    handle = wgri_handle_pool_alloc(&wgr_font_pool);
    if (handle == 0) {
        log_error("font: pool full (%u)", (unsigned)wgr_font_pool.max - 1u);
        park(fid, path); /* keep it for later */
        return 0;
    }
    wgri_handle_pool_resolve(&wgr_font_pool, handle, &index);
    wgr_fonts[index].fons_id = fid;
    wgr_fonts[index].ref_count = 1;
    snprintf(wgr_fonts[index].path, sizeof(wgr_fonts[index].path), "%s", path);
    return handle;
}

/* The embedded default font, deduped under a path no file can have. */
wgr_handle_t wgri_font_create_builtin(void)
{
    static const char *const path = "<built-in>";
    wgr_handle_t handle;
    uint16_t index = 0;
    int fid;

    if (wgr_fons == NULL) {
        return 0;
    }
    handle = find_by_path(path);
    if (handle != 0) {
        wgri_font_retain(handle);
        return handle;
    }
    fid = take_parked(path);
    if (fid == FONS_INVALID) {
        /* static data: fontstash only reads it, and mustn't free it (freeData = 0) */
        fid = fonsAddFontMem(wgr_fons, "builtin", (unsigned char *)wgr_default_font_ttf,
                             (int)sizeof(wgr_default_font_ttf), 0);
        if (fid == FONS_INVALID) {
            return 0;
        }
    }
    handle = wgri_handle_pool_alloc(&wgr_font_pool);
    if (handle == 0) {
        park(fid, path);
        return 0;
    }
    wgri_handle_pool_resolve(&wgr_font_pool, handle, &index);
    wgr_fonts[index].fons_id = fid;
    wgr_fonts[index].ref_count = 1;
    snprintf(wgr_fonts[index].path, sizeof(wgr_fonts[index].path), "%s", path);
    return handle;
}

void wgri_font_retain(wgr_handle_t handle)
{
    wgr_font_t *font_ptr = resolve(handle);
    if (font_ptr != NULL) {
        font_ptr->ref_count++;
    }
}

WGRI_KEEP
void wgr_font_release(wgr_handle_t handle)
{
    wgr_font_t *font_ptr = resolve(handle);
    if (font_ptr == NULL || --font_ptr->ref_count > 0) {
        return;
    }
    park(font_ptr->fons_id, font_ptr->path);
    memset(font_ptr, 0, sizeof(*font_ptr));
    wgri_handle_pool_free(&wgr_font_pool, handle);
}

/* Drops the caller's reference; the font stays while text objects (or the default
 * font) still use it. */
FONScontext *wgri_font_context(void)
{
    return wgr_fons;
}

int wgri_font_fons_id(wgr_handle_t handle)
{
    wgr_font_t *font_ptr = resolve(handle);
    return font_ptr != NULL ? font_ptr->fons_id : FONS_INVALID;
}

void wgri_font_flush(void)
{
    if (wgr_fons != NULL) {
        sfons_flush(wgr_fons);
    }
}

/* A full glyph atlas grows, but never in the middle of a frame: draws recorded
 * earlier in the frame refer to the atlas texture and to UVs for its size, and
 * growing recreates both. So a full atlas only asks to grow; the glyphs that didn't
 * fit skip this one frame, and wgri_font_end_frame grows it once the frame is
 * submitted. More likely with high-DPI text, whose glyphs take up to four times the
 * area. The smaller side doubles each time, up to WGR_FONT_ATLAS_MAX. */
static bool wgr_font_grow_pending;

static void on_fons_error(void *user, int error, int value)
{
    (void)user;
    (void)value;
    if (error == FONS_ATLAS_FULL) {
        wgr_font_grow_pending = true;
    } else {
        log_warn("font: fontstash error %d", error);
    }
}

void wgri_font_end_frame(void)
{
    static bool full_warned;
    int width = 0, height = 0;

    if (!wgr_font_grow_pending || wgr_fons == NULL) {
        return;
    }
    wgr_font_grow_pending = false;
    fonsGetAtlasSize(wgr_fons, &width, &height);
    if (width < WGR_FONT_ATLAS_MAX || height < WGR_FONT_ATLAS_MAX) {
        const bool grow_width = width <= height && width < WGR_FONT_ATLAS_MAX;
        const int new_width = grow_width ? width * 2 : width, new_height = grow_width ? height : height * 2;
        if (fonsExpandAtlas(wgr_fons, new_width, new_height)) {
            log_info("font: glyph atlas grew to %dx%d", new_width, new_height);
            return;
        }
    }
    if (!full_warned) {
        log_warn("font: glyph atlas full at %dx%d; glyphs that don't fit aren't drawn", width, height);
        full_warned = true;
    }
}

void wgri_font_init(void)
{
    wgr_font_parked_count = 0;
    wgr_font_grow_pending = false;
    wgr_font_added = 0;
    if (!wgri_handle_pool_init(&wgr_font_pool, WGR_HANDLE_KIND_FONT, "font", (void **)&wgr_fonts,
                             sizeof(wgr_font_t), FONTS_INITIAL, WGRI_HANDLE_POOL_MAX_SLOTS)) {
        log_error("font: out of memory");
    }

    wgr_fons = sfons_create(&(sfons_desc_t){
        .width = WGR_FONT_ATLAS_DIM,
        .height = WGR_FONT_ATLAS_DIM,
    });
    if (wgr_fons == NULL) {
        log_error("failed to create fontstash context");
        return;
    }
    fonsSetErrorCallback(wgr_fons, on_fons_error, NULL);
}

void wgri_font_deinit(void)
{
    if (wgr_fons != NULL) {
        sfons_destroy(wgr_fons);
        wgr_fons = NULL;
    }
    wgri_handle_pool_destroy(&wgr_font_pool);
}
