#include "wgr_texture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports_internal.h"
#include "internal/wgr_handle_pool_internal.h"
#include "internal/wgr_ktx_internal.h"
#include "internal/wgr_loader_internal.h"
#include "internal/wgr_texture_internal.h"
#include "internal/wgr_module_internal.h"
#include "internal/wgr_render_internal.h"
#include "wgr_logger.h"

#include "sokol_gfx.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_HDR /* environment maps (wgr_environment.c) */
#define STBI_NO_STDIO /* we feed bytes; sync path reads the file itself */
#include "stb_image.h"

#define TEXTURES_INITIAL 64 /* slots to start with; the pool doubles as needed */
#define WGR_TEXTURE_BUILTIN_COUNT 2 /* index 1 = default white, 2 = missing-texture checker */
#define CHECKER_SIZE 64
#define CHECKER_SQUARE 8

/* Texture resource: shared, refcounted, deduped GPU image (+ optional CPU alpha
 * mask for picking, generated lazily). Many Sprite objects may reference one
 * Texture. */
typedef struct {
    sg_image image; /* sampled image (a target's resolve image when multisampled) */
    sg_view view;
    sg_sampler sampler;
    /* render targets (wgr_texture_create_target) */
    bool target;
    sg_image msaa_image;  /* multisampled color image, when the screen uses MSAA */
    sg_image depth_image;
    sg_view color_attachment;
    sg_view resolve_attachment;
    sg_view depth_attachment;
    int width;
    int height;
    unsigned char *alpha; /* lazy CPU alpha mask (w*h bytes) for picking */
    int ref_count;
    char path[256];
    bool has_path;
} wgr_texture_t;

static wgr_texture_t *wgr_textures; /* grown by the pool: don't hold a pointer across a create */
static wgr_handle_pool_t wgr_texture_pool;
static sg_sampler wgr_default_sampler;
static sg_sampler wgr_texture_samplers[3][3][2][2]; /* [wrap_u][wrap_v][filter][mipmaps], made on first use */
static wgr_handle_t wgr_texture_drawing_into;       /* target being drawn into (render pass), 0 = screen */
static bool wgr_texture_self_use_logged;

static const wgr_handle_t WGR_TEXTURE_DEFAULT = WGR_HANDLE_MAKE(WGR_HANDLE_KIND_TEXTURE, 1, 1);
static const wgr_handle_t WGR_TEXTURE_CHECKER = WGR_HANDLE_MAKE(WGR_HANDLE_KIND_TEXTURE, 2, 1);
static wgr_handle_t wgr_texture_placeholder; /* referenced unless built in */

static wgr_texture_t *resolve(wgr_handle_t handle)
{
    uint16_t index = 0;
    if (!wgr_handle_pool_resolve(&wgr_texture_pool, handle, &index)) {
        if (handle != 0) {
            log_warn("Invalid texture handle (%u)", (unsigned int)handle);
        }
        return NULL;
    }
    return &wgr_textures[index];
}

static bool is_builtin_texture(uint16_t index)
{
    return index <= WGR_TEXTURE_BUILTIN_COUNT;
}

static void free_texture_data(wgr_texture_t *texture_ptr)
{
    if (texture_ptr->view.id != 0) {
        sg_destroy_view(texture_ptr->view);
    }
    if (texture_ptr->image.id != 0) {
        sg_destroy_image(texture_ptr->image);
    }
    if (texture_ptr->target) {
        sg_destroy_view(texture_ptr->color_attachment);
        sg_destroy_view(texture_ptr->resolve_attachment); /* invalid ids are ignored */
        sg_destroy_view(texture_ptr->depth_attachment);
        sg_destroy_image(texture_ptr->msaa_image);
        sg_destroy_image(texture_ptr->depth_image);
    }
    free(texture_ptr->alpha);
}

static void extract_alpha_mask(wgr_texture_t *texture_ptr, const unsigned char *rgba)
{
    int i;
    if (texture_ptr == NULL || rgba == NULL || texture_ptr->width <= 0 || texture_ptr->height <= 0) {
        return;
    }
    if (texture_ptr->alpha != NULL) {
        return;
    }
    texture_ptr->alpha = (unsigned char *)malloc((size_t)(texture_ptr->width * texture_ptr->height));
    if (texture_ptr->alpha == NULL) {
        return;
    }
    for (i = 0; i < texture_ptr->width * texture_ptr->height; i++) {
        texture_ptr->alpha[i] = rgba[i * 4 + 3];
    }
}

static wgr_handle_t find_texture_by_path(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    for (uint16_t i = WGR_TEXTURE_BUILTIN_COUNT + 1; i < wgr_texture_pool.capacity; i++) {
        if (wgr_texture_pool.occupied[i] && wgr_textures[i].has_path &&
            strcmp(wgr_textures[i].path, path) == 0) {
            return wgr_handle_pool_handle_from_index(&wgr_texture_pool, i);
        }
    }
    return 0;
}

