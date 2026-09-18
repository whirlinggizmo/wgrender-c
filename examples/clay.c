/* libsk clay example — in-game UI laid out by Clay (github.com/nicbarker/clay),
 * drawn by libsk.
 *
 * Clay lays out UI and reports what to draw; it draws nothing itself. The glue below
 * turns its render commands into libsk's public drawing calls — no sokol, no second
 * copy of the fonts, no input of its own — which is all a layout library needs from
 * libsk (docs/PLAN-ui.md):
 *   - rounded rectangles and borders (sk_shape2d_draw_rounded_rectangle / _border),
 *   - text measured and drawn as slices of Clay's strings (sk_text_measure_n /
 *     sk_text_draw_n), crisp on high-DPI screens,
 *   - scroll areas clipped on a nesting clip stack (sk_render_push_clip / pop_clip),
 *   - images from texture handles (sk_texture_draw_ex),
 *   - pointer capture (sk_input_set_pointer_captured), so game input behind the UI
 *     leaves UI clicks alone.
 *
 * The UI is Clay's own demo layout (deps/clay/clay-video-demo.c): a header with a
 * File dropdown (hover it), a sidebar that switches documents, and a scrolling
 * document of wrapped text (mouse wheel, or drag). To the right of it is a strip of
 * "game": clicking there drops a marker, and only there — a press on the UI, even one
 * dragged into the strip, is captured by the UI. ESC quits. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "clay-video-demo.c" /* Clay's demo layout, unchanged apart from its include */

#include "sk.h"

#define GAME_WIDTH 240.0f /* the strip of "game" to the right of the UI */
#define SCROLL_SPEED 40.0f /* layout pixels per wheel notch */
#define MAX_MARKERS 64

/* What a Clay image element carries in its imageData: a texture and the region of it
 * to show (width or height <= 0: all of it). The demo layout has no images. */
typedef struct {
    sk_handle_t texture;
    float source_x, source_y, source_width, source_height;
} clay_image_t;

static struct {
    ClayVideoDemo_Data demo;
    sk_handle_t fonts[1]; /* Clay font id -> libsk font; 0 = the default font */
    bool press_on_ui;     /* the held press started over the UI */
    float markers[MAX_MARKERS][2];
    int marker_count;
    sk_color_t game_bg, marker, hint;
} g;

/* ---------------------------------------------------------------- glue ---- */

static sk_handle_t font_for(uint16_t font_id)
{
    return font_id < sizeof(g.fonts) / sizeof(g.fonts[0]) ? g.fonts[font_id] : 0;
}

static Clay_Dimensions measure_text(Clay_StringSlice text, Clay_TextElementConfig *config, void *user)
{
    const vec2_t size = sk_text_measure_n(font_for(config->fontId), text.chars, text.length, (float)config->fontSize);
    (void)user;
    return (Clay_Dimensions){size.x, size.y};
}

static void on_clay_error(Clay_ErrorData error)
{
    sk_logger_error("clay: %.*s", error.errorText.length, error.errorText.chars);
}

static sk_color_t color_of(Clay_Color c)
{
    return sk_color_rgba((int)c.r, (int)c.g, (int)c.b, (int)c.a);
}

