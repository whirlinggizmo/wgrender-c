// The binding's declarations, checked by TypeScript (tsc --noEmit --strict): what a
// program should be able to write compiles, and each @ts-expect-error line is a
// mistake the types must catch. Not run, only checked.
import * as wgr from '../wgrender.js';
import * as guest from '../src/guest.js';

export function frame(dt: number): void {
    const model: wgr.wgr_handle_t = wgr.wgr_model_create(0);
    const moved: boolean = wgr.wgr_model_set_position(model, 1, 2, 3);
    const where: wgr.vec3_t = wgr.wgr_model_get_position(model);
    const x: number = where.x + where.y + where.z + dt;

    const mouse = wgr.wgr_input_get_mouse_state();
    const left: number = mouse.left + mouse.buttons[0];

    const status: wgr.wgr_resource_status_t = wgr.wgr_resource_get_status(model);
    const ready: boolean = status === wgr.WGR_RESOURCE_READY;

    const width: number = wgr.wgr_text_measure('hello', 20);
    wgr.wgr_render_clear_background(wgr.WGR_COLOR_RAYWHITE);

    // @ts-expect-error a string is not a handle
    wgr.wgr_model_set_position('model', 1, 2, 3);
    // @ts-expect-error too few arguments
    wgr.wgr_model_set_position(model, 1, 2);
    // @ts-expect-error a vec3_t is read-only
    where.x = 4;
    // @ts-expect-error no such field
    mouse.center;
    // @ts-expect-error 42 is not a wgr_resource_status_t
    const wrong: wgr.wgr_resource_status_t = 42;

    void [moved, x, left, ready, width, wrong];
}

guest.register({ frame: (dt, frameId) => frame(dt + frameId) });