static wgr_handle_t alloc_texture_slot(wgr_texture_t *out)
{
    wgr_handle_t handle;
    uint16_t index = 0;

    handle = wgr_handle_pool_alloc(&wgr_texture_pool);
    if (handle == 0) {
        log_error("texture: pool full (%u)", (unsigned)wgr_texture_pool.max - 1u);
        return 0;
    }
    wgr_handle_pool_resolve(&wgr_texture_pool, handle, &index);
    *out = (wgr_texture_t){0};
    out->sampler = wgr_default_sampler;
    wgr_textures[index] = *out;
    return handle;
}

/* ------------------------------------------------------------ pixels ---- */

struct wgr_texture_pixels {
    int width;
    int height;
    int mip_count;
    bool translucent; /* some pixel isn't fully opaque */
    unsigned char *levels[SG_MAX_MIPMAPS];
};

/* Fill levels 1.. from level 0. Each level averages 2x2 texels of the previous one
 * (the last row or column repeats for odd sizes), in the stored color space. */
static bool build_mipmaps(wgr_texture_pixels_t *pixels)
{
    int lw = pixels->width, lh = pixels->height;

    pixels->mip_count = 1;
    while ((lw > 1 || lh > 1) && pixels->mip_count < SG_MAX_MIPMAPS) {
        const unsigned char *src = pixels->levels[pixels->mip_count - 1];
        const int sw = lw, sh = lh;
        unsigned char *dst;
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
        dst = (unsigned char *)malloc((size_t)lw * (size_t)lh * 4);
        if (dst == NULL) {
            return false;
        }
        for (int y = 0; y < lh; y++) {
            const int y0 = y * 2 < sh ? y * 2 : sh - 1, y1 = y * 2 + 1 < sh ? y * 2 + 1 : sh - 1;
            for (int x = 0; x < lw; x++) {
                const int x0 = x * 2 < sw ? x * 2 : sw - 1, x1 = x * 2 + 1 < sw ? x * 2 + 1 : sw - 1;
                for (int c = 0; c < 4; c++) {
                    const int sum = src[(y0 * sw + x0) * 4 + c] + src[(y0 * sw + x1) * 4 + c] +
                                    src[(y1 * sw + x0) * 4 + c] + src[(y1 * sw + x1) * 4 + c];
                    dst[(y * lw + x) * 4 + c] = (unsigned char)((sum + 2) / 4);
                }
            }
        }
        pixels->levels[pixels->mip_count++] = dst;
    }
    return true;
}

/* Takes ownership of `rgba` (malloc'd): adds mipmaps and the translucency flag. */
static wgr_texture_pixels_t *adopt_rgba(unsigned char *rgba, int width, int height)
{
    wgr_texture_pixels_t *pixels = (wgr_texture_pixels_t *)calloc(1, sizeof(wgr_texture_pixels_t));

    if (pixels == NULL) {
        free(rgba);
        return NULL;
    }
    pixels->width = width;
    pixels->height = height;
    pixels->levels[0] = rgba;
    for (size_t i = 0; i < (size_t)width * (size_t)height && !pixels->translucent; i++) {
        pixels->translucent = rgba[i * 4 + 3] != 255;
    }
    if (!build_mipmaps(pixels)) {
        wgr_texture_pixels_free(pixels);
        return NULL;
    }
    return pixels;
}

wgr_texture_pixels_t *wgr_texture_pixels_decode(const unsigned char *bytes, int size)
{
    int w = 0, h = 0, comp = 0;
    stbi_uc *rgba;

    if (bytes == NULL || size <= 0) {
        return NULL;
    }
    rgba = stbi_load_from_memory(bytes, size, &w, &h, &comp, 4); /* malloc'd (STBI_MALLOC) */
    return rgba != NULL ? adopt_rgba(rgba, w, h) : NULL;
}

const char *wgr_texture_pixels_error(void)
{
    return stbi_failure_reason(); /* per thread */
}

wgr_texture_pixels_t *wgr_texture_pixels_from_rgba(const unsigned char *rgba, int width, int height)
{
    unsigned char *copy;

    if (rgba == NULL || width <= 0 || height <= 0) {
        return NULL;
    }
    copy = (unsigned char *)malloc((size_t)width * (size_t)height * 4);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, rgba, (size_t)width * (size_t)height * 4);
    return adopt_rgba(copy, width, height);
}

void wgr_texture_pixels_free(wgr_texture_pixels_t *pixels)
{
    if (pixels == NULL) {
        return;
    }
    for (int i = 0; i < SG_MAX_MIPMAPS; i++) {
        free(pixels->levels[i]);
    }
    free(pixels);
}

