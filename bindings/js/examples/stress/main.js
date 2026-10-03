// wgrender's stress scene (tools/bench/stress.c) as a JS guest: N entities updated every
// frame, a steady churn of them dying and being replaced, and a screenful of formatted
// text. It follows the C's spec line for line, as the Haxe port does, and is written the
// way JS naturally would be: an entity is an object, and a replaced one is a new object
// for the GC to collect. ?n= sets the count (default 2000).
import createWgrHost from '../../wgrender-host.js';
import * as wgr from '../../wgrender.js';
import * as guest from '../../src/guest.js';

const SCREEN_WIDTH = 1024;
const SCREEN_HEIGHT = 1280;
const SPRITE_PATH = 'sprites/logo/wg-logo-bw-alpha.png';
const DEFAULT_N = 2000;
const STEP = 1 / 60;
const BOX = 10;
const TEXT_LINES = 48;

const given = parseInt(new URLSearchParams(location.search).get('n'), 10);
const n = given > 0 ? given : DEFAULT_N;
let rng = 0;
let entities = null;
let texture = 0;
let scene = 0;
let camera = 0;
let background = 0;

/** xorshift32, as the spec gives it: logical shifts on 32 bits. */
function rnd() {
    let x = rng;
    x ^= x << 13;
    x ^= x >>> 17;
    x ^= x << 5;
    rng = x;
    return (x >>> 8) / 16777216;
}

function spawn() {
    const sprite = wgr.wgr_sprite3d_create(texture);
    wgr.wgr_sprite3d_set_facing(sprite, wgr.WGR_SPRITE3D_FACING_FREE);
    wgr.wgr_scene_add(scene, sprite, 0);
    return {
        x: (rnd() * 2 - 1) * BOX / 2, y: 1 + rnd() * 4, z: (rnd() * 2 - 1) * BOX / 2,
        vx: (rnd() * 2 - 1) * 4, vy: 4 + rnd() * 6, vz: (rnd() * 2 - 1) * 4,
        angle: 0, spin: (rnd() * 2 - 1) * 3, life: 2 + rnd() * 4, sprite,
    };
}

function init() {
    wgr.wgr_asset_set_host('../../assets');
    wgr.wgr_logger_set_level(wgr.WGR_LOGGER_LEVEL_WARN);
    wgr.wgr_set_target_fps(60);
    rng = 0x92D68CA2 | 0; // 2463534242

    camera = wgr.wgr_camera3d_create(wgr.WGR_CAMERA3D_PERSPECTIVE);
    wgr.wgr_camera3d_set_view(camera, 0, 14, 30, 0, 3, 0, 0, 1, 0);
    scene = wgr.wgr_scene_create();
    wgr.wgr_scene_set_active_camera(scene, camera);
    background = wgr.wgr_color_rgba(245, 245, 245, 255);

    texture = wgr.wgr_texture_create(SPRITE_PATH); // the sprites are drawn once it's loaded
    entities = Array.from({ length: n }, spawn);
}

function update(i) {
    const e = entities[i];
    e.vy -= 9.8 * STEP;
    e.x += e.vx * STEP;
    e.y += e.vy * STEP;
    e.z += e.vz * STEP;
    if (e.y < 0) {
        e.y = 0;
        e.vy = -e.vy * 0.8;
    }
    if (Math.abs(e.x) > BOX) {
        e.x = e.x > 0 ? BOX : -BOX;
        e.vx = -e.vx;
    }
    if (Math.abs(e.z) > BOX) {
        e.z = e.z > 0 ? BOX : -BOX;
        e.vz = -e.vz;
    }
    e.angle += e.spin * STEP;
    e.life -= STEP;
    wgr.wgr_sprite3d_set_transform(e.sprite, e.x, e.y, e.z, 0, e.angle, 0, 0.5, 0.5, 0.5);
    if (e.life <= 0) {
        wgr.wgr_sprite3d_destroy(e.sprite);
        entities[i] = spawn(); // a new object; the old one is garbage
    }
}

function drawText() {
    wgr.wgr_text_draw(`stress: ${n} entities`, 10, 10, 16, wgr.WGR_COLOR_BLACK);
    for (let i = 0; i < Math.min(TEXT_LINES, n); i++) {
        const e = entities[i];
        wgr.wgr_text_draw(`e${i}: ${e.x.toFixed(2)} ${e.y.toFixed(2)} ${e.z.toFixed(2)} life ${e.life.toFixed(2)}`,
            10, 34 + 18 * i, 16, wgr.WGR_COLOR_BLACK);
    }
}

const screen = { x: 0, y: 0 }; // the screen size, filled each frame (no garbage)
const FOV = Math.PI / 4; // the camera's vertical field of view on a screen at least as wide as tall

/** On a portrait screen (a phone held upright), widen the vertical field of view so the
 * horizontal one stays what a square screen shows: the scene keeps its width instead of
 * looking zoomed in. Landscape keeps FOV. */
function fitCamera() {
    wgr.wgr_window_get_screen_size(screen);
    const aspect = screen.y > 0 ? screen.x / screen.y : 1;
    wgr.wgr_camera3d_set_fov(camera, aspect >= 1 ? FOV : 2 * Math.atan(Math.tan(FOV / 2) / aspect));
}

function frame() {
    for (let i = 0; i < n; i++) update(i);
    fitCamera();
    wgr.wgr_render_begin_frame();
    wgr.wgr_render_clear_background(background);
    wgr.wgr_scene_draw(scene);
    drawText();
    // the frame rate, top right: one draw beyond the stress spec (tools/bench/stress.c)
    wgr.wgr_text_draw_fps(screen.x - 110, 10);
    wgr.wgr_render_end_frame();
}

const canvas = document.getElementById('canvas');
const host = await createWgrHost({ canvas, print: (t) => console.log(t), printErr: (t) => console.log(t) });
guest.register({ init, frame });
guest.start(host, SCREEN_WIDTH, SCREEN_HEIGHT, 'stress (wgrender host, JS guest)', wgr.WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
