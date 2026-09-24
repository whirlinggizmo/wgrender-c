/* libwgrender textures example — compressed textures (docs/PLAN-textures.md).
 *
 * Each texture twice: loaded from its PNG (left), and as "name.ktx" (right), for which
 * libwgrender picks the file this GPU can use (name.bc7.ktx on desktops, name.astc.ktx on
 * phones, name.etc2.ktx on older phones, else name.png), made beforehand by
 * tools/compress_textures.py. Under each: the file that was loaded, what it takes in
 * GPU memory (with mipmaps) and how long it took from asking to having it. */
#include <stdio.h>
#include <string.h>

#include "wgr.h"
#include "example_assets.h"

enum { TEXTURES = 2, KINDS = 2 }; /* kind 0: the PNG, 1: the .ktx */
static const char *NAMES[TEXTURES] = {"sprites/logo/wg-logo-bw-alpha", "textures/flame"};

typedef struct {
    wgr_handle_t sprite;
    char loaded[160];
    double asked, took;
    int width, height;
} slot_t;

static slot_t g_slots[TEXTURES][KINDS];

static void on_loaded(const char *path, void *user)
{
    slot_t *slot = (slot_t *)user;
    const wgr_handle_t texture = wgr_texture_create(path);
    vec2_t size;
    slot->took = wgr_get_time() - slot->asked;
    snprintf(slot->loaded, sizeof(slot->loaded), "%s", path);
    if (texture == 0) {
        return;
    }
    size = wgr_texture_get_size(texture);
    slot->width = (int)size.x;
    slot->height = (int)size.y;
    slot->sprite = wgr_sprite2d_create(texture);
    wgr_texture_release(texture); /* the sprite holds its own reference */
    wgr_sprite2d_set_size(slot->sprite, 220.0f, 220.0f);
}

static void on_failed(const char *path, void *user)
{
    slot_t *slot = (slot_t *)user;
    snprintf(slot->loaded, sizeof(slot->loaded), "failed: %s", path);
}

static void init(void *user_data)
{
    char path[160];
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    for (int t = 0; t < TEXTURES; t++) {
        for (int k = 0; k < KINDS; k++) {
            slot_t *slot = &g_slots[t][k];
            snprintf(path, sizeof(path), "%s.%s", NAMES[t], k == 0 ? "png" : "ktx");
            slot->asked = wgr_get_time();
            wgr_asset_add_task(wgr_asset_ensure_async(path, NULL, 0), on_loaded, on_failed, slot);
        }
    }
}

/* GPU memory with the full mipmap chain (a third more): 4 bytes a pixel as RGBA, 1 as
 * BC7 / ASTC 4x4 / ETC2 RGBA. */
static double gpu_kb(const slot_t *slot)
{
    const bool compressed = strstr(slot->loaded, ".ktx") != NULL;
    return (double)slot->width * slot->height * (compressed ? 1.0 : 4.0) * 4.0 / 3.0 / 1024.0;
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    char line[200];
    (void)dt;
    (void)tick_fraction;
    (void)user_data;

    wgr_render_begin_frame();
    wgr_render_clear_background(wgr_color_rgba(38, 42, 54, 255));
    wgr_text_draw("libwgrender + sokol — textures: PNG (left) and compressed (right)", 12, 36, 22, WGR_COLOR_RAYWHITE);
    for (int t = 0; t < TEXTURES; t++) {
        for (int k = 0; k < KINDS; k++) {
            const slot_t *slot = &g_slots[t][k];
            const float x = 20.0f + (float)(t * KINDS + k) * 245.0f, y = 80.0f;
            if (slot->sprite != 0) {
                wgr_sprite2d_set_position(slot->sprite, x + 110.0f, y + 110.0f); /* the pivot: the middle */
                wgr_sprite2d_draw(slot->sprite);
            }
            wgr_text_draw(slot->loaded[0] != '\0' ? strrchr(slot->loaded, '/') + 1 : "loading...", (int)x,
                         (int)y + 236, 16, WGR_COLOR_LIGHTGRAY);
            if (slot->sprite != 0) {
                snprintf(line, sizeof(line), "%dx%d  GPU %.0f KB  %.0f ms", slot->width, slot->height, gpu_kb(slot),
                         slot->took * 1000.0);
                wgr_text_draw(line, (int)x, (int)y + 258, 14, WGR_COLOR_LIGHTGRAY);
            }
        }
    }
    wgr_render_end_frame();

    if (wgr_input_get_key(WGR_KEY_ESCAPE) == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
}

int main(void)
{
    wgr_init_values(1000, 380, "libwgrender textures", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
