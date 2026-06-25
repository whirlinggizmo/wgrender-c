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
#define STBI_NO_STDIO /* we feed bytes; sync path reads the file itself */
#include "stb_image.h"

#define MAX_TEXTURES 1024
#define SK_TEXTURE_BUILTIN_COUNT 1 /* index 1 = default white */

typedef struct {
    sg_image image;
    sg_view view;
    sg_sampler sampler;
    int width;
    int height;
    unsigned char *alpha; /* optional CPU alpha mask (w*h bytes) for picking */
} sk_texture_data_t;

static sk_texture_data_t sk_textures[MAX_TEXTURES];
static sk_handle_pool_t sk_texture_pool;
static uint16_t sk_texture_free_indices[MAX_TEXTURES];
static uint16_t sk_texture_generations[MAX_TEXTURES];
static unsigned char sk_texture_occupied[MAX_TEXTURES];
static sg_sampler sk_default_sampler;

static const sk_handle_t SK_TEXTURE_DEFAULT = SK_HANDLE_MAKE(SK_HANDLE_KIND_TEXTURE, 1, 1);

static sk_texture_data_t *resolve(sk_handle_t handle)
{
    uint16_t index = 0;
    if (!sk_handle_pool_resolve(&sk_texture_pool, handle, &index)) {
        return NULL;
    }
    return &sk_textures[index];
}

static sk_handle_t make_texture(const unsigned char *rgba, int w, int h, bool keep_alpha)
{
    sk_handle_t handle;
    uint16_t index = 0;
    sk_texture_data_t *t;

    handle = sk_handle_pool_alloc(&sk_texture_pool);
    if (handle == 0) {
        log_error("MAX_TEXTURES reached (%d)", MAX_TEXTURES);
        return 0;
    }
    sk_handle_pool_resolve(&sk_texture_pool, handle, &index);
    t = &sk_textures[index];

    t->width = w;
    t->height = h;
    if (keep_alpha && rgba != NULL && w > 0 && h > 0) {
        t->alpha = (unsigned char *)malloc((size_t)(w * h));
        if (t->alpha != NULL) {
            for (int i = 0; i < w * h; i++) {
                t->alpha[i] = rgba[i * 4 + 3];
            }
        }
    }
    t->image = sg_make_image(&(sg_image_desc){
        .width = w,
        .height = h,
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        .data.mip_levels[0] = {.ptr = rgba, .size = (size_t)(w * h * 4)},
    });
    t->view = sg_make_view(&(sg_view_desc){.texture.image = t->image});
    t->sampler = sk_default_sampler;
    return handle;
}

SK_KEEP
sk_handle_t sk_texture_get_default(void)
{
    return SK_TEXTURE_DEFAULT;
}

static sk_handle_t create_from_memory(const unsigned char *data, int size, bool keep_alpha)
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
    handle = make_texture(pixels, w, h, keep_alpha);
    stbi_image_free(pixels);
    return handle;
}

static sk_handle_t create_from_file(const char *path, bool keep_alpha);

SK_KEEP
sk_handle_t sk_texture_create_from_memory(const unsigned char *data, int size)
{
    return create_from_memory(data, size, false);
}

SK_KEEP
sk_handle_t sk_texture_create_from_memory_pickable(const unsigned char *data, int size)
{
    return create_from_memory(data, size, true);
}

SK_KEEP
sk_handle_t sk_texture_create_pickable(const char *path)
{
    return create_from_file(path, true);
}

SK_KEEP
sk_handle_t sk_texture_create(const char *path)
{
    return create_from_file(path, false);
}

static sk_handle_t create_from_file(const char *path, bool keep_alpha)
{
    FILE *f;
    long size;
    unsigned char *bytes;
    sk_handle_t handle;
    size_t read;

    if (path == NULL) {
        return 0;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        log_error("Failed to open texture: %s", path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return 0;
    }
    bytes = (unsigned char *)malloc((size_t)size);
    if (bytes == NULL) {
        fclose(f);
        return 0;
    }
    read = fread(bytes, 1, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size) {
        free(bytes);
        return 0;
    }
    handle = create_from_memory(bytes, (int)size, keep_alpha);
    free(bytes);
    return handle;
}

SK_KEEP
vec2_t sk_texture_get_size(sk_handle_t handle)
{
    sk_texture_data_t *t = resolve(handle);
    if (t == NULL) {
        return (vec2_t){0.0f, 0.0f};
    }
    return (vec2_t){(float)t->width, (float)t->height};
}

SK_KEEP
void sk_texture_destroy(sk_handle_t handle)
{
    uint16_t index = 0;
    sk_texture_data_t *t;

    if (!sk_handle_pool_resolve(&sk_texture_pool, handle, &index)) {
        return;
    }
    if (index <= SK_TEXTURE_BUILTIN_COUNT) {
        log_error("Cannot destroy built-in texture (%u)", (unsigned int)handle);
        return;
    }
    t = &sk_textures[index];
    sg_destroy_view(t->view);
    sg_destroy_image(t->image);
    free(t->alpha);
    *t = (sk_texture_data_t){0};
    sk_handle_pool_free(&sk_texture_pool, handle);
}

bool sk_texture_sample_alpha(sk_handle_t handle, float u, float v, float *out_alpha)
{
    sk_texture_data_t *t = resolve(handle);
    int px, py;

    if (t == NULL || t->alpha == NULL || t->width <= 0 || t->height <= 0) {
        return false;
    }
    if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
    if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
    px = (int)(u * (float)(t->width - 1) + 0.5f);
    py = (int)(v * (float)(t->height - 1) + 0.5f);
    if (out_alpha != NULL) {
        *out_alpha = (float)t->alpha[py * t->width + px] / 255.0f;
    }
    return true;
}

bool sk_texture_get_binding(sk_handle_t handle, sg_view *view, sg_sampler *smp,
                            int *width, int *height)
{
    sk_texture_data_t *t = resolve(handle);
    if (t == NULL) {
        t = resolve(SK_TEXTURE_DEFAULT);
        if (t == NULL) {
            return false;
        }
    }
    if (view) *view = t->view;
    if (smp) *smp = t->sampler;
    if (width) *width = t->width;
    if (height) *height = t->height;
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
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    });

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
}

void sk_texture_deinit(void)
{
    for (uint16_t i = 1; i < MAX_TEXTURES; i++) {
        if (sk_texture_occupied[i]) {
            sg_destroy_view(sk_textures[i].view);
            sg_destroy_image(sk_textures[i].image);
            free(sk_textures[i].alpha);
            sk_textures[i] = (sk_texture_data_t){0};
        }
    }
    sg_destroy_sampler(sk_default_sampler);
    sk_handle_pool_reset(&sk_texture_pool);
}
