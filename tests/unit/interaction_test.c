/* Scene pointer interaction (docs/PLAN-2d.md): hover, press and click per member,
 * enabled, capture, 2D over 3D, touch, tick edges. Pointer events go through the real
 * input code; each "frame" updates the interaction and then clears frame edges, like
 * the runtime. */
#include "internal/wgr_camera3d_internal.h"
#include "internal/wgr_internal_internal.h"
#include "internal/wgr_platform_internal.h"
#include "internal/wgr_render_internal.h"
#include "internal/wgr_scene_internal.h"
#include "internal/wgr_sprite2d_internal.h"
#include "wgr_camera3d.h"
#include "wgr_input.h"
#include "wgr_logger.h"
#include "wgr_scene.h"
#include "wgr_shape3d.h"
#include "wgr_sprite2d.h"
#include "wgr_texture.h"
#include "wgr_window.h"
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
        wgr_input_end_frame();
        wgr_scene_end_frame_interaction();
        pointer.frame_open = false;
    }
}

static void event(sapp_event_type type)
{
    end_frame();
    sapp_event ev = {.type = type, .mouse_x = pointer.x, .mouse_y = pointer.y, .mouse_button = SAPP_MOUSEBUTTON_LEFT};
    wgr_input_handle_event(&ev);
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
    wgr_scene_update_interaction();
    pointer.frame_open = true;
}

