/* Scene pointer interaction (docs/PLAN-2d.md): hover, press and click per member,
 * enabled, capture, 2D over 3D, touch, tick edges. Pointer events go through the real
 * input code; each "frame" updates the interaction and then clears frame edges, like
 * the runtime. */
#include "internal/sk_camera3d.h"
#include "internal/sk_internal.h"
#include "internal/sk_platform.h"
#include "internal/sk_render.h"
#include "internal/sk_scene.h"
#include "internal/sk_sprite2d.h"
#include "sk_camera3d.h"
#include "sk_input.h"
#include "sk_logger.h"
#include "sk_scene.h"
#include "sk_shape3d.h"
#include "sk_sprite2d.h"
#include "sk_texture.h"
#include "sk_window.h"
#include "test.h"
#include "tests.h"

#include "sokol_app.h"
#include "sokol_gfx.h"

static struct {
    float x, y;
    bool frame_open; /* checks may still read this frame's edges */
} pointer;

/* The runtime clears a frame's edges after its callbacks, before the next events. */
static void end_frame(void)
{
    if (pointer.frame_open) {
        sk_input_end_frame();
        sk_scene_end_frame_interaction();
        pointer.frame_open = false;
    }
}

static void event(sapp_event_type type)
{
    end_frame();
    sapp_event ev = {.type = type, .mouse_x = pointer.x, .mouse_y = pointer.y, .mouse_button = SAPP_MOUSEBUTTON_LEFT};
    sk_input_handle_event(&ev);
}

static void move(float x, float y)
{
    end_frame();
    pointer.x = x;
    pointer.y = y;
    event(SAPP_EVENTTYPE_MOUSE_MOVE);
}

/* A frame: the events since the previous one are in; update before the callbacks
 * (the checks that follow). */
static void frame(void)
{
    end_frame();
    sk_scene_update_interaction();
    pointer.frame_open = true;
}

