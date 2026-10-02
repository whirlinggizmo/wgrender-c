#include "wgr_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_font_internal.h"
#include "internal/wgr_handle_pool_internal.h"
#include "internal/wgr_loader_internal.h"
#include "internal/wgr_resource_internal.h"
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
    wgri_resource_t resource; /* first: the resource core's part (internal/wgr_resource_internal.h) */
    int fons_id;              /* FONS_INVALID until it's READY */
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

/* A font file's bytes, read on a worker; fontstash takes them in fill. */
typedef struct {
    unsigned char *bytes;
    int size;
} wgr_font_prepared_t;

static void *prepare_font(const char *path)
{
    wgr_font_prepared_t *prepared = calloc(1, sizeof(*prepared));
    if (prepared == NULL) {
        return NULL;
    }
    prepared->bytes = read_file(path, &prepared->size);
    if (prepared->bytes == NULL) {
        wgr_logger_error("Failed to read font: %s", path);
        free(prepared);
        return NULL;
    }
    return prepared;
}

static void discard_font(void *data)
{
    wgr_font_prepared_t *prepared = (wgr_font_prepared_t *)data;
    if (prepared == NULL) return;
    free(prepared->bytes); /* NULL once fontstash took them */
    free(prepared);
}

/* Add the file to fontstash for the PENDING `font`, or take the one parked for its
 * path. A file fontstash won't take fails the load, so on the web the asset layer
 * fetches it once more (a bad cached copy, not a bad font). */
static wgri_loader_step_t fill(void *data, const char *path, wgr_handle_t font)
{
    wgr_font_prepared_t *prepared = (wgr_font_prepared_t *)data;
    wgr_font_t *font_ptr = resolve(font);
    char name[32];
    int fid;

    if (font_ptr == NULL || wgr_fons == NULL) {
        return WGRI_LOADER_FAILED;
    }
    fid = take_parked(font_ptr->resource.path);
    if (fid == FONS_INVALID) {
        snprintf(name, sizeof(name), "font%d", wgr_font_added++);
        /* fontstash keeps the bytes: freeData = 1 frees them with the context */
        fid = fonsAddFontMem(wgr_fons, name, prepared->bytes, prepared->size, 1);
        if (fid == FONS_INVALID) {
            wgr_logger_error("fontstash failed to add font: %s", path);
            return WGRI_LOADER_FAILED;
        }
        prepared->bytes = NULL;
    }
    font_ptr->fons_id = fid;
    return WGRI_LOADER_DONE;
}

static const wgri_loader_t wgr_font_loader = {
    .name = "font",
    .prepare = prepare_font,
    .discard = discard_font,
    .fill = fill,
};

static void init_record(void *record)
{
    ((wgr_font_t *)record)->fons_id = FONS_INVALID;
}

/* fontstash can't remove a font: park it for a later create of the same path. */
static void free_record(void *record)
{
    const wgr_font_t *font_ptr = (const wgr_font_t *)record;
    if (font_ptr->fons_id != FONS_INVALID) {
        park(font_ptr->fons_id, font_ptr->resource.path);
    }
}

static const wgri_resource_kind_t wgr_font_kind = {
    .create = "wgr_font_create",
    .loader = &wgr_font_loader,
    .init = init_record,
    .free = free_record,
};

WGRI_KEEP
wgr_handle_t wgr_font_create(const char *path)
{
    return wgri_resource_create(WGR_HANDLE_KIND_FONT, path);
}

/* The embedded default font, deduped under a path no asset path can be (an asset path
 * never has a ':'). */
wgr_handle_t wgri_font_create_builtin(void)
{
    static const char *const path = ":built-in";
    wgr_handle_t handle;
    wgr_font_t *font_ptr;
    int fid;

    if (wgr_fons == NULL) {
        return 0;
    }
    for (uint16_t i = 1; i < wgr_font_pool.capacity; i++) {
        if (wgr_font_pool.occupied[i] && strcmp(wgr_fonts[i].resource.path, path) == 0) {
            handle = wgri_handle_pool_handle_from_index(&wgr_font_pool, i);
            wgri_resource_retain(handle);
            return handle;
        }
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
    handle = wgri_resource_add(WGR_HANDLE_KIND_FONT);
    font_ptr = resolve(handle);
    if (font_ptr == NULL) {
        park(fid, path);
        return 0;
    }
    font_ptr->fons_id = fid;
    snprintf(font_ptr->resource.path, sizeof(font_ptr->resource.path), "%s", path);
    return handle;
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
        wgr_logger_warn("font: fontstash error %d", error);
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
            wgr_logger_info("font: glyph atlas grew to %dx%d", new_width, new_height);
            return;
        }
    }
    if (!full_warned) {
        wgr_logger_warn("font: glyph atlas full at %dx%d; glyphs that don't fit aren't drawn", width, height);
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
        wgr_logger_error("font: out of memory");
    }

    wgri_resource_register(&wgr_font_pool, &wgr_font_kind);
    wgr_fons = sfons_create(&(sfons_desc_t){
        .width = WGR_FONT_ATLAS_DIM,
        .height = WGR_FONT_ATLAS_DIM,
    });
    if (wgr_fons == NULL) {
        wgr_logger_error("failed to create fontstash context");
        return;
    }
    fonsSetErrorCallback(wgr_fons, on_fons_error, NULL);
}

void wgri_font_deinit(void)
{
    wgri_resource_register(&wgr_font_pool, NULL);
    if (wgr_fons != NULL) {
        sfons_destroy(wgr_fons);
        wgr_fons = NULL;
    }
    wgri_handle_pool_destroy(&wgr_font_pool);
}