void test_interaction(void)
{
    const vec2_t screen = wgr_window_get_screen_size();

    sg_setup(&(sg_desc){.environment = wgr_platform_environment()});
    wgr_render_init();
    wgr_scene_init();
    wgr_camera3d_init();
    wgr_texture_init();
    wgr_sprite2d_init();
    wgr_shape3d_init();
    wgr_input_init();
    wgr_logger_set_level(WGR_LOGGER_LEVEL_ERROR);
    CHECK(wgr_window_set_size(800, 600));

    wgr_handle_t camera = wgr_camera3d_create(WGR_CAMERA3D_PERSPECTIVE);
    wgr_camera3d_set_view(camera, 0, 0, 10, 0, 0, 0, 0, 1, 0);
    wgr_handle_t scene = wgr_scene_create();
    wgr_scene_set_active_camera(scene, camera);

    wgr_handle_t button = wgr_sprite2d_create(wgr_texture_get_default()); /* 100x40 centered on (200, 100) */
    wgr_sprite2d_set_size(button, 100, 40);
    wgr_sprite2d_set_pivot(button, 0.5f, 0.5f);
    wgr_sprite2d_set_position(button, 200, 100);
    wgr_scene_add(scene, button, 0);

    wgr_handle_t cube = wgr_shape3d_create(); /* at the origin: the middle of the screen */
    wgr_shape3d_set_cube(cube, 2, 2, 2);
    wgr_scene_add(scene, cube, 0);

    /* not interactive: nothing tracked */
    move(200, 100);
    frame();
    CHECK(wgr_scene_get_hovered(scene) == 0);
    CHECK(wgr_scene_set_interactive(scene, true));
    CHECK(wgr_scene_is_interactive(scene));

    /* hover: enter, stay, leave */
    frame();
    CHECK(wgr_scene_get_hovered(scene) == button);
    CHECK(wgr_scene_get_hover(scene, button) == WGR_BUTTON_PRESSED);
    frame();
    CHECK(wgr_scene_get_hover(scene, button) == WGR_BUTTON_DOWN);
    CHECK(wgr_scene_get_hover(scene, cube) == WGR_BUTTON_UP);
    move(400, 300); /* over the cube */
    frame();
    CHECK(wgr_scene_get_hovered(scene) == cube);
    CHECK(wgr_scene_get_hover(scene, button) == WGR_BUTTON_RELEASED);
    CHECK(wgr_scene_get_hover(scene, cube) == WGR_BUTTON_PRESSED);
    frame();
    CHECK(wgr_scene_get_hover(scene, button) == WGR_BUTTON_UP);

    /* press on the 3D cube: no capture */
    event(SAPP_EVENTTYPE_MOUSE_DOWN);
    frame();
    CHECK(wgr_scene_get_press(scene, cube) == WGR_BUTTON_PRESSED);
    CHECK(!wgr_input_is_pointer_captured());
    event(SAPP_EVENTTYPE_MOUSE_UP);
    frame();
    CHECK(wgr_scene_get_press(scene, cube) == WGR_BUTTON_RELEASED);
    CHECK(wgr_scene_is_clicked(scene, cube));
    frame();
    CHECK(wgr_scene_get_press(scene, cube) == WGR_BUTTON_UP);
    CHECK(!wgr_scene_is_clicked(scene, cube));

    /* press on the button, drag off, release elsewhere: no click; captured through the release frame */
    move(200, 100);
    frame();
    event(SAPP_EVENTTYPE_MOUSE_DOWN);
    frame();
    CHECK(wgr_scene_get_press(scene, button) == WGR_BUTTON_PRESSED);
    CHECK(wgr_input_is_pointer_captured());
    move(600, 500);
    frame();
    CHECK(wgr_scene_get_press(scene, button) == WGR_BUTTON_DOWN);
    CHECK(wgr_scene_get_hover(scene, button) == WGR_BUTTON_RELEASED);
    event(SAPP_EVENTTYPE_MOUSE_UP);
    frame();
    CHECK(wgr_scene_get_press(scene, button) == WGR_BUTTON_RELEASED);
    CHECK(!wgr_scene_is_clicked(scene, button));
    CHECK(wgr_input_is_pointer_captured());
    frame();
    CHECK(!wgr_input_is_pointer_captured());

    /* a tap within one frame: pressed and clicked together */
    move(200, 100);
    event(SAPP_EVENTTYPE_MOUSE_DOWN);
    event(SAPP_EVENTTYPE_MOUSE_UP);
    frame();
    CHECK(wgr_scene_get_press(scene, button) == WGR_BUTTON_PRESSED);
    CHECK(wgr_scene_is_clicked(scene, button));

    /* the 2D button over the 3D cube is hit first */
    wgr_sprite2d_set_position(button, 400, 300);
    move(400, 300);
    frame();
    CHECK(wgr_scene_get_hovered(scene) == button);

    /* disabled: still hovered (blocks the cube), but doesn't react */
    CHECK(wgr_sprite2d_set_enabled(button, false));
    CHECK(!wgr_sprite2d_is_enabled(button));
    event(SAPP_EVENTTYPE_MOUSE_DOWN);
    event(SAPP_EVENTTYPE_MOUSE_UP);
    frame();
    CHECK(wgr_scene_get_hovered(scene) == button);
    CHECK(wgr_scene_get_hover(scene, button) == WGR_BUTTON_UP);
    CHECK(wgr_scene_get_press(scene, button) == WGR_BUTTON_UP);
    CHECK(!wgr_scene_is_clicked(scene, button));
    CHECK(wgr_scene_get_hover(scene, cube) == WGR_BUTTON_UP);
    CHECK(!wgr_scene_is_clicked(scene, cube));
    wgr_sprite2d_set_enabled(button, true);

    /* not pickable: the pointer goes through to the cube */
    wgr_sprite2d_set_pickable(button, false);
    frame();
    CHECK(wgr_scene_get_hovered(scene) == cube);
    wgr_sprite2d_set_pickable(button, true);

    /* touch drives the pointer like the left button */
    frame();
    {
        sapp_event touch = {.type = SAPP_EVENTTYPE_TOUCHES_BEGAN, .num_touches = 1};
        touch.touches[0] = (sapp_touchpoint){.identifier = 7, .pos_x = 400, .pos_y = 300, .changed = true};
        end_frame();
        wgr_input_handle_event(&touch);
        frame();
        CHECK(wgr_scene_get_press(scene, button) == WGR_BUTTON_PRESSED);
        touch.type = SAPP_EVENTTYPE_TOUCHES_ENDED;
        end_frame();
        wgr_input_handle_event(&touch);
        frame();
        CHECK(wgr_scene_is_clicked(scene, button));

        /* a second finger cancels the press: released off-screen, no click, and the
           pointer stays up until both fingers lift */
        frame();
        touch.type = SAPP_EVENTTYPE_TOUCHES_BEGAN;
        end_frame();
        wgr_input_handle_event(&touch);
        frame();
        CHECK(wgr_scene_get_press(scene, button) == WGR_BUTTON_PRESSED);
        sapp_event second = {.type = SAPP_EVENTTYPE_TOUCHES_BEGAN, .num_touches = 2};
        second.touches[0] = (sapp_touchpoint){.identifier = 7, .pos_x = 400, .pos_y = 300};
        second.touches[1] = (sapp_touchpoint){.identifier = 8, .pos_x = 500, .pos_y = 300, .changed = true};
        end_frame();
        wgr_input_handle_event(&second);
        frame();
        CHECK(wgr_scene_get_press(scene, button) == WGR_BUTTON_RELEASED);
        CHECK(!wgr_scene_is_clicked(scene, button));
        CHECK(wgr_input_get_mouse_position().x == -1 && wgr_input_get_mouse_position().y == -1);
        second.type = SAPP_EVENTTYPE_TOUCHES_ENDED; /* the second finger lifts... */
        end_frame();
        wgr_input_handle_event(&second);
        touch.type = SAPP_EVENTTYPE_TOUCHES_MOVED;  /* ...and the first one moves on */
        wgr_input_handle_event(&touch);
        frame();
        CHECK(wgr_input_get_mouse_button(0) == WGR_BUTTON_UP);
        CHECK(wgr_input_get_mouse_position().x == -1); /* still cancelled */
        touch.type = SAPP_EVENTTYPE_TOUCHES_ENDED;
        end_frame();
        wgr_input_handle_event(&touch);
        frame();
        CHECK(!wgr_scene_is_clicked(scene, button));
        touch.type = SAPP_EVENTTYPE_TOUCHES_BEGAN; /* all lifted: the next touch is a pointer again */
        end_frame();
        wgr_input_handle_event(&touch);
        touch.type = SAPP_EVENTTYPE_TOUCHES_ENDED;
        wgr_input_handle_event(&touch);
        frame();
        CHECK(wgr_scene_is_clicked(scene, button));
    }

    /* tick edges carry over frames that ran no tick */
    frame();
    wgr_scene_end_tick_interaction(); /* a tick ran: start from nothing */
    move(10, 10);
    frame(); /* left the button: no tick ran */
    frame();
    wgr_input_set_context(WGR_INPUT_CONTEXT_TICK);
    CHECK(wgr_scene_get_hover(scene, button) == WGR_BUTTON_RELEASED);
    wgr_scene_end_tick_interaction();
    CHECK(wgr_scene_get_hover(scene, button) == WGR_BUTTON_UP);
    wgr_input_set_context(WGR_INPUT_CONTEXT_FRAME);

    /* destroying an object takes it out of every scene, with its hover and press state */
    {
        wgr_handle_t other = wgr_scene_create();
        wgr_handle_t gone = wgr_sprite2d_create(wgr_texture_get_default());
        wgr_sprite2d_set_size(gone, 100, 40);
        wgr_sprite2d_set_position(gone, 650, 100);
        CHECK(wgr_scene_add(scene, gone, 0) && wgr_scene_add(other, gone, 0));
        move(650, 100);
        frame();
        CHECK(wgr_scene_get_hovered(scene) == gone);
        event(SAPP_EVENTTYPE_MOUSE_DOWN);
        frame();
        CHECK(wgr_scene_get_press(scene, gone) == WGR_BUTTON_PRESSED);
        wgr_sprite2d_destroy(gone);
        CHECK(wgr_scene_get_hovered(scene) == 0);
        CHECK(!wgr_scene_set_layer(scene, gone, 1)); /* not a member any more */
        CHECK(!wgr_scene_set_layer(other, gone, 1));
        event(SAPP_EVENTTYPE_MOUSE_UP);
        frame();
        CHECK(!wgr_scene_is_clicked(scene, gone));
        CHECK(wgr_scene_get_hovered(scene) == 0);
        wgr_scene_destroy(other);
    }

    end_frame();
    CHECK(wgr_window_set_size((int)screen.x, (int)screen.y));
    wgr_logger_set_level(WGR_LOGGER_LEVEL_INFO);
    wgr_scene_destroy(scene);
    wgr_input_deinit();
    wgr_shape3d_deinit();
    wgr_sprite2d_deinit();
    wgr_texture_deinit();
    wgr_camera3d_deinit();
    wgr_scene_deinit();
    wgr_render_deinit();
    sg_shutdown();
}