static sg_image make_image(const wgr_texture_pixels_t *pixels)
{
    sg_image_desc desc = {.width = pixels->width, .height = pixels->height,
                          .pixel_format = SG_PIXELFORMAT_RGBA8, .num_mipmaps = pixels->mip_count};
    int lw = pixels->width, lh = pixels->height;

    for (int i = 0; i < pixels->mip_count; i++) {
        desc.data.mip_levels[i] = (sg_range){.ptr = pixels->levels[i], .size = (size_t)lw * (size_t)lh * 4};
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
    }
    return sg_make_image(&desc);
}

/* A texture around `image` (taken over; destroyed on failure), loaded from `path` (or
 * none); its slot index in *index_out. */
static wgr_handle_t add_texture(sg_image image, int width, int height, const char *path, uint16_t *index_out)
{
    wgr_handle_t handle;
    uint16_t index = 0;
    wgr_texture_t t = {0};

    handle = alloc_texture_slot(&t);
    if (handle == 0) {
        sg_destroy_image(image);
        return 0;
    }
    wgr_handle_pool_resolve(&wgr_texture_pool, handle, &index);

    t.width = width;
    t.height = height;
    t.sampler = wgr_default_sampler;
    t.ref_count = 1;
    if (path != NULL && path[0] != '\0') {
        size_t n = strlen(path);
        if (n >= sizeof(t.path)) {
            n = sizeof(t.path) - 1;
        }
        memcpy(t.path, path, n);
        t.path[n] = '\0';
        t.has_path = true;
    }
    t.image = image;
    if (sg_query_image_state(t.image) == SG_RESOURCESTATE_VALID) {
        t.view = sg_make_view(&(sg_view_desc){.texture.image = t.image});
    }
    if (sg_query_view_state(t.view) != SG_RESOURCESTATE_VALID) {
        log_error("Couldn't create a GPU image for %s (see the sokol error above)", t.has_path ? t.path : "texture");
        sg_destroy_view(t.view);
        sg_destroy_image(t.image);
        wgr_handle_pool_free(&wgr_texture_pool, handle);
        return 0;
    }
    wgr_textures[index] = t;
    *index_out = index;
    return handle;
}

wgr_handle_t wgr_texture_create_pixels(const wgr_texture_pixels_t *pixels, const char *path, bool keep_alpha)
{
    uint16_t index = 0;
    wgr_handle_t handle;

    if (pixels == NULL) {
        return 0;
    }
    handle = add_texture(make_image(pixels), pixels->width, pixels->height, path, &index);
    if (handle != 0 && keep_alpha && pixels->translucent) {
        extract_alpha_mask(&wgr_textures[index], pixels->levels[0]);
    }
    return handle;
}

