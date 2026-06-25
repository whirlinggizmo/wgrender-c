#include "sk_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_font.h"
#include "internal/sk_handle_pool.h"
#include "sk_logger.h"

#include "fontstash.h"
#include "sokol_gfx.h"
#include "util/sokol_gl.h"
#include "util/sokol_fontstash.h"

#define MAX_FONTS 64
#define SK_FONT_ATLAS_DIM 1024

typedef struct {
    int fons_id;
} sk_font_t;

static sk_font_t sk_fonts[MAX_FONTS];
static sk_handle_pool_t sk_font_pool;
static uint16_t sk_font_free_indices[MAX_FONTS];
static uint16_t sk_font_generations[MAX_FONTS];
static unsigned char sk_font_occupied[MAX_FONTS];
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

SK_KEEP
sk_handle_t sk_font_create(const char *path)
{
    sk_handle_t handle;
    uint16_t index = 0;
    unsigned char *bytes;
    int size = 0;
    int fid;

    if (sk_fons == NULL) {
        return 0;
    }
    /* fontstash needs the TTF bytes to outlive the font; hand it the read buffer
     * with freeData = 1 so it owns and frees them on context destroy. */
    bytes = read_file(path, &size);
    if (bytes == NULL) {
        log_error("Failed to read font: %s", path ? path : "(null)");
        return 0;
    }

    handle = sk_handle_pool_alloc(&sk_font_pool);
    if (handle == 0) {
        free(bytes);
        log_error("MAX_FONTS reached (%d)", MAX_FONTS);
        return 0;
    }
    sk_handle_pool_resolve(&sk_font_pool, handle, &index);

    {
        char name[32];
        snprintf(name, sizeof(name), "font%u", (unsigned int)index);
        fid = fonsAddFontMem(sk_fons, name, bytes, size, 1);
    }
    if (fid == FONS_INVALID) {
        sk_handle_pool_free(&sk_font_pool, handle);
        log_error("fontstash failed to add font");
        return 0;
    }
    sk_fonts[index].fons_id = fid;
    return handle;
}

SK_KEEP
void sk_font_destroy(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_font_pool, handle, &index)) {
        return;
    }
    /* fontstash has no per-font delete; the glyph data lives until shutdown.
     * We just release the handle slot. */
    sk_fonts[index].fons_id = FONS_INVALID;
    sk_handle_pool_free(&sk_font_pool, handle);
}

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
