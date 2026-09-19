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
 *   - scroll areas on a nesting clip stack (sk_render_push_clip / pop_clip),
 *   - images from texture handles, plain or nine-sliced (sk_texture_draw_ex /
 *     _nine_slice),
 *   - overlay colors, custom elements drawn by the game,
 *   - pointer capture (sk_input_set_pointer_captured), so game input behind the UI
 *     leaves UI clicks alone.
 *
 * Tab switches between two pages:
 *   - Clay's own demo layout (deps/clay/examples/shared-layouts/clay-video-demo.c):
 *     a header with a File dropdown, a sidebar that switches documents, a scrolling
 *     document;
 *   - "elements": the rest of what Clay emits — images (tinted, nine-sliced, with
 *     tooltips on hover, click to select), borders of every shape, a scroll area
 *     inside a scroll area, a custom element, an overlay that dims a card on hover,
 *     and an animated transition.
 * To the right of the UI is a strip of "game": clicking there drops a marker, and
 * only there — a press on the UI, even one dragged into the strip, belongs to the UI.
 * Mouse wheel or drag scrolls. ESC quits.
 *
 * deps/clay comes from libsk's Clay fork (tools/update_clay.sh), which carries one
 * fix: scrolling goes to the innermost scroll area under the pointer, which nested
 * scroll areas need. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CLAY_IMPLEMENTATION
#include "clay.h"
#include "examples/shared-layouts/clay-video-demo.c" /* Clay's demo layout, unchanged */

#include "example_assets.h"
#include "sk.h"

#define GAME_WIDTH 240.0f /* the strip of "game" to the right of the UI */
#define SCROLL_SPEED 4.0f /* times 10 (Clay's own wheel scale): 40 layout pixels per wheel notch */
#define MAX_MARKERS 64
#define MAX_OVERLAYS 8
#define TILES_PATH "textures/tiles.png"
#define PANEL_PATH "textures/ui_panel.png"

/* ---------------------------------------------------------------- glue ---- */

/* What a Clay image element carries in its imageData: a texture, the region of it to
 * show (width or height <= 0: all of it), optional nine-slice borders in texture
 * pixels, and a tint (0: none). The tint lives here rather than in the element's
 * backgroundColor because Clay also draws a background rectangle for that color,
 * after the image. */
typedef struct {
    sk_handle_t texture;
    float source[4];
    float slice[4]; /* left, top, right, bottom */
    sk_color_t tint;
} clay_image_t;

/* What a Clay custom element carries in its customData: the game draws it. */
typedef struct {
    void (*draw)(Clay_BoundingBox box, void *user);
    void *user;
} clay_custom_t;

static struct {
    sk_handle_t fonts[1];                 /* Clay font id -> libsk font; 0 = the default font */
    Clay_Color overlays[MAX_OVERLAYS];    /* active overlay colors, outermost first */
    int overlay_count;
    bool press_on_ui;                     /* the held press started over the UI */
} glue;

