// The stress scene (examples/stress) with every entity's position read back each frame,
// three ways, to measure what a getter costs (?mode=, default obj):
//
//   obj    wgr_sprite3d_get_position(sprite): a new {x, y, z} per call
//   into   wgr_sprite3d_get_position(sprite, v): fills a vector the caller owns
//   view   a Float32Array onto the binding's one record slot, as librl's scratch
//          buffer: nothing made, nothing copied, valid until the next getter
//   none   no read: the baseline
//   keep   obj, with the result stored on the entity, so it outlives the call (V8
//          can't optimise an escaping object away)
//
// The reads are summed into a field of a plain object (acc.sum), which V8 keeps as an
// unboxed double. A float returned from a function it doesn't inline, or written to a
// module variable, is boxed: 16 bytes a read, ~200 MB/min at 5,000 reads a frame, which
// this benchmark first measured as the getters' own garbage (2026-10-03).
//
// ?n= sets the count (default 2000).
import createWgrHost from '../../wgrender-host.js';
import * as wgr from '../../wgrender.js';
import * as guest from '../../src/guest.js';
import { host as wasm, record } from '../../src/runtime.js';

const mode = new URLSearchParams(location.search).get('mode') || 'obj';
const acc = { sum: 0 };
const owned = { x: 0, y: 0, z: 0 };
let view = null;
let viewBuffer = null;
let viewAt = 0;

/** The view mode, done fairly: the call writes the binding's slot; the view onto it is
 * made again only when the slot moved or memory grew (which detaches every view). */
function positionView(sprite) {
    const at = record(12);
    wasm._wgr_sprite3d_get_position(at, sprite);
    const heap = wasm.HEAPF32;
    if (view === null || viewBuffer !== heap.buffer || viewAt !== at) {
        viewBuffer = heap.buffer;
        viewAt = at;
        view = heap.subarray(at >> 2, (at >> 2) + 3);
    }
    return view;
}

function readBack(e) {
    if (mode === 'none') {
        return;
    } else if (mode === 'into') {
        acc.sum += wgr.wgr_sprite3d_get_position(e.sprite, owned).x;
    } else if (mode === 'view') {
        acc.sum += positionView(e.sprite)[0];
    } else if (mode === 'keep') {
        e.at = wgr.wgr_sprite3d_get_position(e.sprite);
        acc.sum += e.at.x;
    } else {
        acc.sum += wgr.wgr_sprite3d_get_position(e.sprite).x;
    }
}

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
        angle: 0, spin: (rnd() * 2 - 1) * 3, life: 2 + rnd() * 4, sprite, at: null,
    };
}

function init() {
    wgr.wgr_asset_set_host('../../assets');
    wgr.wgr_logger_set_level(wgr.WGR_LOGGER_LEVEL_WARN);
    wgr.wgr_set_target_fps(60);
    rng = 0x92D68CA2 | 0; // 2463534242

    const camera = wgr.wgr_camera3d_create(wgr.WGR_CAMERA3D_PERSPECTIVE);
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
    readBack(e);
    if (e.life <= 0) {
        wgr.wgr_sprite3d_destroy(e.sprite);
        entities[i] = spawn(); // a new object; the old one is garbage
    }
}

function drawText() {
    wgr.wgr_text_draw(`getters (${mode}): ${n} entities, sum ${acc.sum.toFixed(0)}`, 10, 10, 16, wgr.WGR_COLOR_BLACK);
    for (let i = 0; i < Math.min(TEXT_LINES, n); i++) {
        const e = entities[i];
        wgr.wgr_text_draw(`e${i}: ${e.x.toFixed(2)} ${e.y.toFixed(2)} ${e.z.toFixed(2)} life ${e.life.toFixed(2)}`,
            10, 34 + 18 * i, 16, wgr.WGR_COLOR_BLACK);
    }
}

function frame() {
    acc.sum = 0;
    for (let i = 0; i < n; i++) update(i);
    wgr.wgr_render_begin_frame();
    wgr.wgr_render_clear_background(background);
    wgr.wgr_scene_draw(scene);
    drawText();
    wgr.wgr_render_end_frame();
}

const canvas = document.getElementById('canvas');
const host = await createWgrHost({ canvas, print: (t) => console.log(t), printErr: (t) => console.log(t) });
guest.register({ init, frame });
guest.start(host, SCREEN_WIDTH, SCREEN_HEIGHT, 'getters (wgrender host, JS guest)', wgr.WGR_WINDOW_FLAG_WINDOW_RESIZABLE);