static unsigned char *read_file_bytes(const char *path, int *out_size)
{
    FILE *f;
    long size;
    unsigned char *bytes;
    size_t read;

    *out_size = 0;
    if (path == NULL || (f = fopen(path, "rb")) == NULL) {
        log_error("Failed to open texture: %s", path ? path : "(null)");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    bytes = (unsigned char *)malloc((size_t)size);
    if (bytes == NULL) {
        fclose(f);
        return NULL;
    }
    read = fread(bytes, 1, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size) {
        free(bytes);
        return NULL;
    }
    *out_size = (int)size;
    return bytes;
}

/* ------------------------------------------------------------ loader ---- */

static void *prepare_texture(const char *path)
{
    int size = 0;
    unsigned char *bytes = read_file_bytes(path, &size);
    wgr_texture_pixels_t *pixels;

    if (bytes == NULL) {
        return NULL;
    }
    pixels = wgr_texture_pixels_decode(bytes, size);
    free(bytes);
    if (pixels == NULL) {
        log_error("Failed to decode image %s (%s)", path, wgr_texture_pixels_error());
    }
    return pixels;
}

static wgr_loader_step_t finish_texture(void *prepared, const char *path, wgr_handle_t *resource)
{
    *resource = wgr_texture_create_pixels((const wgr_texture_pixels_t *)prepared, path, false);
    return *resource != 0 ? WGR_LOADER_DONE : WGR_LOADER_FAILED;
}

static void discard_texture(void *prepared)
{
    wgr_texture_pixels_free((wgr_texture_pixels_t *)prepared);
}

static wgr_handle_t find_texture(const char *path)
{
    const wgr_handle_t texture = find_texture_by_path(path);
    wgr_texture_retain(texture);
    return texture;
}

static const wgr_loader_t wgr_texture_loader = {
    .name = "texture",
    .prepare = prepare_texture,
    .finish = finish_texture,
    .discard = discard_texture,
    .find = find_texture,
    .release = wgr_texture_release,
};

/* ------------------------------------------------ compressed textures (KTX) ---- */

/* textures/rock.ktx names a texture compressed for GPUs (tools/compress_textures.sh):
 * rock.bc7.ktx (desktops), rock.astc.ktx (phones), rock.etc2.ktx (older phones), and
 * rock.png for anything else. The first this GPU can sample is the one loaded (and on
 * the web, the only one downloaded): the asset layer and wgr_texture_create both map
 * the path. */
static const struct {
    const char *suffix;
    sg_pixel_format format;
} KTX_VARIANTS[] = {
    {".bc7.ktx", SG_PIXELFORMAT_BC7_RGBA},
    {".astc.ktx", SG_PIXELFORMAT_ASTC_4x4_RGBA},
    {".etc2.ktx", SG_PIXELFORMAT_ETC2_RGBA8},
};
enum { KTX_VARIANT_COUNT = sizeof(KTX_VARIANTS) / sizeof(KTX_VARIANTS[0]) };
static int wgr_ktx_support = -1; /* bit i: variant i usable; -1: ask the GPU */

static bool ends_with(const char *s, const char *suffix)
{
    const size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

void wgr_texture_set_ktx_support(int mask)
{
    wgr_ktx_support = mask;
}

static bool variant_supported(int i)
{
    if (wgr_ktx_support >= 0) return (wgr_ktx_support >> i) & 1;
    return sg_query_pixelformat(KTX_VARIANTS[i].format).sample;
}

bool wgr_texture_ktx_path(const char *path, char *out, size_t out_size)
{
    size_t stem;
    if (path == NULL || !ends_with(path, ".ktx")) return false;
    for (int i = 0; i < KTX_VARIANT_COUNT; i++) {
        if (ends_with(path, KTX_VARIANTS[i].suffix)) { /* a variant already */
            return snprintf(out, out_size, "%s", path) < (int)out_size;
        }
    }
    stem = strlen(path) - 4;
    for (int i = 0; i < KTX_VARIANT_COUNT; i++) {
        if (variant_supported(i)) {
            return snprintf(out, out_size, "%.*s%s", (int)stem, path, KTX_VARIANTS[i].suffix) < (int)out_size;
        }
    }
    return snprintf(out, out_size, "%.*s.png", (int)stem, path) < (int)out_size; /* no compressed format here */
}

/* The PNG beside name.ktx, used when this GPU's variant is missing: false for a path
 * that isn't a plain name.ktx (a variant named outright has no fallback). */
static bool ktx_fallback(const char *path, char *out, size_t out_size)
{
    if (path == NULL || !ends_with(path, ".ktx")) return false;
    for (int i = 0; i < KTX_VARIANT_COUNT; i++) {
        if (ends_with(path, KTX_VARIANTS[i].suffix)) return false;
    }
    return snprintf(out, out_size, "%.*s.png", (int)(strlen(path) - 4), path) < (int)out_size;
}

/* wgr_asset's path mapper for .ktx: this GPU's variant, falling back to the PNG. */
static bool map_ktx(const char *path, char *out, size_t out_size, char *fallback, size_t fallback_size)
{
    if (!wgr_texture_ktx_path(path, out, out_size)) return false;
    if (!ends_with(out, ".ktx") || !ktx_fallback(path, fallback, fallback_size)) fallback[0] = '\0';
    return true;
}

static bool file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f != NULL) fclose(f);
    return f != NULL;
}

typedef struct {
    unsigned char *bytes; /* the file; ktx points into it */
    wgr_ktx_t ktx;
} wgr_ktx_file_t;

static void *prepare_ktx(const char *path)
{
    int size = 0;
    const char *error = NULL;
    wgr_ktx_file_t *file = calloc(1, sizeof(*file));
    if (file == NULL) return NULL;
    file->bytes = read_file_bytes(path, &size);
    if (file->bytes == NULL) {
        free(file);
        return NULL;
    }
    if (!wgr_ktx_parse(file->bytes, (size_t)size, &file->ktx, &error)) {
        log_error("Can't load %s: %s", path, error);
        free(file->bytes);
        free(file);
        return NULL;
    }
    return file;
}

static wgr_handle_t create_ktx(const wgr_ktx_t *ktx, const char *path)
{
    sg_image_desc desc = {.width = ktx->width, .height = ktx->height, .pixel_format = ktx->format,
                          .num_mipmaps = ktx->mip_count, .label = "wgr-texture-ktx"};
    uint16_t index = 0;
    if (!sg_query_pixelformat(ktx->format).sample) {
        log_error("Can't load %s: this GPU can't sample its format", path != NULL ? path : "a compressed texture");
        return 0;
    }
    for (int i = 0; i < ktx->mip_count; i++) {
        desc.data.mip_levels[i] = (sg_range){.ptr = ktx->levels[i], .size = ktx->sizes[i]};
    }
    return add_texture(sg_make_image(&desc), ktx->width, ktx->height, path, &index);
}

wgr_handle_t wgr_texture_create_ktx(const wgr_ktx_t *ktx)
{
    return create_ktx(ktx, NULL);
}

