/* Widgets for the examples — buttons, a progress bar and a scrolling list, built from
 * libsk's public API (2D shapes, text2d, scene interaction) the way a game would.
 *
 * libsk has no widget API on purpose (docs/ROADMAP.md, "GUI direction"): what a widget
 * is — how it's themed, which one has focus, how it takes the keyboard — is policy, and
 * a library that picks it for everyone is the GUI toolkit that decision rules out. This
 * header is example code, not API: copy it, change it, throw it away. Anything here
 * that libsk's public API can't express is a gap in libsk, to fix there.
 *
 * Each widget is scene members, so they're drawn, picked and clipped with everything
 * else: create them once, then call the update each frame for their colors and what
 * the pointer did. Layers decide what's on top; a label sits above its shape and isn't
 * pickable, so the shape under it takes the pointer. */
#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include <stdbool.h>
#include <stddef.h>

#include "sk.h"

#define UI_LIST_MAX_ROWS 64

/* The colors widgets draw themselves in. */
typedef struct {
    sk_color_t idle, hover, pressed, disabled; /* a button's rectangle */
    sk_color_t text, text_disabled;
    sk_color_t track, fill, knob;  /* a bar */
    sk_color_t row, row_selected;  /* a list */
} ui_theme_t;

static inline ui_theme_t ui_theme_default(void)
{
    ui_theme_t theme;
    theme.idle = sk_color_rgba(70, 80, 105, 255);
    theme.hover = sk_color_rgba(95, 115, 160, 255);
    theme.pressed = sk_color_rgba(45, 55, 80, 255);
    theme.disabled = sk_color_rgba(55, 58, 64, 255);
    theme.text = sk_color_rgba(235, 238, 245, 255);
    theme.text_disabled = sk_color_rgba(120, 124, 132, 255);
    theme.track = sk_color_rgba(150, 175, 230, 255);
    theme.fill = sk_color_rgba(110, 200, 140, 255);
    theme.knob = sk_color_rgba(235, 238, 245, 255);
    theme.row = sk_color_rgba(44, 50, 66, 255);
    theme.row_selected = sk_color_rgba(80, 110, 90, 255);
    return theme;
}

/* True while the pointer is over or pressing something (its state covers both the
 * frame it started and the frames after). */
static inline bool ui_is_active(sk_button_state_t state)
{
    return state == SK_BUTTON_PRESSED || state == SK_BUTTON_DOWN;
}

/* ------------------------------------------------------------------ button ---- */

/* A rounded rectangle with a label centered on it. */
typedef struct {
    sk_handle_t shape, label;
    float x, y, width, height;
} ui_button_t;

static inline ui_button_t ui_button_create(sk_handle_t scene, int layer, const char *text, float x, float y,
                                           float width, float height, float text_size)
{
    ui_button_t button;
    button.x = x, button.y = y, button.width = width, button.height = height;
    button.shape = sk_shape2d_create();
    sk_shape2d_set_rectangle(button.shape, width, height, 10);
    sk_shape2d_set_transform(button.shape, x, y, 0, 1, 1);
    sk_scene_add(scene, button.shape, layer);

    /* centered on the button, so the label needs no measuring */
    button.label = sk_text2d_create(0);
    sk_text2d_set_text(button.label, text);
    sk_text2d_set_size(button.label, text_size);
    sk_text2d_set_align(button.label, SK_TEXT_ALIGN_CENTER, SK_TEXT_ALIGN_MIDDLE);
    sk_text2d_set_position(button.label, x + width * 0.5f, y + height * 0.5f);
    sk_text2d_set_pickable(button.label, false); /* the rectangle under it takes the pointer */
    sk_scene_add(scene, button.label, layer + 1);
    return button;
}

/* Color it for what the pointer is doing, and say whether it was clicked this frame.
 * A disabled button still blocks the pointer; it just doesn't react. */
static inline bool ui_button_update(const ui_button_t *button, sk_handle_t scene, const ui_theme_t *theme)
{
    const bool enabled = sk_shape2d_is_enabled(button->shape);
    const bool held = ui_is_active(sk_scene_get_press(scene, button->shape));
    const bool over = ui_is_active(sk_scene_get_hover(scene, button->shape));
    sk_shape2d_set_color(button->shape,
                         !enabled ? theme->disabled : held ? theme->pressed : over ? theme->hover : theme->idle);
    sk_text2d_set_color(button->label, enabled ? theme->text : theme->text_disabled);
    return enabled && sk_scene_is_clicked(scene, button->shape);
}

static inline void ui_button_set_enabled(const ui_button_t *button, bool enabled)
{
    sk_shape2d_set_enabled(button->shape, enabled);
}

static inline bool ui_button_is_enabled(const ui_button_t *button)
{
    return sk_shape2d_is_enabled(button->shape);
}

static inline void ui_button_set_text(const ui_button_t *button, const char *text)
{
    sk_text2d_set_text(button->label, text);
}

static inline void ui_button_destroy(ui_button_t *button)
{
    sk_text2d_destroy(button->label);
    sk_shape2d_destroy(button->shape);
    button->label = button->shape = 0;
}

/* --------------------------------------------------------------------- bar ---- */

/* A progress bar: an outlined track, a rounded fill and a round knob at its end.
 * Nothing about it is pickable — it shows a value, it doesn't take one. */
typedef struct {
    sk_handle_t track, fill, knob;
    float x, y, width, height;
} ui_bar_t;

