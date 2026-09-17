#include "sk_texture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_handle_pool.h"
#include "internal/sk_texture.h"
#include "sk_logger.h"

#include "sokol_gfx.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_HDR /* environment maps (sk_environment.c) */
#define STBI_NO_STDIO /* we feed bytes; sync path reads the file itself */
#include "stb_image.h"

#define MAX_TEXTURES 1024
#define SK_TEXTURE_BUILTIN_COUNT 2 /* index 1 = default white, 2 = missing-texture checker */
#define CHECKER_SIZE 64
#define CHECKER_SQUARE 8

/* Texture resource: shared, refcounted, deduped GPU image (+ optional CPU alpha
 * mask for picking, generated lazily). Many Sprite objects may reference one
 * Texture. */
typedef struct {
    sg_image image; /* sampled image (a target's resolve image when multisampled) */
    sg_view view;
    sg_sampler sampler;
    /* render targets (sk_texture_create_target) */
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
} sk_texture_t;

static sk_texture_t sk_textures[MAX_TEXTURES];
static sk_handle_pool_t sk_texture_pool;
static uint16_t sk_texture_free_indices[MAX_TEXTURES];
static uint16_t sk_texture_generations[MAX_TEXTURES];
static unsigned char sk_texture_occupied[MAX_TEXTURES];
static sg_sampler sk_default_sampler;
static sg_sampler sk_texture_samplers[3][3][2]; /* [wrap_u][wrap_v][filter], made on first use */
static sk_handle_t sk_texture_drawing_into;       /* target being drawn into (render pass), 0 = screen */
static bool sk_texture_self_use_logged;

static const sk_handle_t SK_TEXTURE_DEFAULT = SK_HANDLE_MAKE(SK_HANDLE_KIND_TEXTURE, 1, 1);
static const sk_handle_t SK_TEXTURE_CHECKER = SK_HANDLE_MAKE(SK_HANDLE_KIND_TEXTURE, 2, 1);
static sk_handle_t sk_texture_placeholder; /* referenced unless built in */

static sk_texture_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_texture_pool, handle, &index)) {
        if (handle != 0) {
            log_warn("Invalid texture handle (%u)", (unsigned int)handle);
        }
        return NULL;
    }
    return &sk_textures[index];
}

static bool is_builtin_texture(uint16_t index)
{
    return index <= SK_TEXTURE_BUILTIN_COUNT;
}

static void free_texture_data(sk_texture_t *texture_ptr)
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

static void extract_alpha_mask(sk_texture_t *texture_ptr, const unsigned char *rgba)
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

static sk_handle_t find_texture_by_path(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    for (uint16_t i = SK_TEXTURE_BUILTIN_COUNT + 1; i < MAX_TEXTURES; i++) {
        if (sk_texture_occupied[i] && sk_textures[i].has_path &&
            strcmp(sk_textures[i].path, path) == 0) {
            return sk_handle_pool_handle_from_index(&sk_texture_pool, i);
        }
    }
    return 0;
}

static sk_handle_t alloc_texture_slot(sk_texture_t *out)
{
    sk_handle_t handle;
    uint16_t index = 0;

    handle = sk_handle_pool_alloc(&sk_texture_pool);
    if (handle == 0) {
        log_error("MAX_TEXTURES reached (%d)", MAX_TEXTURES);
        return 0;
    }
    sk_handle_pool_resolve(&sk_texture_pool, handle, &index);
    *out = (sk_texture_t){0};
    out->sampler = sk_default_sampler;
    sk_textures[index] = *out;
    return handle;
}

/* Make an RGBA8 image with a full mipmap chain. Each level averages 2x2 texels
 * of the previous one (the last row or column repeats for odd sizes), in the
 * stored color space. */