static wgr_loader_step_t finish_ktx(void *prepared, const char *path, wgr_handle_t *resource)
{
    *resource = create_ktx(&((const wgr_ktx_file_t *)prepared)->ktx, path);
    return *resource != 0 ? WGR_LOADER_DONE : WGR_LOADER_FAILED;
}

static void discard_ktx(void *prepared)
{
    wgr_ktx_file_t *file = (wgr_ktx_file_t *)prepared;
    if (file == NULL) return;
    free(file->bytes);
    free(file);
}

static const wgr_loader_t wgr_ktx_loader = {
    .name = "compressed texture",
    .prepare = prepare_ktx,
    .finish = finish_ktx,
    .discard = discard_ktx,
    .find = find_texture,
    .release = wgr_texture_release,
};

wgr_handle_t wgr_texture_create_rgba(const unsigned char *rgba, int width, int height)
{
    wgr_texture_pixels_t *pixels = wgr_texture_pixels_from_rgba(rgba, width, height);
    const wgr_handle_t handle = wgr_texture_create_pixels(pixels, NULL, true); /* no source to re-read later */
    wgr_texture_pixels_free(pixels);
    return handle;
}

bool wgr_texture_get_alpha_mask(wgr_handle_t handle, const unsigned char **alpha, int *width, int *height)
{
    wgr_texture_t *texture_ptr = resolve(handle);
    if (texture_ptr == NULL) {
        return false;
    }
    if (texture_ptr->alpha == NULL && (!texture_ptr->has_path || !wgr_texture_ensure_alpha_mask(handle))) {
        return false;
    }
    *alpha = texture_ptr->alpha;
    *width = texture_ptr->width;
    *height = texture_ptr->height;
    return true;
}

void wgr_texture_retain(wgr_handle_t handle)
{
    wgr_texture_t *texture_ptr = resolve(handle);
    if (texture_ptr != NULL) {
        texture_ptr->ref_count++;
    }
}

WGR_KEEP
void wgr_texture_release(wgr_handle_t handle)
{
    uint16_t index = 0;
    wgr_texture_t *texture_ptr;

    if (!wgr_handle_pool_resolve(&wgr_texture_pool, handle, &index)) {
        return;
    }
    if (is_builtin_texture(index)) {
        return;
    }
    texture_ptr = &wgr_textures[index];
    if (texture_ptr->ref_count > 0) {
        texture_ptr->ref_count--;
    }
    if (texture_ptr->ref_count == 0) {
        free_texture_data(texture_ptr);
        memset(texture_ptr, 0, sizeof(*texture_ptr));
        wgr_handle_pool_free(&wgr_texture_pool, handle);
    }
}

bool wgr_texture_ensure_alpha_mask(wgr_handle_t handle)
{
    wgr_texture_t *texture_ptr = resolve(handle);
    unsigned char *bytes;
    int size = 0;
    stbi_uc *pixels;
    int w, h, comp;

    if (texture_ptr == NULL || texture_ptr->alpha != NULL) {
        return texture_ptr != NULL && texture_ptr->alpha != NULL;
    }
    if (!texture_ptr->has_path) {
        return false;
    }
    if (ends_with(texture_ptr->path, ".ktx")) { /* compressed: its pixels are in the PNG beside it */
        char png[sizeof(texture_ptr->path)];
        const char *dot = strrchr(texture_ptr->path, '.');
        const char *variant = dot;
        while (variant > texture_ptr->path && variant[-1] != '.' && variant[-1] != '/') variant--;
        if (variant > texture_ptr->path && variant[-1] == '.') dot = variant - 1; /* rock.bc7.ktx -> rock */
        snprintf(png, sizeof(png), "%.*s.png", (int)(dot - texture_ptr->path), texture_ptr->path);
        bytes = read_file_bytes(png, &size);
    } else {
        bytes = read_file_bytes(texture_ptr->path, &size);
    }
    if (bytes == NULL) {
        return false;
    }
    pixels = stbi_load_from_memory(bytes, size, &w, &h, &comp, 4);
    free(bytes);
    if (pixels == NULL) {
        log_error("Failed to decode image for alpha mask (%s)", stbi_failure_reason());
        return false;
    }
    if (w != texture_ptr->width || h != texture_ptr->height) {
        stbi_image_free(pixels);
        log_warn("Alpha mask decode size mismatch for %s", texture_ptr->path);
        return false;
    }
    extract_alpha_mask(texture_ptr, pixels);
    stbi_image_free(pixels);
    return texture_ptr->alpha != NULL;
}

/* --------------------------------------------- public API (texture resource) */

WGR_KEEP
wgr_handle_t wgr_texture_get_default(void)
{
    return WGR_TEXTURE_DEFAULT;
}

static sg_wrap to_sg_wrap(wgr_texture_wrap_t wrap)
{
    switch (wrap) {
        case WGR_TEXTURE_WRAP_REPEAT: return SG_WRAP_REPEAT;
        case WGR_TEXTURE_WRAP_MIRROR: return SG_WRAP_MIRRORED_REPEAT;
        default: return SG_WRAP_CLAMP_TO_EDGE;
    }
}

