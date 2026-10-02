/* libwgrender textures example — compressed textures (docs/HISTORY.md, "compressed textures").
 *
 * Each texture twice: loaded from its PNG (left), and as "name.ktx" (right), for which
 * libwgrender picks the file this GPU can use (name.bc7.ktx on desktops, name.astc.ktx on
 * phones, name.etc2.ktx on older phones, else name.png), made beforehand by
 * tools/compress_textures.py. Under each: the file that was loaded, what it takes in
 * GPU memory (with mipmaps) and how long it took from asking to having it. A texture
 * loads on create; once it's READY, wgr_resource_get_path says which file it was read
 * from. */
#include <stdio.h>
#include <string.h>

#include "wgr.h"
#include "shared/example_assets.h"

enum { TEXTURES = 2, KINDS = 2 }; /* kind 0: the PNG, 1: the .ktx */
static const char *NAMES[TEXTURES] = {"sprites/logo/wg-logo-bw-alpha", "textures/flame"};

typedef struct {
    wgr_handle_t texture, sprite;
    char loaded[160]; /* the file it was read from, once it's READY */
    double asked, took; /* took: 0 until the texture is READY */
    int width, height;
} slot_t;

static slot_t g_slots[TEXTURES][KINDS];

static void init(void *user_data)
{
    char path[160];
    (void)user_data;
    wgr_asset_set_host(EXAMPLE_ASSET_BASE);
    wgr_asset_set_manifest(EXAMPLE_ASSET_MANIFEST);
    for (int t = 0; t < TEXTURES; t++) {
        for (int k = 0; k < KINDS; k++) {
            slot_t *slot = &g_slots[t][k];
            snprintf(path, sizeof(path), "%s.%s", NAMES[t], k == 0 ? "png" : "ktx");
            slot->asked = wgr_get_time();
            slot->texture = wgr_texture_create(path); /* kept to read its status and size */
            slot->sprite = wgr_sprite2d_create(slot->texture);
            wgr_sprite2d_set_size(slot->sprite, 220.0f, 220.0f);
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
            slot_t *slot = &g_slots[t][k];
            const float x = 20.0f + (float)(t * KINDS + k) * 245.0f, y = 80.0f;
            if (slot->took == 0.0 && wgr_resource_get_status(slot->texture) == WGR_RESOURCE_READY) {
                const vec2_t size = wgr_texture_get_size(slot->texture);
                slot->took = wgr_get_time() - slot->asked;
                slot->width = (int)size.x;
                slot->height = (int)size.y;
                snprintf(slot->loaded, sizeof(slot->loaded), "%s", wgr_resource_get_path(slot->texture));
            } else if (slot->loaded[0] == '\0' && wgr_resource_get_status(slot->texture) == WGR_RESOURCE_FAILED) {
                snprintf(slot->loaded, sizeof(slot->loaded), "failed: %s", NAMES[t]);
            }
            wgr_sprite2d_set_position(slot->sprite, x + 110.0f, y + 110.0f); /* the pivot: the middle */
            wgr_sprite2d_draw(slot->sprite); /* nothing until it's loaded */
            wgr_text_draw(slot->loaded[0] != '\0' ? strrchr(slot->loaded, '/') + 1 : "loading...", (int)x,
                         (int)y + 236, 16, WGR_COLOR_LIGHTGRAY);
            if (slot->took > 0.0) {
                snprintf(line, sizeof(line), "%dx%d  GPU %.0f KB  %.0f ms", slot->width, slot->height, gpu_kb(slot),
                         slot->took * 1000.0);
                wgr_text_draw(line, (int)x, (int)y + 258, 14, WGR_COLOR_LIGHTGRAY);
            }
        }
    }
    wgr_render_end_frame();

#ifndef __EMSCRIPTEN__ /* on the web Escape is the browser's (it leaves fullscreen), and a page has nothing to quit to */
    if (wgr_input_get_key(WGR_KEY_ESCAPE) == WGR_BUTTON_PRESSED) {
        wgr_request_quit();
    }
#endif
}

int main(void)
{
    wgr_init_values(1000, 380, "libwgrender textures", WGR_WINDOW_FLAG_MSAA_4X_HINT | WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
    wgr_set_init(init, NULL);
    wgr_set_frame(frame, NULL);
    return wgr_run();
}
