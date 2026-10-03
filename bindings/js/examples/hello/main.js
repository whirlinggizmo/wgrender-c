// wgrender's hello as a JS guest: a window, 2D shapes, text and the mouse.
// A port of examples/hello.c (and of the Haxe binding's examples/hello).
import createWgrHost from '../../wgrender-host.js';
import * as wgr from '../../wgrender.js';
import * as guest from '../../src/guest.js';

const SCREEN_WIDTH = 800;
const SCREEN_HEIGHT = 600;

function frame(dt) {
    const mouse = wgr.wgr_input_get_mouse_state();

    wgr.wgr_render_begin_frame();
    wgr.wgr_render_clear_background(wgr.WGR_COLOR_RAYWHITE);

    // filled + outlined rectangles
    wgr.wgr_shape2d_draw_rectangle(40, 40, 200, 120, wgr.WGR_COLOR_SKYBLUE);
    wgr.wgr_shape2d_draw_rectangle_lines(40, 40, 200, 120, wgr.WGR_COLOR_DARKBLUE);

    // line + triangle + circles
    wgr.wgr_shape2d_draw_line(40, 200, 240, 320, wgr.WGR_COLOR_RED);
    wgr.wgr_shape2d_draw_triangle(320, 60, 280, 180, 360, 180, wgr.WGR_COLOR_GOLD);
    wgr.wgr_shape2d_draw_circle(440, 120, 60, wgr.WGR_COLOR_PURPLE);
    wgr.wgr_shape2d_draw_circle_lines(440, 120, 60, wgr.WGR_COLOR_BLACK);

    // a marker that follows the mouse
    wgr.wgr_shape2d_draw_circle(mouse.x, mouse.y, 8, wgr.WGR_COLOR_MAROON);

    wgr.wgr_text_draw('wgrender + sokol, from JS', 40, 360, 32, wgr.WGR_COLOR_DARKGRAY);
    wgr.wgr_text_draw_fps(40, 12);

    wgr.wgr_render_end_frame();
}

const canvas = document.getElementById('canvas');
const host = await createWgrHost({ canvas, print: (t) => console.log(t), printErr: (t) => console.log(t) });
guest.register({ frame });
guest.start(host, SCREEN_WIDTH, SCREEN_HEIGHT, 'hello (wgrender host, JS guest)',
    wgr.WGR_WINDOW_FLAG_MSAA_4X_HINT | wgr.WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