WGR_KEEP
bool wgr_texture_set_sampling(wgr_handle_t texture, wgr_texture_wrap_t wrap_u, wgr_texture_wrap_t wrap_v,
                             wgr_texture_filter_t filter)
{
    wgr_texture_t *texture_ptr = resolve(texture);

    if (texture_ptr == NULL) {
        return false;
    }
    if (wrap_u < WGR_TEXTURE_WRAP_REPEAT || wrap_u > WGR_TEXTURE_WRAP_MIRROR || wrap_v < WGR_TEXTURE_WRAP_REPEAT ||
        wrap_v > WGR_TEXTURE_WRAP_MIRROR || filter < WGR_TEXTURE_FILTER_LINEAR || filter > WGR_TEXTURE_FILTER_NEAREST) {
        log_warn("wgr_texture_set_sampling: invalid wrap or filter");
        return false;
    }
    texture_ptr->sampler = wgr_texture_sampler(wrap_u, wrap_v, filter, true);
    return true;
}

sg_sampler wgr_texture_sampler(wgr_texture_wrap_t wrap_u, wgr_texture_wrap_t wrap_v, wgr_texture_filter_t filter,
                              bool mipmaps)
{
    const int u = wrap_u >= 0 && wrap_u < 3 ? (int)wrap_u : 0;
    const int v = wrap_v >= 0 && wrap_v < 3 ? (int)wrap_v : 0;
    const int nearest = filter == WGR_TEXTURE_FILTER_NEAREST ? 1 : 0;
    sg_sampler *smp = &wgr_texture_samplers[u][v][nearest][mipmaps ? 1 : 0];
    if (smp->id == SG_INVALID_ID) {
        const sg_filter f = nearest ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;
        *smp = sg_make_sampler(&(sg_sampler_desc){
            .min_filter = f,
            .mag_filter = f,
            .mipmap_filter = f,
            .max_lod = mipmaps ? 1000.0f : 0.0f, /* 0: the base level only */
            .wrap_u = to_sg_wrap((wgr_texture_wrap_t)u),
            .wrap_v = to_sg_wrap((wgr_texture_wrap_t)v),
        });
    }
    return *smp;
}

WGR_KEEP
wgr_handle_t wgr_texture_create_target(int width, int height)
{
    const sg_environment_defaults env = sg_query_desc().environment.defaults;
    const int samples = env.sample_count > 1 ? env.sample_count : 1;
    const sg_pixel_format color_format = env.color_format != _SG_PIXELFORMAT_DEFAULT ? env.color_format
                                                                                     : SG_PIXELFORMAT_RGBA8;
    const sg_pixel_format depth_format = env.depth_format != _SG_PIXELFORMAT_DEFAULT ? env.depth_format
                                                                                     : SG_PIXELFORMAT_DEPTH_STENCIL;
    wgr_texture_t t = {0};
    wgr_handle_t handle;
    uint16_t index = 0;

    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        log_error("wgr_texture_create_target: invalid size %dx%d", width, height);
        return 0;
    }
    handle = alloc_texture_slot(&t);
    if (handle == 0) {
        return 0;
    }
    t.width = width;
    t.height = height;
    t.target = true;
    t.ref_count = 1;
    t.sampler = wgr_default_sampler;
    /* same format and MSAA as the screen, so every screen pipeline works in a target */
    t.image = sg_make_image(&(sg_image_desc){
        .usage = {.color_attachment = samples == 1, .resolve_attachment = samples > 1},
        .width = width,
        .height = height,
        .pixel_format = color_format,
        .sample_count = 1,
        .label = "wgr-target-color",
    });
    if (samples > 1) {
        t.msaa_image = sg_make_image(&(sg_image_desc){
            .usage.color_attachment = true,
            .width = width,
            .height = height,
            .pixel_format = color_format,
            .sample_count = samples,
            .label = "wgr-target-msaa",
        });
        t.color_attachment = sg_make_view(&(sg_view_desc){.color_attachment.image = t.msaa_image});
        t.resolve_attachment = sg_make_view(&(sg_view_desc){.resolve_attachment.image = t.image});
    } else {
        t.color_attachment = sg_make_view(&(sg_view_desc){.color_attachment.image = t.image});
    }
    t.depth_image = sg_make_image(&(sg_image_desc){
        .usage.depth_stencil_attachment = true,
        .width = width,
        .height = height,
        .pixel_format = depth_format,
        .sample_count = samples,
        .label = "wgr-target-depth",
    });
    t.depth_attachment = sg_make_view(&(sg_view_desc){.depth_stencil_attachment.image = t.depth_image});
    t.view = sg_make_view(&(sg_view_desc){.texture.image = t.image});
    wgr_handle_pool_resolve(&wgr_texture_pool, handle, &index);
    wgr_textures[index] = t;
    return handle;
}