static void render(Clay_RenderCommandArray commands)
{
    for (int32_t i = 0; i < commands.length; i++) {
        const Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(&commands, i);
        const Clay_BoundingBox b = cmd->boundingBox;
        switch (cmd->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                const Clay_RectangleRenderData *r = &cmd->renderData.rectangle;
                sk_shape2d_draw_rounded_rectangle(b.x, b.y, b.width, b.height, r->cornerRadius.topLeft,
                                                  r->cornerRadius.topRight, r->cornerRadius.bottomRight,
                                                  r->cornerRadius.bottomLeft, color_of(r->backgroundColor));
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                const Clay_BorderRenderData *r = &cmd->renderData.border;
                sk_shape2d_draw_border(b.x, b.y, b.width, b.height, r->width.left, r->width.top, r->width.right,
                                       r->width.bottom, r->cornerRadius.topLeft, r->cornerRadius.topRight,
                                       r->cornerRadius.bottomRight, r->cornerRadius.bottomLeft, color_of(r->color));
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                const Clay_TextRenderData *t = &cmd->renderData.text;
                sk_text_draw_n(font_for(t->fontId), t->stringContents.chars, t->stringContents.length, b.x, b.y,
                               (float)t->fontSize, color_of(t->textColor));
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_IMAGE: {
                const clay_image_t *image = (const clay_image_t *)cmd->renderData.image.imageData;
                if (image != NULL) {
                    sk_texture_draw_ex(image->texture, image->source_x, image->source_y, image->source_width,
                                       image->source_height, b.x, b.y, b.width, b.height,
                                       color_of(cmd->renderData.image.backgroundColor));
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: /* scroll areas; nested ones intersect */
                sk_render_push_clip(b.x, b.y, b.width, b.height);
                break;
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
                sk_render_pop_clip();
                break;
            default: /* custom elements: none in the demo */
                break;
        }
    }
}

/* Clay does its own hit-testing; tell libsk when the UI has the pointer, so game code
 * that asks sk_input_is_pointer_captured() leaves it alone. As with scene
 * interaction, a press that starts on the UI stays captured until it's released, even
 * when it's dragged off the UI. */
static void update_pointer_capture(const sk_mouse_state_t *mouse)
{
    const Clay_ElementIdArray over = Clay_GetPointerOverIds();
    const bool over_ui = over.length > 0;
    if (mouse->left == SK_BUTTON_PRESSED) {
        g.press_on_ui = over_ui;
    } else if (mouse->left == SK_BUTTON_UP) {
        g.press_on_ui = false;
    }
    sk_input_set_pointer_captured(over_ui || g.press_on_ui);
}

/* ------------------------------------------------------------- example ---- */

static void init(void *user_data)
{
    const vec2_t screen = sk_window_get_screen_size();
    const uint32_t memory = Clay_MinMemorySize();
    (void)user_data;
    Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(memory, malloc(memory)),
                    (Clay_Dimensions){screen.x - GAME_WIDTH, screen.y}, (Clay_ErrorHandler){on_clay_error, NULL});
    Clay_SetMeasureTextFunction(measure_text, NULL);
    g.demo = ClayVideoDemo_Initialize();
    g.game_bg = sk_color_rgba(24, 30, 40, 255);
    g.marker = sk_color_rgba(240, 190, 90, 255);
    g.hint = sk_color_rgba(140, 150, 170, 255);
}

static void frame(float dt, float tick_fraction, void *user_data)
{
    const sk_keyboard_state_t kb = sk_input_get_keyboard_state();
    const sk_mouse_state_t mouse = sk_input_get_mouse_state();
    const vec2_t screen = sk_window_get_screen_size();
    const float ui_width = screen.x - GAME_WIDTH;
    char line[64];
    Clay_RenderCommandArray commands;
    (void)tick_fraction;
    (void)user_data;

    if (kb.keys[SK_KEY_ESCAPE] == SK_BUTTON_PRESSED) sk_request_quit();

    /* the UI: size, pointer and scrolling in, capture out, then its layout */
    Clay_SetLayoutDimensions((Clay_Dimensions){ui_width, screen.y});
    Clay_SetPointerState((Clay_Vector2){(float)mouse.x, (float)mouse.y}, mouse.left == SK_BUTTON_PRESSED ||
                                                                              mouse.left == SK_BUTTON_DOWN);
    Clay_UpdateScrollContainers(true, (Clay_Vector2){mouse.wheel_x * SCROLL_SPEED, mouse.wheel * SCROLL_SPEED}, dt);
    update_pointer_capture(&mouse);
    commands = ClayVideoDemo_CreateLayout(&g.demo);

    /* the game: it only takes clicks the UI didn't */
    if (mouse.left == SK_BUTTON_PRESSED && !sk_input_is_pointer_captured() && (float)mouse.x >= ui_width &&
        g.marker_count < MAX_MARKERS) {
        g.markers[g.marker_count][0] = (float)mouse.x;
        g.markers[g.marker_count][1] = (float)mouse.y;
        g.marker_count++;
    }

    sk_render_begin();
    sk_render_clear_background(SK_COLOR_BLACK);
    sk_shape2d_draw_rectangle(ui_width, 0, GAME_WIDTH, screen.y, g.game_bg);
    for (int i = 0; i < g.marker_count; i++) {
        sk_shape2d_draw_circle(g.markers[i][0], g.markers[i][1], 6, g.marker);
    }
    sk_text_draw_ex(0, "game: click to drop a marker", ui_width + 16, 16, 14, g.hint);
    snprintf(line, sizeof(line), "markers: %d", g.marker_count);
    sk_text_draw_ex(0, line, ui_width + 16, 36, 14, g.hint);
    snprintf(line, sizeof(line), "pointer captured: %s", sk_input_is_pointer_captured() ? "yes" : "no");
    sk_text_draw_ex(0, line, ui_width + 16, 56, 14, g.hint);
    render(commands);
    sk_render_end();
}

int main(void)
{
    sk_init_values(1180, 720, "libsk clay", SK_WINDOW_FLAG_MSAA_4X_HINT | SK_WINDOW_FLAG_WINDOW_HIGHDPI);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