static sk_handle_t font_for(uint16_t font_id)
{
    return font_id < sizeof(glue.fonts) / sizeof(glue.fonts[0]) ? glue.fonts[font_id] : 0;
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

/* A Clay color with the active overlays applied, as Clay defines them:
 * mix(color, overlay.rgb, overlay.a), alpha kept. Exact for everything with one color
 * (rectangles, borders, text), and for images when the overlay darkens (a tint scales
 * the texture); brightening an image is approximated through its tint. */
static sk_color_t color_of(Clay_Color c)
{
    for (int i = 0; i < glue.overlay_count; i++) {
        const Clay_Color o = glue.overlays[i];
        const float k = o.a / 255.0f;
        c.r += (o.r - c.r) * k;
        c.g += (o.g - c.g) * k;
        c.b += (o.b - c.b) * k;
    }
    return sk_color_rgba((int)(c.r + 0.5f), (int)(c.g + 0.5f), (int)(c.b + 0.5f), (int)(c.a + 0.5f));
}

static void draw_image(Clay_BoundingBox b, const clay_image_t *image)
{
    const sk_color_t tint = image->tint != 0 ? image->tint : SK_COLOR_WHITE;
    const Clay_Color base = {(float)sk_color_get_red(tint), (float)sk_color_get_green(tint),
                             (float)sk_color_get_blue(tint), (float)sk_color_get_alpha(tint)};
    const bool sliced = image->slice[0] > 0 || image->slice[1] > 0 || image->slice[2] > 0 || image->slice[3] > 0;
    if (sliced) {
        sk_texture_draw_nine_slice(image->texture, image->source[0], image->source[1], image->source[2],
                                   image->source[3], image->slice[0], image->slice[1], image->slice[2],
                                   image->slice[3], b.x, b.y, b.width, b.height, color_of(base));
    } else {
        sk_texture_draw_ex(image->texture, image->source[0], image->source[1], image->source[2], image->source[3], b.x,
                           b.y, b.width, b.height, color_of(base));
    }
}

static void render(Clay_RenderCommandArray commands)
{
    glue.overlay_count = 0;
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
            case CLAY_RENDER_COMMAND_TYPE_IMAGE:
                if (cmd->renderData.image.imageData != NULL) {
                    draw_image(b, (const clay_image_t *)cmd->renderData.image.imageData);
                }
                break;
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
                const clay_custom_t *custom = (const clay_custom_t *)cmd->renderData.custom.customData;
                if (custom != NULL && custom->draw != NULL) {
                    custom->draw(b, custom->user);
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: /* scroll areas; nested ones intersect */
                sk_render_push_clip(b.x, b.y, b.width, b.height);
                break;
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
                sk_render_pop_clip();
                break;
            case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_START:
                if (glue.overlay_count < MAX_OVERLAYS) {
                    glue.overlays[glue.overlay_count] = cmd->renderData.overlayColor.color;
                }
                glue.overlay_count++; /* counted past the limit, so ends still pair up */
                break;
            case CLAY_RENDER_COMMAND_TYPE_OVERLAY_COLOR_END:
                if (glue.overlay_count > 0) {
                    glue.overlay_count--;
                }
                break;
            default:
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
    const bool over_ui = Clay_GetPointerOverIds().length > 0;
    if (mouse->left == SK_BUTTON_PRESSED) {
        glue.press_on_ui = over_ui;
    } else if (mouse->left == SK_BUTTON_UP) {
        glue.press_on_ui = false;
    }
    sk_input_set_pointer_captured(over_ui || glue.press_on_ui);
}

/* ------------------------------------------------------- elements page ---- */

enum { TILES = 8, BORDER_BOXES = 5, CHIPS = 14, ROWS = 12 };

static const char *TILE_NAMES[TILES] = {"grass", "sand", "water", "stone", "tree", "flag", "coin", "rock (tinted)"};
static const float TILE_CELLS[TILES][4] = {{0, 0, 16, 16},  {16, 0, 16, 16},  {32, 0, 16, 16},  {48, 0, 16, 16},
                                           {0, 16, 16, 32}, {16, 16, 16, 32}, {32, 16, 16, 16}, {48, 16, 16, 16}};

static struct {
    ClayVideoDemo_Data demo;
    bool elements; /* the page shown */
    sk_handle_t tiles, panel;
    clay_image_t tile_images[TILES], panel_image;
    clay_custom_t meter;
    int selected_tile;
    bool expanded;
    float time;
    float markers[MAX_MARKERS][2];
    int marker_count;
    sk_color_t game_bg, marker, hint;
} g;

static const Clay_Color INK = {235, 238, 245, 255};
static const Clay_Color DIM_INK = {150, 158, 175, 255};
static const Clay_Color PAGE = {30, 33, 43, 255};
static const Clay_Color CARD = {45, 50, 64, 255};
static const Clay_Color ACCENT = {110, 150, 235, 255};

static void on_tile(Clay_ElementId id, Clay_PointerData pointer, void *user)
{
    (void)id;
    if (pointer.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        g.selected_tile = (int)(intptr_t)user;
    }
}

static void on_expand(Clay_ElementId id, Clay_PointerData pointer, void *user)
{
    (void)id;
    (void)user;
    if (pointer.state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME) {
        g.expanded = !g.expanded;
    }
}

/* A custom element: a level meter the game animates and draws itself. */
static void draw_meter(Clay_BoundingBox b, void *user)
{
    const float level = 0.5f + 0.45f * sinf(g.time * 1.7f);
    (void)user;
    sk_shape2d_draw_rounded_rectangle(b.x, b.y, b.width, b.height, 6, 6, 6, 6, sk_color_rgba(24, 27, 36, 255));
    sk_shape2d_draw_rounded_rectangle(b.x + 3, b.y + 3, (b.width - 6) * level, b.height - 6, 4, 4, 4, 4,
                                      sk_color_lerp(sk_color_rgba(90, 200, 130, 255), sk_color_rgba(235, 120, 90, 255),
                                                    level));
}

static void text(Clay_String string, uint16_t size, Clay_Color color)
{
    CLAY_TEXT(string, CLAY_TEXT_CONFIG({.fontId = 0, .fontSize = size, .textColor = color}));
}

/* A titled card; its content is declared inside. */
#define CARD_OPEN(id, title)                                                                                            \
    CLAY(CLAY_ID(id), {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .padding = CLAY_PADDING_ALL(12),  \
                                  .childGap = 10, .layoutDirection = CLAY_TOP_TO_BOTTOM},                                \
                       .backgroundColor = CARD, .cornerRadius = CLAY_CORNER_RADIUS(10),                                  \
                       .overlayColor = Clay_Hovered() ? (Clay_Color){0, 0, 0, 70} : (Clay_Color){0, 0, 0, 0}})        \
    for (int card_once_ = (text(CLAY_STRING(title), 16, INK), 1); card_once_; card_once_ = 0)

static void images_card(void)
{
    CARD_OPEN("Images", "Images: source rects, tint, nine-slice")
    {
        CLAY(CLAY_ID("TileRow"), {.layout = {.childGap = 8, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
            for (int i = 0; i < TILES; i++) {
                const bool tall = TILE_CELLS[i][3] > 16;
                CLAY(CLAY_IDI("Tile", i), {.layout = {.sizing = {CLAY_SIZING_FIXED(32), CLAY_SIZING_FIXED(tall ? 64 : 32)}},
                                           .image = {.imageData = &g.tile_images[i]},
                                           .border = {.color = ACCENT,
                                                      .width = CLAY_BORDER_OUTSIDE(g.selected_tile == i ? 3 : 0)}}) {
                    Clay_OnHover(on_tile, (void *)(intptr_t)i);
                    if (Clay_Hovered()) { /* a tooltip, floating above the tile */
                        CLAY(CLAY_IDI("TileTip", i),
                             {.floating = {.attachTo = CLAY_ATTACH_TO_PARENT,
                                           .attachPoints = {.element = CLAY_ATTACH_POINT_CENTER_BOTTOM,
                                                            .parent = CLAY_ATTACH_POINT_CENTER_TOP},
                                           .offset = {0, -6}, .zIndex = 10},
                              .layout = {.padding = {8, 8, 4, 4}},
                              .backgroundColor = {15, 17, 24, 240}, .cornerRadius = CLAY_CORNER_RADIUS(4)}) {
                            text((Clay_String){.length = (int32_t)strlen(TILE_NAMES[i]), .chars = TILE_NAMES[i]}, 14,
                                 INK);
                        }
                    }
                }
            }
        }
        /* nine-slice: one 48 x 48 texture stretched to any size, children on top */
        CLAY(CLAY_ID("Sliced"), {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(56)},
                                            .childAlignment = {CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER}},
                                 .image = {.imageData = &g.panel_image}}) {
            text(CLAY_STRING("a nine-slice panel at any size"), 14, INK);
        }
    }
}

static void borders_card(void)
{
    static const Clay_BorderWidth widths[BORDER_BOXES] = {
        {2, 2, 2, 2, 0}, {6, 0, 0, 0, 0}, {0, 0, 3, 3, 0}, {2, 2, 2, 2, 0}, {8, 8, 8, 8, 0}};
    static const Clay_CornerRadius radii[BORDER_BOXES] = {
        {8, 8, 8, 8}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 16, 0, 16}, {20, 20, 20, 20}};

    CARD_OPEN("Borders", "Borders: per-side widths, per-corner radii")
    {
        CLAY(CLAY_ID("BorderRow"), {.layout = {.childGap = 10}}) {
            for (int i = 0; i < BORDER_BOXES; i++) {
                CLAY(CLAY_IDI("BorderBox", i), {.layout = {.sizing = {CLAY_SIZING_FIXED(56), CLAY_SIZING_FIXED(44)}},
                                                .backgroundColor = {60, 66, 84, 255}, .cornerRadius = radii[i],
                                                .border = {.color = ACCENT, .width = widths[i]}}) {}
            }
        }
        /* lines between children: Clay emits them as rectangles */
        CLAY(CLAY_ID("Segments"), {.layout = {.padding = {10, 10, 6, 6}, .childGap = 20},
                                   .cornerRadius = CLAY_CORNER_RADIUS(6),
                                   .border = {.color = DIM_INK, .width = CLAY_BORDER_ALL(1)}}) {
            text(CLAY_STRING("one"), 14, INK);
            text(CLAY_STRING("two"), 14, INK);
            text(CLAY_STRING("three"), 14, INK);
        }
    }
}