WGR_KEEP
wgr_handle_t wgr_texture_get_placeholder(void)
{
    return wgr_texture_placeholder;
}

WGR_KEEP
bool wgr_texture_set_placeholder(wgr_handle_t texture)
{
    if (texture == 0) {
        texture = WGR_TEXTURE_CHECKER;
    } else if (resolve(texture) == NULL) {
        return false;
    }
    if (texture != wgr_texture_placeholder) {
        wgr_texture_retain(texture); /* before releasing, in case they're the same resource */
        wgr_texture_release(wgr_texture_placeholder);
        wgr_texture_placeholder = texture;
    }
    return true;
}

WGR_KEEP
wgr_handle_t wgr_texture_create(const char *path)
{
    char mapped[256], fallback[256];
    if (wgr_texture_ktx_path(path, mapped, sizeof(mapped))) { /* rock.ktx: this GPU's variant */
        if (ends_with(mapped, ".ktx") && !file_exists(mapped) && ktx_fallback(path, fallback, sizeof(fallback))) {
            log_warn("Texture %s not found; using %s instead", mapped, fallback);
            snprintf(mapped, sizeof(mapped), "%s", fallback);
        }
        path = mapped;
    }
    return wgr_loader_create(path != NULL && ends_with(path, ".ktx") ? &wgr_ktx_loader : &wgr_texture_loader, path);
}

WGR_KEEP
vec2_t wgr_texture_get_size(wgr_handle_t handle)
{
    wgr_texture_t *texture_ptr = resolve(handle);
    if (texture_ptr == NULL) {
        return (vec2_t){0.0f, 0.0f};
    }
    return (vec2_t){(float)texture_ptr->width, (float)texture_ptr->height};
}


bool wgr_texture_sample_alpha(wgr_handle_t handle, float u, float v, float *out_alpha)
{
    wgr_texture_t *texture_ptr = resolve(handle);
    int px, py;

    if (texture_ptr == NULL || texture_ptr->alpha == NULL || texture_ptr->width <= 0 || texture_ptr->height <= 0) {
        return false;
    }
    if (u < 0.0f) {
        u = 0.0f;
    } else if (u > 1.0f) {
        u = 1.0f;
    }
    if (v < 0.0f) {
        v = 0.0f;
    } else if (v > 1.0f) {
        v = 1.0f;
    }
    px = (int)(u * (float)(texture_ptr->width - 1) + 0.5f);
    py = (int)(v * (float)(texture_ptr->height - 1) + 0.5f);
    if (out_alpha != NULL) {
        *out_alpha = (float)texture_ptr->alpha[py * texture_ptr->width + px] / 255.0f;
    }
    return true;
}

bool wgr_texture_get_binding(wgr_handle_t handle, sg_view *view, sg_sampler *smp,
                            int *width, int *height)
{
    wgr_texture_t *texture_ptr = resolve(handle);
    if (handle != 0 && handle == wgr_texture_drawing_into) {
        /* a pass can't sample the texture it renders into */
        if (!wgr_texture_self_use_logged) {
            log_warn("texture: a render target can't be drawn into itself; the default texture is used");
            wgr_texture_self_use_logged = true;
        }
        texture_ptr = NULL;
    }
    if (texture_ptr == NULL) {
        texture_ptr = resolve(WGR_TEXTURE_DEFAULT);
        if (texture_ptr == NULL) {
            return false;
        }
    }
    if (view) {
        *view = texture_ptr->view;
    }
    if (smp) {
        *smp = texture_ptr->sampler;
    }
    if (width) {
        *width = texture_ptr->width;
    }
    if (height) {
        *height = texture_ptr->height;
    }
    return true;
}