void test_interaction(void)
{
    const vec2_t screen = sk_window_get_screen_size();

    sg_setup(&(sg_desc){.environment = sk_platform_environment()});
    sk_render_init();
    sk_scene_init();
    sk_camera3d_init();
    sk_texture_init();
    sk_sprite2d_init();
    sk_shape3d_init();
    sk_input_init();
    sk_logger_set_level(SK_LOGGER_LEVEL_ERROR);
    CHECK(sk_window_set_size(800, 600));

    sk_handle_t camera = sk_camera3d_create(SK_CAMERA3D_PERSPECTIVE);
    sk_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    sk_handle_t scene = sk_scene_create();
    sk_scene_set_active_camera(scene, camera);

    sk_handle_t button = sk_sprite2d_create(sk_texture_get_default()); /* 100x40 centered on (200, 100) */
    sk_sprite2d_set_size(button, 100, 40);
    sk_sprite2d_set_pivot(button, 0.5f, 0.5f);
    sk_sprite2d_set_position(button, 200, 100);
    sk_scene_add(scene, button, 0);

    sk_handle_t cube = sk_shape3d_create(); /* at the origin: the middle of the screen */
    sk_shape3d_set_cube(cube, 2, 2, 2);
    sk_scene_add(scene, cube, 0);

    /* not interactive: nothing tracked */
    move(200, 100);
    frame();
    CHECK(sk_scene_get_hovered(scene) == 0);
    CHECK(sk_scene_set_interactive(scene, true));
    CHECK(sk_scene_is_interactive(scene));

    /* hover: enter, stay, leave */
    frame();
    CHECK(sk_scene_get_hovered(scene) == button);
    CHECK(sk_scene_get_hover(scene, button) == SK_BUTTON_PRESSED);
    frame();
    CHECK(sk_scene_get_hover(scene, button) == SK_BUTTON_DOWN);
    CHECK(sk_scene_get_hover(scene, cube) == SK_BUTTON_UP);
    move(400, 300); /* over the cube */
    frame();
    CHECK(sk_scene_get_hovered(scene) == cube);
    CHECK(sk_scene_get_hover(scene, button) == SK_BUTTON_RELEASED);
    CHECK(sk_scene_get_hover(scene, cube) == SK_BUTTON_PRESSED);
    frame();
    CHECK(sk_scene_get_hover(scene, button) == SK_BUTTON_UP);

    /* press on the 3D cube: no capture */
    event(SAPP_EVENTTYPE_MOUSE_DOWN);
    frame();
    CHECK(sk_scene_get_press(scene, cube) == SK_BUTTON_PRESSED);
    CHECK(!sk_input_is_pointer_captured());
    event(SAPP_EVENTTYPE_MOUSE_UP);
    frame();
    CHECK(sk_scene_get_press(scene, cube) == SK_BUTTON_RELEASED);
    CHECK(sk_scene_is_clicked(scene, cube));
    frame();
    CHECK(sk_scene_get_press(scene, cube) == SK_BUTTON_UP);
    CHECK(!sk_scene_is_clicked(scene, cube));

    /* press on the button, drag off, release elsewhere: no click; captured through the release frame */
    move(200, 100);
    frame();
    event(SAPP_EVENTTYPE_MOUSE_DOWN);
    frame();
    CHECK(sk_scene_get_press(scene, button) == SK_BUTTON_PRESSED);
    CHECK(sk_input_is_pointer_captured());
    move(600, 500);
    frame();
    CHECK(sk_scene_get_press(scene, button) == SK_BUTTON_DOWN);
    CHECK(sk_scene_get_hover(scene, button) == SK_BUTTON_RELEASED);
    event(SAPP_EVENTTYPE_MOUSE_UP);
    frame();
    CHECK(sk_scene_get_press(scene, button) == SK_BUTTON_RELEASED);
    CHECK(!sk_scene_is_clicked(scene, button));
    CHECK(sk_input_is_pointer_captured());
    frame();
    CHECK(!sk_input_is_pointer_captured());

    /* a tap within one frame: pressed and clicked together */
    move(200, 100);
    event(SAPP_EVENTTYPE_MOUSE_DOWN);
    event(SAPP_EVENTTYPE_MOUSE_UP);
    frame();
    CHECK(sk_scene_get_press(scene, button) == SK_BUTTON_PRESSED);
    CHECK(sk_scene_is_clicked(scene, button));

    /* the 2D button over the 3D cube is hit first */
    sk_sprite2d_set_position(button, 400, 300);
    move(400, 300);
    frame();
    CHECK(sk_scene_get_hovered(scene) == button);

    /* disabled: still hovered (blocks the cube), but doesn't react */
    CHECK(sk_sprite2d_set_enabled(button, false));
    CHECK(!sk_sprite2d_is_enabled(button));
    event(SAPP_EVENTTYPE_MOUSE_DOWN);
    event(SAPP_EVENTTYPE_MOUSE_UP);
    frame();
    CHECK(sk_scene_get_hovered(scene) == button);
    CHECK(sk_scene_get_hover(scene, button) == SK_BUTTON_UP);
    CHECK(sk_scene_get_press(scene, button) == SK_BUTTON_UP);
    CHECK(!sk_scene_is_clicked(scene, button));
    CHECK(sk_scene_get_hover(scene, cube) == SK_BUTTON_UP);
    CHECK(!sk_scene_is_clicked(scene, cube));
    sk_sprite2d_set_enabled(button, true);

    /* not pickable: the pointer goes through to the cube */
    sk_sprite2d_set_pickable(button, false);
    frame();
    CHECK(sk_scene_get_hovered(scene) == cube);
    sk_sprite2d_set_pickable(button, true);

    /* touch drives the pointer like the left button */
    frame();
    {
        sapp_event touch = {.type = SAPP_EVENTTYPE_TOUCHES_BEGAN, .num_touches = 1};
        touch.touches[0] = (sapp_touchpoint){.identifier = 7, .pos_x = 400, .pos_y = 300, .changed = true};
        end_frame();
        sk_input_handle_event(&touch);
        frame();
        CHECK(sk_scene_get_press(scene, button) == SK_BUTTON_PRESSED);
        touch.type = SAPP_EVENTTYPE_TOUCHES_ENDED;
        end_frame();
        sk_input_handle_event(&touch);
        frame();
        CHECK(sk_scene_is_clicked(scene, button));

        /* a second finger cancels the press: released off-screen, no click, and the
           pointer stays up until both fingers lift */
        frame();
        touch.type = SAPP_EVENTTYPE_TOUCHES_BEGAN;
        end_frame();
        sk_input_handle_event(&touch);
        frame();
        CHECK(sk_scene_get_press(scene, button) == SK_BUTTON_PRESSED);
        sapp_event second = {.type = SAPP_EVENTTYPE_TOUCHES_BEGAN, .num_touches = 2};
        second.touches[0] = (sapp_touchpoint){.identifier = 7, .pos_x = 400, .pos_y = 300};
        second.touches[1] = (sapp_touchpoint){.identifier = 8, .pos_x = 500, .pos_y = 300, .changed = true};
        end_frame();
        sk_input_handle_event(&second);
        frame();
        CHECK(sk_scene_get_press(scene, button) == SK_BUTTON_RELEASED);
        CHECK(!sk_scene_is_clicked(scene, button));
        CHECK(sk_input_get_mouse_position().x == -1 && sk_input_get_mouse_position().y == -1);
        second.type = SAPP_EVENTTYPE_TOUCHES_ENDED; /* the second finger lifts... */
        end_frame();
        sk_input_handle_event(&second);
        touch.type = SAPP_EVENTTYPE_TOUCHES_MOVED;  /* ...and the first one moves on */
        sk_input_handle_event(&touch);
        frame();
        CHECK(sk_input_get_mouse_button(0) == SK_BUTTON_UP);
        CHECK(sk_input_get_mouse_position().x == -1); /* still cancelled */
        touch.type = SAPP_EVENTTYPE_TOUCHES_ENDED;
        end_frame();
        sk_input_handle_event(&touch);
        frame();
        CHECK(!sk_scene_is_clicked(scene, button));
        touch.type = SAPP_EVENTTYPE_TOUCHES_BEGAN; /* all lifted: the next touch is a pointer again */
        end_frame();
        sk_input_handle_event(&touch);
        touch.type = SAPP_EVENTTYPE_TOUCHES_ENDED;
        sk_input_handle_event(&touch);
        frame();
        CHECK(sk_scene_is_clicked(scene, button));
    }

    /* tick edges carry over frames that ran no tick */
    frame();
    sk_scene_end_tick_interaction(); /* a tick ran: start from nothing */
    move(10, 10);
    frame(); /* left the button: no tick ran */
    frame();
    sk_input_set_context(SK_INPUT_CONTEXT_TICK);
    CHECK(sk_scene_get_hover(scene, button) == SK_BUTTON_RELEASED);
    sk_scene_end_tick_interaction();
    CHECK(sk_scene_get_hover(scene, button) == SK_BUTTON_UP);
    sk_input_set_context(SK_INPUT_CONTEXT_FRAME);

    end_frame();
    CHECK(sk_window_set_size((int)screen.x, (int)screen.y));
    sk_logger_set_level(SK_LOGGER_LEVEL_INFO);
    sk_scene_destroy(scene);
    sk_input_deinit();
    sk_shape3d_deinit();
    sk_sprite2d_deinit();
    sk_texture_deinit();
    sk_camera3d_deinit();
    sk_scene_deinit();
    sk_render_deinit();
    sg_shutdown();
}