static void scrolling_card(void)
{
    static const char *ROW_TEXT[ROWS] = {"Scroll this list: wheel or drag", "row 2", "row 3", "",
                                         "row 5", "row 6", "row 7", "row 8", "row 9", "row 10", "row 11", "the end"};
    CARD_OPEN("Scrolling", "A scroll area inside a scroll area")
    {
        CLAY(CLAY_ID("Outer"), {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(170)}, .childGap = 6,
                                           .padding = CLAY_PADDING_ALL(8), .layoutDirection = CLAY_TOP_TO_BOTTOM},
                                .backgroundColor = {35, 39, 51, 255}, .cornerRadius = CLAY_CORNER_RADIUS(6),
                                .clip = {.vertical = true, .childOffset = Clay_GetScrollOffset()}}) {
            for (int i = 0; i < ROWS; i++) {
                if (i == 3) { /* the inner area: scrolls sideways, clipped by the outer one too */
                    CLAY(CLAY_ID("Inner"), {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(44)},
                                                       .childGap = 6, .padding = CLAY_PADDING_ALL(6)},
                                            .backgroundColor = {24, 27, 36, 255}, .cornerRadius = CLAY_CORNER_RADIUS(6),
                                            .clip = {.horizontal = true, .childOffset = Clay_GetScrollOffset()}}) {
                        for (int c = 0; c < CHIPS; c++) {
                            CLAY(CLAY_IDI("Chip", c),
                                 {.layout = {.sizing = {CLAY_SIZING_FIXED(56), CLAY_SIZING_GROW(0)}},
                                  .backgroundColor = {(float)(70 + c * 11), (float)(110 + (c * 37) % 90), 200, 255},
                                  .cornerRadius = CLAY_CORNER_RADIUS(4)}) {}
                        }
                    }
                    continue;
                }
                text((Clay_String){.length = (int32_t)strlen(ROW_TEXT[i]), .chars = ROW_TEXT[i]}, 14, DIM_INK);
            }
        }
    }
}