static sg_image make_mipmapped_image(const unsigned char *rgba, int w, int h)
{
    sg_image_desc desc = {.width = w, .height = h, .pixel_format = SG_PIXELFORMAT_RGBA8};
    unsigned char *levels[SG_MAX_MIPMAPS] = {NULL};
    int lw = w, lh = h, count = 1;
    sg_image image;

    desc.data.mip_levels[0] = (sg_range){.ptr = rgba, .size = (size_t)(w * h * 4)};
    while ((lw > 1 || lh > 1) && count < SG_MAX_MIPMAPS) {
        const unsigned char *src = count == 1 ? rgba : levels[count - 1];
        const int sw = lw, sh = lh;
        unsigned char *dst;
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
        dst = (unsigned char *)malloc((size_t)(lw * lh * 4));
        if (dst == NULL) {
            break;
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
        levels[count] = dst;
        desc.data.mip_levels[count] = (sg_range){.ptr = dst, .size = (size_t)(lw * lh * 4)};
        count++;
    }
    desc.num_mipmaps = count;
    image = sg_make_image(&desc);
    for (int i = 1; i < count; i++) {
        free(levels[i]);
    }
    return image;
}

static sk_handle_t create_texture_from_rgba(const unsigned char *rgba, int w, int h,
                                            const char *path)
{
    sk_handle_t handle;
    uint16_t index = 0;
    sk_texture_t t = {0};

    if (rgba == NULL || w <= 0 || h <= 0) {
        return 0;
    }
    handle = alloc_texture_slot(&t);
    if (handle == 0) {
        return 0;
    }
    sk_handle_pool_resolve(&sk_texture_pool, handle, &index);

    t.width = w;
    t.height = h;
    t.sampler = sk_default_sampler;
    t.ref_count = 0;
    if (path != NULL && path[0] != '\0') {
        size_t n = strlen(path);
        if (n >= sizeof(t.path)) {
            n = sizeof(t.path) - 1;
        }
        memcpy(t.path, path, n);
        t.path[n] = '\0';
        t.has_path = true;
    }
    t.image = make_mipmapped_image(rgba, w, h);
    t.view = sg_make_view(&(sg_view_desc){.texture.image = t.image});
    sk_textures[index] = t;
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

static sk_handle_t create_texture_from_memory(const unsigned char *data, int size,
                                              const char *path)
{
    int w = 0, h = 0, comp = 0;
    stbi_uc *pixels;
    sk_handle_t handle;

    if (data == NULL || size <= 0) {
        return 0;
    }
    pixels = stbi_load_from_memory(data, size, &w, &h, &comp, 4);
    if (pixels == NULL) {
        log_error("Failed to decode image (%s)", stbi_failure_reason());
        return 0;
    }
    handle = create_texture_from_rgba(pixels, w, h, path);
    stbi_image_free(pixels);
    return handle;
}

sk_handle_t sk_texture_create_rgba(const unsigned char *rgba, int width, int height)
{
    sk_handle_t handle = create_texture_from_rgba(rgba, width, height, NULL);
    sk_texture_t *texture_ptr = resolve(handle);
    bool translucent = false;

    if (texture_ptr == NULL) {
        return 0;
    }
    for (int i = 0; i < width * height && !translucent; i++) {
        translucent = rgba[i * 4 + 3] != 255;
    }
    if (translucent) {
        extract_alpha_mask(texture_ptr, rgba); /* no source to re-read later */
    }
    texture_ptr->ref_count = 1;
    return handle;
}

bool sk_texture_get_alpha_mask(sk_handle_t handle, const unsigned char **alpha, int *width, int *height)
{
    sk_texture_t *texture_ptr = resolve(handle);
    if (texture_ptr == NULL) {
        return false;
    }
    if (texture_ptr->alpha == NULL && (!texture_ptr->has_path || !sk_texture_ensure_alpha_mask(handle))) {
        return false;
    }
    *alpha = texture_ptr->alpha;
    *width = texture_ptr->width;
    *height = texture_ptr->height;
    return true;
}

void sk_texture_retain(sk_handle_t handle)
{
    sk_texture_t *texture_ptr = resolve(handle);
    if (texture_ptr != NULL) {
        texture_ptr->ref_count++;
    }
}

void sk_texture_release(sk_handle_t handle)
{
    uint16_t index = 0;
    sk_texture_t *texture_ptr;

    if (!sk_handle_pool_resolve(&sk_texture_pool, handle, &index)) {
        return;
    }
    if (is_builtin_texture(index)) {
        return;
    }
    texture_ptr = &sk_textures[index];
    if (texture_ptr->ref_count > 0) {
        texture_ptr->ref_count--;
    }
    if (texture_ptr->ref_count == 0) {
        free_texture_data(texture_ptr);
        memset(texture_ptr, 0, sizeof(*texture_ptr));
        sk_handle_pool_free(&sk_texture_pool, handle);
    }
}

bool sk_texture_ensure_alpha_mask(sk_handle_t handle)
{
    sk_texture_t *texture_ptr = resolve(handle);
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
    bytes = read_file_bytes(texture_ptr->path, &size);
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

SK_KEEP
sk_handle_t sk_texture_get_default(void)
{
    return SK_TEXTURE_DEFAULT;
}

static sg_wrap to_sg_wrap(sk_texture_wrap_t wrap)
{
    switch (wrap) {
        case SK_TEXTURE_WRAP_REPEAT: return SG_WRAP_REPEAT;
        case SK_TEXTURE_WRAP_MIRROR: return SG_WRAP_MIRRORED_REPEAT;
        default: return SG_WRAP_CLAMP_TO_EDGE;
    }
}

SK_KEEP
bool sk_texture_set_sampling(sk_handle_t texture, sk_texture_wrap_t wrap_u, sk_texture_wrap_t wrap_v,
                             sk_texture_filter_t filter)
{
    sk_texture_t *texture_ptr = resolve(texture);
    sg_sampler *smp;

    if (texture_ptr == NULL) {
        return false;
    }
    if (wrap_u < SK_TEXTURE_WRAP_REPEAT || wrap_u > SK_TEXTURE_WRAP_MIRROR || wrap_v < SK_TEXTURE_WRAP_REPEAT ||
        wrap_v > SK_TEXTURE_WRAP_MIRROR || filter < SK_TEXTURE_FILTER_LINEAR || filter > SK_TEXTURE_FILTER_NEAREST) {
        log_warn("sk_texture_set_sampling: invalid wrap or filter");
        return false;
    }
    smp = &sk_texture_samplers[wrap_u][wrap_v][filter];
    if (smp->id == SG_INVALID_ID) {
        const sg_filter f = filter == SK_TEXTURE_FILTER_NEAREST ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;
        *smp = sg_make_sampler(&(sg_sampler_desc){
            .min_filter = f,
            .mag_filter = f,
            .mipmap_filter = f,
            .wrap_u = to_sg_wrap(wrap_u),
            .wrap_v = to_sg_wrap(wrap_v),
        });
    }
    texture_ptr->sampler = *smp;
    return true;
}

SK_KEEP
sk_handle_t sk_texture_create_target(int width, int height)
{
    const sg_environment_defaults env = sg_query_desc().environment.defaults;
    const int samples = env.sample_count > 1 ? env.sample_count : 1;
    const sg_pixel_format color_format = env.color_format != _SG_PIXELFORMAT_DEFAULT ? env.color_format
                                                                                     : SG_PIXELFORMAT_RGBA8;
    const sg_pixel_format depth_format = env.depth_format != _SG_PIXELFORMAT_DEFAULT ? env.depth_format
                                                                                     : SG_PIXELFORMAT_DEPTH_STENCIL;
    sk_texture_t t = {0};
    sk_handle_t handle;
    uint16_t index = 0;

    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        log_error("sk_texture_create_target: invalid size %dx%d", width, height);
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
    t.sampler = sk_default_sampler;
    /* same format and MSAA as the screen, so every screen pipeline works in a target */
    t.image = sg_make_image(&(sg_image_desc){
        .usage = {.color_attachment = samples == 1, .resolve_attachment = samples > 1},
        .width = width,
        .height = height,
        .pixel_format = color_format,
        .sample_count = 1,
        .label = "sk-target-color",
    });
    if (samples > 1) {
        t.msaa_image = sg_make_image(&(sg_image_desc){
            .usage.color_attachment = true,
            .width = width,
            .height = height,
            .pixel_format = color_format,
            .sample_count = samples,
            .label = "sk-target-msaa",
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
        .label = "sk-target-depth",
    });
    t.depth_attachment = sg_make_view(&(sg_view_desc){.depth_stencil_attachment.image = t.depth_image});
    t.view = sg_make_view(&(sg_view_desc){.texture.image = t.image});
    sk_handle_pool_resolve(&sk_texture_pool, handle, &index);
    sk_textures[index] = t;
    return handle;
}

SK_KEEP
sk_handle_t sk_texture_get_placeholder(void)
{
    return sk_texture_placeholder;
}

SK_KEEP
bool sk_texture_set_placeholder(sk_handle_t texture)
{
    if (texture == 0) {
        texture = SK_TEXTURE_CHECKER;
    } else if (resolve(texture) == NULL) {
        return false;
    }
    if (texture != sk_texture_placeholder) {
        sk_texture_retain(texture); /* before releasing, in case they're the same resource */
        sk_texture_release(sk_texture_placeholder);
        sk_texture_placeholder = texture;
    }
    return true;
}

SK_KEEP
sk_handle_t sk_texture_create(const char *path)
{
    sk_handle_t tex = find_texture_by_path(path);
    unsigned char *bytes;
    int size = 0;

    if (tex != 0) {
        sk_texture_retain(tex);
        return tex;
    }
    bytes = read_file_bytes(path, &size);
    if (bytes == NULL) {
        return 0;
    }
    tex = create_texture_from_memory(bytes, size, path);
    free(bytes);
    if (tex == 0) {
        return 0;
    }
    sk_texture_retain(tex);
    return tex;
}

SK_KEEP
vec2_t sk_texture_get_size(sk_handle_t handle)
{
    sk_texture_t *texture_ptr = resolve(handle);
    if (texture_ptr == NULL) {
        return (vec2_t){0.0f, 0.0f};
    }
    return (vec2_t){(float)texture_ptr->width, (float)texture_ptr->height};
}

SK_KEEP
void sk_texture_destroy(sk_handle_t handle)
{
    uint16_t index = 0;

    if (!sk_handle_pool_resolve(&sk_texture_pool, handle, &index)) {
        return;
    }
    if (is_builtin_texture(index)) {
        log_error("Cannot destroy built-in texture (%u)", (unsigned int)handle);
        return;
    }
    sk_texture_release(handle);
}

bool sk_texture_sample_alpha(sk_handle_t handle, float u, float v, float *out_alpha)
{
    sk_texture_t *texture_ptr = resolve(handle);
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

bool sk_texture_get_binding(sk_handle_t handle, sg_view *view, sg_sampler *smp,
                            int *width, int *height)
{
    sk_texture_t *texture_ptr = resolve(handle);
    if (handle != 0 && handle == sk_texture_drawing_into) {
        /* a pass can't sample the texture it renders into */
        if (!sk_texture_self_use_logged) {
            log_warn("texture: a render target can't be drawn into itself; the default texture is used");
            sk_texture_self_use_logged = true;
        }
        texture_ptr = NULL;
    }
    if (texture_ptr == NULL) {
        texture_ptr = resolve(SK_TEXTURE_DEFAULT);
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

void sk_texture_init(void)
{
    static const unsigned char white[4] = {255, 255, 255, 255};
    uint16_t index = 0;

    memset(sk_textures, 0, sizeof(sk_textures));
    sk_handle_pool_init(&sk_texture_pool,
                        SK_HANDLE_KIND_TEXTURE,
                        MAX_TEXTURES,
                        sk_texture_free_indices,
                        MAX_TEXTURES,
                        sk_texture_generations,
                        sk_texture_occupied);

    sk_default_sampler = sg_make_sampler(&(sg_sampler_desc){
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        .mipmap_filter = SG_FILTER_LINEAR,
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    });
    sk_texture_samplers[SK_TEXTURE_WRAP_CLAMP][SK_TEXTURE_WRAP_CLAMP][SK_TEXTURE_FILTER_LINEAR] = sk_default_sampler;
    sk_texture_drawing_into = 0;
    sk_texture_self_use_logged = false;

    /* reserve + populate the built-in 1x1 white default at index 1 */
    sk_texture_generations[1] = 1;
    sk_texture_occupied[1] = 1;
    sk_texture_pool.next_index = SK_TEXTURE_BUILTIN_COUNT + 1;
    sk_handle_pool_resolve(&sk_texture_pool, SK_TEXTURE_DEFAULT, &index);
    sk_textures[index].width = 1;
    sk_textures[index].height = 1;
    sk_textures[index].image = sg_make_image(&(sg_image_desc){
        .width = 1,
        .height = 1,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0] = {.ptr = white, .size = sizeof(white)},
    });
    sk_textures[index].view = sg_make_view(&(sg_view_desc){.texture.image = sk_textures[index].image});
    sk_textures[index].sampler = sk_default_sampler;

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
        sk_texture_generations[2] = 1;
        sk_texture_occupied[2] = 1;
        sk_handle_pool_resolve(&sk_texture_pool, SK_TEXTURE_CHECKER, &index);
        sk_textures[index].width = CHECKER_SIZE;
        sk_textures[index].height = CHECKER_SIZE;
        sk_textures[index].image = make_mipmapped_image(checker, CHECKER_SIZE, CHECKER_SIZE);
        sk_textures[index].view = sg_make_view(&(sg_view_desc){.texture.image = sk_textures[index].image});
        sk_textures[index].sampler = sk_default_sampler;
    }
    sk_texture_placeholder = SK_TEXTURE_CHECKER;
}

void sk_texture_deinit(void)
{
    for (uint16_t i = 1; i < MAX_TEXTURES; i++) {
        if (sk_texture_occupied[i]) {
            free_texture_data(&sk_textures[i]);
            sk_textures[i] = (sk_texture_t){0};
        }
    }
    for (int i = 0; i < 3 * 3 * 2; i++) {
        sg_sampler *smp = &((sg_sampler *)sk_texture_samplers)[i];
        if (smp->id != SG_INVALID_ID) sg_destroy_sampler(*smp); /* includes the default */
        *smp = (sg_sampler){0};
    }
    sk_default_sampler = (sg_sampler){0};
    sk_texture_placeholder = 0;
    sk_handle_pool_reset(&sk_texture_pool);
}

bool sk_texture_get_target(sk_handle_t handle, sg_attachments *attachments, int *width, int *height)
{
    sk_texture_t *texture_ptr = resolve(handle);
    if (texture_ptr == NULL || !texture_ptr->target) {
        return false;
    }
    *attachments = (sg_attachments){
        .colors[0] = texture_ptr->color_attachment,
        .resolves[0] = texture_ptr->resolve_attachment,
        .depth_stencil = texture_ptr->depth_attachment,
    };
    *width = texture_ptr->width;
    *height = texture_ptr->height;
    return true;
}

bool sk_texture_is_flipped(sk_handle_t handle)
{
    uint16_t index = 0;
    return sk_handle_pool_resolve(&sk_texture_pool, handle, &index) && sk_textures[index].target &&
           !sg_query_features().origin_top_left;
}

void sk_texture_set_drawing_into(sk_handle_t handle)
{
    sk_texture_drawing_into = handle;
}
