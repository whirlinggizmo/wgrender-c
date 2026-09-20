# Plan: Fixed-rate tick + frame callback timing arguments

Status: **implemented (2026-09-16).**

## Problem

libwgrender has one callback, `wgr_set_frame(fn)`, called once per rendered frame, with
timing read from the global `wgr_get_delta_time()`. That leaves no good place for
simulation that must be deterministic:

- A variable `dt` makes physics and gameplay depend on the frame rate.
- When frames stall (a hidden or throttled window), `dt` is clamped to 100 ms, so
  game time silently runs slow.
- `wgr_set_target_fps` gets misused as a simulation-rate control. It should only
  be a power cap.

## Design

Two callbacks, named for what they are (frame rate vs tick rate):

```c
typedef void (*wgr_tick_fn)(float dt, void *user_data);                        /* dt = 1/hz, always */
typedef void (*wgr_frame_fn)(float dt, float tick_fraction, void *user_data);  /* once per rendered frame */

void wgr_set_tick(wgr_tick_fn tick_fn, void *user_data, int hz);  /* hz <= 0 or NULL fn: no tick */
void wgr_set_frame(wgr_frame_fn frame_fn, void *user_data);
```

- **tick:** simulation at a fixed rate. Runs 0..N times before each frame, always
  with the same `dt`. Never draws.
- **frame:** variable update plus drawing, once per rendered frame. `dt` is the
  time since the previous frame. `tick_fraction` (0..1) is how far this frame is
  into the next tick, for drawing tick state smoothly:
  `draw_pos = lerp(prev_tick_pos, tick_pos, tick_fraction)`. It is 0 when no tick
  is set.
- **Timing is passed as arguments, not read from globals.** `wgr_get_delta_time()`
  is removed. `wgr_get_time()` (absolute clock) stays.
- Name: `tick_fraction`, not the tutorial term "alpha" (which already means
  transparency in a graphics library). Godot calls it the physics interpolation
  fraction, Bevy the overstep fraction.

### Scheduling

Per rendered frame (after frame pacing, before the frame callback):

```
accumulator += real elapsed time since the previous frame
steps = 0
while accumulator >= step and steps < MAX_TICKS_PER_FRAME (5):
    tick(step); accumulator -= step; steps++
if accumulator >= step:            # still behind after 5 ticks (a stall)
    accumulator = fmod(accumulator, step)   # drop the backlog, keep the phase
tick_fraction = accumulator / step
frame(dt, tick_fraction)
```

- The cap prevents the "spiral of death" (slow ticks causing more ticks). After a
  stall, simulation time falls behind wall time instead of freezing the app.
- The accumulator uses real elapsed time from the monotonic clock, not the
  smoothed or clamped frame `dt`.
- Changing the rate or callback resets the accumulator.
- Web: identical. Ticks run inside the browser's frame callback.
- The scheduling logic is pure (`src/wgr_tick_clock.c`) and unit tested, like
  `wgr_frame_pace`.

### Input edges

Input is read through getters (`wgr_input_get_keyboard_state()`,
`wgr_input_get_mouse_state()`), so "pressed this frame" needs a defined meaning
inside a tick:

- **Edges (pressed / released, mouse and wheel deltas, typed keys and chars) are
  relative to the callback you're in.** In a tick: since the previous tick. In a
  frame: since the previous frame. Held state (`down`) and the mouse position are
  shared.
- So each press is seen **exactly once** by ticks, however many ticks run per
  frame: the first tick after the press sees it, later ticks in the same frame
  don't, and if a frame runs no ticks the press carries over to the next tick.
- Implementation: events update two edge sets. Tick edges clear after each tick;
  frame edges clear after the frame callback. The runtime tells the input module
  which callback is running.
- Precedent: Godot 4's `is_action_just_pressed()` is relative to the physics frame
  inside `_physics_process`. Unity's per-frame `GetKeyDown` inside `FixedUpdate`
  is the classic bug this avoids.

## Changes

- `include/wgr.h`: `wgr_tick_fn`, new `wgr_frame_fn` signature, `wgr_set_tick`; remove
  `wgr_get_delta_time`; document frame vs tick and the input edge rule.
- `src/wgr.c`: tick scheduling in `on_frame`, callback context for input.
- `src/wgr_tick_clock.c` + `src/internal/wgr_tick_clock.h`: pure scheduler.
- `src/wgr_input.c`: separate tick and frame edge sets; getters pick by context.
- Every example's frame callback gets the new signature; `model.c` and
  `simple.c` use the `dt` argument.
- New `examples/tick.c`: an object moving at a 10 Hz tick, drawn raw (visibly
  stepping) next to one drawn with `tick_fraction` (smooth), and a counter that
  increments once per key press inside the tick.
- README loop section, TASKS (resolves the frame-timing decision), parity map
  (`rl_get_delta_time` becomes dropped: timing is passed to callbacks).

## Verification

- Unit tests: tick clock (steps per frame, fraction, max-steps cap and backlog
  drop, rate change, no tick); input edges per context (press seen by exactly one
  tick with 0, 1 and 3 ticks per frame; frame edges unaffected by ticks).
- `examples/tick.c` screenshots on desktop and web; `make test`, `make check`,
  `make webcheck` (WebGL2 + WebGPU).