static void motion_card(void)
{
    CARD_OPEN("Motion", "Custom element, overlay, transition")
    {
        CLAY(CLAY_ID("Meter"), {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(24)}},
                                .custom = {.customData = &g.meter}}) {}
        text(CLAY_STRING("Every card darkens under the pointer: an overlay color."), 14, DIM_INK);
        CLAY(CLAY_ID("ExpandRow"), {.layout = {.childGap = 10, .childAlignment = {.y = CLAY_ALIGN_Y_CENTER}}}) {
            CLAY(CLAY_ID("ExpandButton"), {.layout = {.padding = {12, 12, 6, 6}},
                                           .backgroundColor = Clay_Hovered() ? ACCENT : (Clay_Color){70, 80, 105, 255},
                                           .cornerRadius = CLAY_CORNER_RADIUS(6)}) {
                Clay_OnHover(on_expand, NULL);
                text(g.expanded ? CLAY_STRING("Shrink") : CLAY_STRING("Expand"), 14, INK);
            }
            CLAY(CLAY_ID("Bar"), {.layout = {.sizing = {CLAY_SIZING_FIXED(g.expanded ? 220 : 40), CLAY_SIZING_FIXED(20)}},
                                  .backgroundColor = g.expanded ? (Clay_Color){110, 200, 140, 255} : ACCENT,
                                  .cornerRadius = CLAY_CORNER_RADIUS(10),
                                  .transition = {.handler = Clay_EaseOut, .duration = 0.4f,
                                                 .properties = CLAY_TRANSITION_PROPERTY_WIDTH |
                                                               CLAY_TRANSITION_PROPERTY_BACKGROUND_COLOR}}) {}
        }
    }
}

