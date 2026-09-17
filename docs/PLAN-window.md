# Plan: Window and monitor control

Status: **phase 1 implemented (2026-09-17)** with decisions 1–3 and 5 as
recommended; window flags (decision 4) are phase 2. See "As built".

## Problem

librl had window size and position and monitor queries (through raylib's RGFW
backend). sokol_app has none of these, so they were the last deferred parity items
(`make parity`: 8 todos). libsk also has two quiet gaps in the window API today:

- `sk_window_get_position` always returns (0, 0).
- The window flags `RESIZABLE`, `UNDECORATED`, `TRANSPARENT`, `HIDDEN` and
  `ALWAYS_RUN` are accepted and ignored.

## What we have

`deps/sokol_utils/sokol_app_utils.h` (squk/sokol_utils, zlib license, vendored with
two marked fixes; see its `VERSION`) adds to sokol_app, on Win32, macOS and X11:

```c
void sapp_set_window_position(int x, int y);   void sapp_get_window_position(int *x, int *y);
void sapp_set_window_size(int w, int h);       void sapp_get_window_size(int *w, int *h);
int  sapp_num_displays(void);                  int  sapp_current_display(void);
void sapp_set_display(int index);              const char *sapp_display_name(int index);
int  sapp_display_width(int index);            int  sapp_display_height(int index);
bool sapp_window_focused(void);                void sapp_set_fullscreen(bool enable);
void sapp_set_swap_interval(int interval);     int  sapp_get_swap_interval(void);
void sapp_set_mouse_position(float x, float y);
```

On web almost all of these do nothing (fullscreen works). There is no display
position; X11 (XRandR), Win32 and macOS each have one, so it's a small addition.

## Proposed API (`include/sk_window.h`)

```c
/* Window, in logical pixels (like sk_window_get_screen_size). False where the
 * platform can't do it (see "Web" and "Linux"); logged once. */
bool   sk_window_set_size(int width, int height);
bool   sk_window_set_position(int x, int y);      /* top-left, desktop coordinates */
vec2_t sk_window_get_position(void);               /* now real; (0, 0) on web */
bool   sk_window_set_fullscreen(bool fullscreen);
bool   sk_window_is_fullscreen(void);
bool   sk_window_is_focused(void);

/* Monitors: 0 .. count - 1. */
int         sk_window_get_monitor_count(void);
int         sk_window_get_monitor(void);                    /* the one the window is on */
bool        sk_window_set_monitor(int monitor);             /* move the window there */
vec2_t      sk_window_get_monitor_size(int monitor);        /* logical pixels */
vec2_t      sk_window_get_monitor_position(int monitor);    /* desktop coordinates */
const char *sk_window_get_monitor_name(int monitor);
```

- librl's `get_monitor_width` and `get_monitor_height` merge into
  `sk_window_get_monitor_size`, like `sk_window_get_screen_size`.
- Fullscreen, focus and monitor names are new (librl only had the fullscreen flag
  at startup). Runtime vsync (`sapp_set_swap_interval`) is left out for now; it
  interacts with frame pacing and `SK_WINDOW_FLAG_VSYNC_OFF` and deserves its own
  small design.
- Mouse warping (`sapp_set_mouse_position`) is left out; it belongs with input.

### Web

- Size: resizes the canvas (its CSS size; sokol follows it). The page layout can
  still override it, so it returns true only when the canvas took the size.
- Position, monitors other than 0: return false / (0, 0). Monitor 0 is the screen
  (`window.screen` size), the name is "".
- Fullscreen: sokol's fullscreen toggle (needs a user gesture in browsers).

### Linux

sokol_app is X11-only, so on Wayland desktops libsk runs through XWayland, where
compositors ignore a program positioning its own window and may ignore resizing.
Calls still return true (X11 accepted them); the window may not move. Documented,
not worked around.

## Window flags

| Flag | Proposal |
|---|---|
| `RESIZABLE` | Honor: without it, the window gets a fixed size (X11 size hints, Win32 style, macOS style mask) |
| `UNDECORATED` | Honor: X11 `_MOTIF_WM_HINTS`, Win32 `WS_POPUP`, macOS borderless style |
| `HIDDEN` | Honor: create unmapped / hidden; add `sk_window_show(bool)` |
| `ALWAYS_RUN` | Remove: sokol_app never pauses on desktop, and browsers throttle background tabs regardless |
| `TRANSPARENT` | Web only (`composite_mode`); desktop needs an alpha framebuffer config from sokol's GL setup. Honor on web, document desktop as unsupported |

The honored flags need platform code next to sokol_app's window creation, in the
vendored utils header (marked `[libsk]`), and run after `sapp_run` has created the
window, so a visible window may flash in its default style first on some platforms.

## Decisions

1. **API as above** (merged monitor size, fullscreen/focus/name added, vsync and
   mouse warp later). Recommend: yes.
2. **Coordinates:** window and monitor sizes in logical pixels (DPI-scaled, like the
   rest of libsk); positions in the OS's desktop coordinates (unscaled on X11 and
   Win32, points on macOS). Recommend: yes; a fully DPI-consistent desktop space
   isn't possible across monitors with different scales.
3. **Unsupported platforms return false** (logged once) rather than pretending.
   Recommend: yes.
4. **Window flags:** honor `RESIZABLE`, `UNDECORATED`, `HIDDEN` (+ `sk_window_show`);
   remove `ALWAYS_RUN`; `TRANSPARENT` web-only. As a second phase after the
   functions. Recommend: yes.
5. **Testing:** Linux (X11 via XWayland here: position may be ignored) and web in
   the browser; Windows and macOS use sokol_utils' code untested by us. A unit test
   on the headless build checks the fallbacks; an example (`examples/window.c`)
   lists monitors and moves/resizes the window with keys. Recommend: yes.

## As built (phase 1)

- `sapp_display_position` added to the vendored header (`[libsk]`; X11 XRandR, Win32,
  macOS with y flipped to top-down).
- Tested with a scratch program calling each function over frames:
  - **Desktop (XWayland on COSMIC, 2 monitors):** monitors, names and positions
    correct (DP-3 1920x1080 at (1920, 0), HDMI-A-1 at (0, 0)); resizing and
    fullscreen work; moving the window and `set_monitor` are ignored by the
    compositor, as expected.
  - **Xvfb (plain X11, no window manager):** moving works, `set_monitor` centers the
    window; fullscreen does nothing without a window manager.
  - **Web:** webcheck on WebGL2 and WebGPU (the `window` example starts cleanly);
    keys aren't exercised automatically.
  - Windows and macOS: sokol_utils' code, untested by us.
- The same test found a desktop quit abort in sokol_audio (the device callback
  called `saudio_sample_rate` during shutdown), fixed in `sk_audio.c`.

## Order

1. Functions (decisions 1–3) with the `window` example; parity map updated.
2. Window flags (decision 4).