static inline ui_bar_t ui_bar_create(sk_handle_t scene, int layer, float x, float y, float width, float height)
{
    ui_bar_t bar;
    bar.x = x, bar.y = y, bar.width = width, bar.height = height;
    bar.track = sk_shape2d_create();
    sk_shape2d_set_rectangle(bar.track, width, height, height * 0.5f);
    sk_shape2d_set_transform(bar.track, x, y, 0, 1, 1);
    sk_shape2d_set_outline(bar.track, 2);
    sk_shape2d_set_pickable(bar.track, false);
    sk_scene_add(scene, bar.track, layer + 1); /* over the fill */

    bar.fill = sk_shape2d_create();
    sk_shape2d_set_pickable(bar.fill, false);
    sk_scene_add(scene, bar.fill, layer);

    bar.knob = sk_shape2d_create();
    sk_shape2d_set_circle(bar.knob, height * 0.34f);
    sk_shape2d_set_pickable(bar.knob, false);
    sk_scene_add(scene, bar.knob, layer + 1);
    return bar;
}

/* Show `value` (0..1): the fill grows from the left and the knob rides its end. At 0
 * there's nothing to show, so the fill and knob are hidden. */
static inline void ui_bar_set_value(const ui_bar_t *bar, float value, const ui_theme_t *theme)
{
    const float clamped = value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;
    const float round = bar->height * 0.5f;
    const float length = bar->width * clamped;
    const bool any = clamped > 0.0f;

    sk_shape2d_set_color(bar->track, theme->track);
    sk_shape2d_set_rectangle(bar->fill, length > bar->height ? length : bar->height, bar->height, round);
    sk_shape2d_set_transform(bar->fill, bar->x, bar->y, 0, 1, 1);
    sk_shape2d_set_color(bar->fill, theme->fill);
    sk_shape2d_set_visible(bar->fill, any);
    sk_shape2d_set_transform(bar->knob, bar->x + (length > round ? length - round : round), bar->y + round, 0, 1, 1);
    sk_shape2d_set_color(bar->knob, theme->knob);
    sk_shape2d_set_visible(bar->knob, any);
}

static inline void ui_bar_destroy(ui_bar_t *bar)
{
    sk_shape2d_destroy(bar->knob);
    sk_shape2d_destroy(bar->fill);
    sk_shape2d_destroy(bar->track);
    bar->track = bar->fill = bar->knob = 0;
}

/* -------------------------------------------------------------------- list ---- */

/* Rows of text in a box: the wheel scrolls them and clicking one selects it. The rows
 * and their labels live on two layers clipped to the same box, so a row scrolled out of
 * it is neither drawn nor hit. */
typedef struct {
    sk_handle_t rows[UI_LIST_MAX_ROWS], labels[UI_LIST_MAX_ROWS];
    int count, selected;
    float x, y, width, height, row_height, scroll;
} ui_list_t;

/* Where a row sits now, given the scroll. */
static inline void ui_list_place(const ui_list_t *list)
{
    for (int i = 0; i < list->count; i++) {
        const float y = list->y + (float)i * list->row_height - list->scroll;
        sk_shape2d_set_transform(list->rows[i], list->x, y, 0, 1, 1);
        sk_text2d_set_position(list->labels[i], list->x + 12, y + list->row_height * 0.5f);
    }
}

static inline ui_list_t ui_list_create(sk_handle_t scene, int row_layer, const char *const *names, int count, float x,
                                       float y, float width, float height, float row_height, float text_size)
{
    ui_list_t list;
    list.count = count < UI_LIST_MAX_ROWS ? count : UI_LIST_MAX_ROWS;
    list.selected = -1;
    list.x = x, list.y = y, list.width = width, list.height = height;
    list.row_height = row_height, list.scroll = 0.0f;
    for (int i = 0; i < list.count; i++) {
        list.rows[i] = sk_shape2d_create();
        sk_shape2d_set_rectangle(list.rows[i], width, row_height - 4.0f, 6);
        sk_scene_add(scene, list.rows[i], row_layer);

        list.labels[i] = sk_text2d_create(0);
        sk_text2d_set_text(list.labels[i], names[i]);
        sk_text2d_set_size(list.labels[i], text_size);
        sk_text2d_set_align(list.labels[i], SK_TEXT_ALIGN_LEFT, SK_TEXT_ALIGN_MIDDLE);
        sk_text2d_set_pickable(list.labels[i], false);
        sk_scene_add(scene, list.labels[i], row_layer + 1);
    }
    sk_scene_set_clip(scene, row_layer, x, y, width, height);
    sk_scene_set_clip(scene, row_layer + 1, x, y, width, height);
    ui_list_place(&list);
    return list;
}

/* Scroll by `wheel` notches, color the rows, and return the selected row (-1 for
 * none). Pass sk_input_get_mouse_state().wheel. */
static inline int ui_list_update(ui_list_t *list, sk_handle_t scene, const ui_theme_t *theme, float wheel)
{
    const float span = (float)list->count * list->row_height - list->height;
    const float most = span > 0.0f ? span : 0.0f;

    if (wheel != 0.0f) {
        list->scroll -= wheel * list->row_height;
        list->scroll = list->scroll < 0.0f ? 0.0f : list->scroll > most ? most : list->scroll;
    }
    ui_list_place(list);
    for (int i = 0; i < list->count; i++) {
        const bool over = ui_is_active(sk_scene_get_hover(scene, list->rows[i]));
        if (sk_scene_is_clicked(scene, list->rows[i])) {
            list->selected = i;
        }
        sk_shape2d_set_color(list->rows[i],
                             list->selected == i ? theme->row_selected : over ? theme->hover : theme->row);
        sk_text2d_set_color(list->labels[i], theme->text);
    }
    return list->selected;
}

static inline void ui_list_destroy(ui_list_t *list)
{
    for (int i = 0; i < list->count; i++) {
        sk_text2d_destroy(list->labels[i]);
        sk_shape2d_destroy(list->rows[i]);
    }
    list->count = 0;
    list->selected = -1;
}

#endif // UI_WIDGETS_H