void wgr_texture_init(void)
{
    wgr_render_hooks.texture_target = wgr_texture_get_target;
    wgr_render_hooks.texture_drawing_into = wgr_texture_set_drawing_into;
    static const unsigned char white[4] = {255, 255, 255, 255};
    uint16_t index = 0;

    if (!wgr_handle_pool_init(&wgr_texture_pool, WGR_HANDLE_KIND_TEXTURE, "texture", (void **)&wgr_textures,
                             sizeof(wgr_texture_t), TEXTURES_INITIAL, WGR_HANDLE_POOL_MAX_SLOTS)) {
        log_error("texture: out of memory");
    }
    wgr_asset_register_loader(".png", &wgr_texture_loader);
    wgr_asset_register_loader(".jpg", &wgr_texture_loader);
    wgr_asset_register_loader(".jpeg", &wgr_texture_loader);
    wgr_asset_register_loader(".ktx", &wgr_ktx_loader);
    wgr_asset_register_path_mapper(".ktx", map_ktx);

    wgr_default_sampler = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .mipmap_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    });
    wgr_texture_samplers[WGR_TEXTURE_WRAP_CLAMP][WGR_TEXTURE_WRAP_CLAMP][WGR_TEXTURE_FILTER_LINEAR][1] = wgr_default_sampler;
    wgr_texture_drawing_into = 0;
    wgr_texture_self_use_logged = false;

    /* reserve + populate the built-in 1x1 white default at index 1 */
    wgr_texture_pool.generations[1] = 1;
    wgr_texture_pool.occupied[1] = 1;
    wgr_texture_pool.next_index = WGR_TEXTURE_BUILTIN_COUNT + 1;
    wgr_handle_pool_resolve(&wgr_texture_pool, WGR_TEXTURE_DEFAULT, &index);
    wgr_textures[index].width = 1;
    wgr_textures[index].height = 1;
    wgr_textures[index].image = sg_make_image(&(sg_image_desc){
        .width = 1,
        .height = 1,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0] = {.ptr = white, .size = sizeof(white)},
    });
    wgr_textures[index].view = sg_make_view(&(sg_view_desc){.texture.image = wgr_textures[index].image});
    wgr_textures[index].sampler = wgr_default_sampler;

    /* built-in placeholder at index 2: magenta and black checks, hard to miss */
    {
        unsigned char checker[CHECKER_SIZE * CHECKER_SIZE * 4];
        for (int y = 0; y < CHECKER_SIZE; y++) {
            for (int x = 0; x < CHECKER_SIZE; x++) {
                const bool magenta = ((x / CHECKER_SQUARE) + (y / CHECKER_SQUARE)) % 2 == 0;
                unsigned char *p = &checker[(y * CHECKER_SIZE + x) * 4];
                p[0] = magenta ? 255 : 20;
                p[1] = 0;
                p[2] = magenta ? 255 : 20;
                p[3] = 255;
            }
        }
        wgr_texture_pool.generations[2] = 1;
        wgr_texture_pool.occupied[2] = 1;
        wgr_handle_pool_resolve(&wgr_texture_pool, WGR_TEXTURE_CHECKER, &index);
        wgr_textures[index].width = CHECKER_SIZE;
        wgr_textures[index].height = CHECKER_SIZE;
        wgr_texture_pixels_t *pixels = wgr_texture_pixels_from_rgba(checker, CHECKER_SIZE, CHECKER_SIZE);
        wgr_textures[index].image = make_image(pixels);
        wgr_texture_pixels_free(pixels);
        wgr_textures[index].view = sg_make_view(&(sg_view_desc){.texture.image = wgr_textures[index].image});
        wgr_textures[index].sampler = wgr_default_sampler;
    }
    wgr_texture_placeholder = WGR_TEXTURE_CHECKER;
}

void wgr_texture_deinit(void)
{
    wgr_render_hooks.texture_target = NULL;
    wgr_render_hooks.texture_drawing_into = NULL;
    for (uint16_t i = 1; i < wgr_texture_pool.capacity; i++) {
        if (wgr_texture_pool.occupied[i]) {
            free_texture_data(&wgr_textures[i]);
            wgr_textures[i] = (wgr_texture_t){0};
        }
    }
    for (int i = 0; i < 3 * 3 * 2 * 2; i++) {
        sg_sampler *smp = &((sg_sampler *)wgr_texture_samplers)[i];
        if (smp->id != SG_INVALID_ID) sg_destroy_sampler(*smp); /* includes the default */
        *smp = (sg_sampler){0};
    }
    wgr_default_sampler = (sg_sampler){0};
    wgr_texture_placeholder = 0;
    wgr_handle_pool_destroy(&wgr_texture_pool);
}

bool wgr_texture_get_target(wgr_handle_t handle, sg_attachments *attachments, int *width, int *height)
{
    wgr_texture_t *texture_ptr = resolve(handle);
    if (texture_ptr == NULL || !texture_ptr->target) {
        return false;
    }
    *attachments = (sg_attachments){
        .colors[0] = texture_ptr->color_attachment,
        .resolves[0] = texture_ptr->resolve_attachment,
        .depth_stencil = texture_ptr->depth_attachment,
    };
    if (width != NULL) *width = texture_ptr->width;
    if (height != NULL) *height = texture_ptr->height;
    return true;
}

bool wgr_texture_is_flipped(wgr_handle_t handle)
{
    uint16_t index = 0;
    return wgr_handle_pool_resolve(&wgr_texture_pool, handle, &index) && wgr_textures[index].target &&
           !sg_query_features().origin_top_left;
}

void wgr_texture_set_drawing_into(wgr_handle_t handle)
{
    wgr_texture_drawing_into = handle;
}

/* An optional subsystem: part of the runtime when a program uses it (internal/wgr_module.h). */
static wgr_module_t wgr_texture_module = {.name = "texture", .order = 10, .init = wgr_texture_init, .deinit = wgr_texture_deinit};
WGR_MODULE(wgr_texture_module)