static Clay_RenderCommandArray elements_layout(float dt)
{
    Clay_BeginLayout();
    CLAY(CLAY_ID("Elements"), {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                                          .padding = CLAY_PADDING_ALL(16), .childGap = 12,
                                          .layoutDirection = CLAY_TOP_TO_BOTTOM},
                               .backgroundColor = PAGE}) {
        CLAY(CLAY_ID("Title"), {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}}}) {
            text(CLAY_STRING("Clay elements, drawn by libsk"), 20, INK);
            CLAY(CLAY_ID("TitleGap"), {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(1)}}}) {}
            text(CLAY_STRING("Tab: Clay's demo"), 14, DIM_INK);
        }
        CLAY(CLAY_ID("Top"), {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childGap = 12}}) {
            images_card();
            borders_card();
        }
        CLAY(CLAY_ID("Bottom"), {.layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIT(0)}, .childGap = 12}}) {
            scrolling_card();
            motion_card();
        }
    }
    return Clay_EndLayout(dt);
}

/* ------------------------------------------------------------- example ---- */

static void on_texture(const char *path, void *user)
{
    const sk_handle_t texture = sk_texture_create(path);
    if (user == &g.tiles) { /* pixel art: keep it crisp when scaled up */
        sk_texture_set_sampling(texture, SK_TEXTURE_WRAP_CLAMP, SK_TEXTURE_WRAP_CLAMP, SK_TEXTURE_FILTER_NEAREST);
    }
    *(sk_handle_t *)user = texture;
}

static void on_failed(const char *path, void *user)
{
    (void)user;
    sk_logger_error("load failed: %s", path);
}

static void init(void *user_data)
{
    const vec2_t screen = sk_window_get_screen_size();
    const uint32_t memory = Clay_MinMemorySize();
    (void)user_data;
    sk_asset_set_host(EXAMPLE_ASSET_BASE);
    Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(memory, malloc(memory)),
                    (Clay_Dimensions){screen.x - GAME_WIDTH, screen.y}, (Clay_ErrorHandler){on_clay_error, NULL});
    Clay_SetMeasureTextFunction(measure_text, NULL);
    g.demo = ClayVideoDemo_Initialize();
    g.selected_tile = -1;
    g.meter = (clay_custom_t){draw_meter, NULL};
    g.game_bg = sk_color_rgba(24, 30, 40, 255);
    g.marker = sk_color_rgba(240, 190, 90, 255);
    g.hint = sk_color_rgba(140, 150, 170, 255);
    sk_asset_add_task(sk_asset_ensure_async(TILES_PATH, NULL, SK_ASSET_NONE), on_texture, on_failed, &g.tiles);
    sk_asset_add_task(sk_asset_ensure_async(PANEL_PATH, NULL, SK_ASSET_NONE), on_texture, on_failed, &g.panel);
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
    if (kb.keys[SK_KEY_TAB] == SK_BUTTON_PRESSED) g.elements = !g.elements;
    g.time += dt;

    /* image data follows the textures as they load */
    for (int i = 0; i < TILES; i++) {
        g.tile_images[i] = (clay_image_t){.texture = g.tiles,
                                          .source = {TILE_CELLS[i][0], TILE_CELLS[i][1], TILE_CELLS[i][2],
                                                     TILE_CELLS[i][3]},
                                          .tint = i == 7 ? sk_color_rgba(120, 190, 255, 255) : 0};
    }
    g.panel_image = (clay_image_t){.texture = g.panel, .slice = {16, 16, 16, 16}};

    /* the UI: size, pointer and scrolling in, capture out, then its layout */
    Clay_SetLayoutDimensions((Clay_Dimensions){ui_width, screen.y});
    Clay_SetPointerState((Clay_Vector2){(float)mouse.x, (float)mouse.y}, mouse.left == SK_BUTTON_PRESSED ||
                                                                              mouse.left == SK_BUTTON_DOWN);
    Clay_UpdateScrollContainers(true, (Clay_Vector2){mouse.wheel_x * SCROLL_SPEED, mouse.wheel * SCROLL_SPEED}, dt);
    update_pointer_capture(&mouse);
    commands = g.elements ? elements_layout(dt) : ClayVideoDemo_CreateLayout(&g.demo);

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
    sk_text_draw_ex(0, "Tab: switch UI page", ui_width + 16, 76, 14, g.hint);
    render(commands);
    sk_render_end();
}

int main(void)
{
    sk_init_values(1180, 720, "libsk clay", SK_WINDOW_FLAG_MSAA_4X_HINT | SK_WINDOW_FLAG_WINDOW_RESIZABLE);
    sk_set_init(init, NULL);
    sk_set_frame(frame, NULL);
    return sk_run();
}
