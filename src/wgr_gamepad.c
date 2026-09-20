#include "wgr_input.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/wgr_gamepad.h"
#include "internal/wgr_internal.h"
#include "internal/wgr_module.h"
#include "wgr_logger.h"

/* Gamepads: each platform reports its pads in one shape (wgr_pad_t: buttons by
 * position, sticks -1..1 with y down, triggers 0..1), polled at the start of each
 * frame; edges come from comparing with the previous poll, recorded twice like the
 * keyboard's (frame and tick, wgr_input.c). Slots are the platform's: kept while a pad
 * is connected, the lowest free one for a new pad.
 *
 *   web      the Gamepad API (the "standard" mapping; others as listed)
 *   Linux    evdev: /dev/input/event* devices with gamepad buttons, rescanned for
 *            new ones every couple of seconds
 *   Windows  XInput, loaded at runtime (written, not yet tested)
 *   others   none yet (macOS: the GameController framework); headless: none */

#define RESCAN_SECONDS 2.0
#define NAME_SIZE 64

typedef struct {
    bool connected;
    char name[NAME_SIZE];
    bool buttons[WGR_GAMEPAD_BUTTON_COUNT];
    float axes[WGR_GAMEPAD_AXIS_COUNT];
} wgr_pad_t;

typedef struct {
    bool pressed[WGR_GAMEPAD_BUTTON_COUNT];
    bool released[WGR_GAMEPAD_BUTTON_COUNT];
} wgr_pad_edges_t;

static struct {
    wgr_pad_t raw[WGR_INPUT_MAX_GAMEPADS];  /* as the platform reports them (kept: evdev sends changes) */
    wgr_pad_t pads[WGR_INPUT_MAX_GAMEPADS]; /* as the program sees them, this frame */
    wgr_pad_edges_t frame_edges[WGR_INPUT_MAX_GAMEPADS];
    wgr_pad_edges_t tick_edges[WGR_INPUT_MAX_GAMEPADS];
    float deadzone;
    bool testing; /* wgr_gamepad_set_test_pad: the platform is ignored */
    wgr_pad_t test_pads[WGR_INPUT_MAX_GAMEPADS];
} wgr_gp;

static void normalize(float *value, float lo, float hi)
{
    *value = *value < lo ? lo : (*value > hi ? hi : *value);
}

/* ------------------------------------------------------------ platforms ---- */

#if defined(WGR_HEADLESS)

static void platform_open(void) {}
static void platform_close(void) {}
static void platform_poll(wgr_pad_t pads[WGR_INPUT_MAX_GAMEPADS]) { (void)pads; }

#elif defined(__EMSCRIPTEN__)
#include <emscripten.h>

/* Per pad: bit i of the result says pad i is connected; axes (6 floats), buttons
 * (17 ints) and name (NAME_SIZE bytes) written at the given addresses. The standard
 * mapping lists buttons as: south east west north, bumpers, triggers, back start,
 * sticks, d-pad up down left right, guide; axes as left x y, right x y (y down). */
EM_JS(int, web_poll_gamepads, (float *axes, int *buttons, char *names, int pads, int name_size), {
    const list = navigator.getGamepads ? navigator.getGamepads() : [];
    const order = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 16, 10, 11, 12, 13, 14, 15]; /* ours from theirs */
    let connected = 0;
    for (let i = 0; i < pads && i < list.length; i++) {
        const pad = list[i];
        if (!pad || !pad.connected) continue;
        connected |= 1 << i;
        for (let b = 0; b < 17; b++) {
            const button = pad.buttons[order[b]];
            HEAP32[(buttons >> 2) + i * 17 + b] = button && button.pressed ? 1 : 0;
        }
        for (let a = 0; a < 4; a++) {
            HEAPF32[(axes >> 2) + i * 6 + a] = a < pad.axes.length ? pad.axes[a] : 0;
        }
        HEAPF32[(axes >> 2) + i * 6 + 4] = pad.buttons[6] ? pad.buttons[6].value : 0;
        HEAPF32[(axes >> 2) + i * 6 + 5] = pad.buttons[7] ? pad.buttons[7].value : 0;
        stringToUTF8(pad.id, names + i * name_size, name_size);
    }
    return connected;
});

static void platform_open(void) {}
static void platform_close(void) {}

static void platform_poll(wgr_pad_t pads[WGR_INPUT_MAX_GAMEPADS])
{
    static float axes[WGR_INPUT_MAX_GAMEPADS * 6];
    static int buttons[WGR_INPUT_MAX_GAMEPADS * 17];
    static char names[WGR_INPUT_MAX_GAMEPADS * NAME_SIZE];
    const int connected = web_poll_gamepads(axes, buttons, names, WGR_INPUT_MAX_GAMEPADS, NAME_SIZE);
    for (int i = 0; i < WGR_INPUT_MAX_GAMEPADS; i++) {
        pads[i].connected = (connected >> i) & 1;
        if (!pads[i].connected) continue;
        for (int b = 0; b < WGR_GAMEPAD_BUTTON_COUNT; b++) pads[i].buttons[b] = buttons[i * 17 + b] != 0;
        for (int a = 0; a < WGR_GAMEPAD_AXIS_COUNT; a++) pads[i].axes[a] = axes[i * 6 + a];
        memcpy(pads[i].name, &names[i * NAME_SIZE], NAME_SIZE);
        pads[i].name[NAME_SIZE - 1] = '\0';
    }
}

#elif defined(__linux__)
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define LONG_BITS (sizeof(long) * CHAR_BIT)
#define HAS_BIT(bits, n) (((bits)[(n) / LONG_BITS] >> ((n) % LONG_BITS)) & 1UL)

typedef struct {
    int fd; /* -1: the slot is free */
    char path[288];
    bool xpad;           /* its X and Y buttons use BTN_NORTH's and BTN_WEST's codes */
    bool right_on_z;     /* no RX/RY: the right stick is on Z/RZ (generic HID pads) */
    struct input_absinfo abs[ABS_HAT0Y + 1];
} wgr_evdev_t;

static wgr_evdev_t wgr_evdevs[WGR_INPUT_MAX_GAMEPADS];
static double wgr_next_scan;

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static float abs_value(const wgr_evdev_t *dev, int code, int value, bool trigger)
{
    const struct input_absinfo *info = &dev->abs[code];
    const float range = (float)(info->maximum - info->minimum);
    float t;
    if (range <= 0.0f) return 0.0f;
    t = (float)(value - info->minimum) / range;
    return trigger ? t : t * 2.0f - 1.0f;
}

static int button_of(const wgr_evdev_t *dev, int code)
{
    switch (code) {
        case BTN_SOUTH: return WGR_GAMEPAD_BUTTON_SOUTH;
        case BTN_EAST: return WGR_GAMEPAD_BUTTON_EAST;
        case BTN_NORTH: return dev->xpad ? WGR_GAMEPAD_BUTTON_WEST : WGR_GAMEPAD_BUTTON_NORTH; /* BTN_X */
        case BTN_WEST: return dev->xpad ? WGR_GAMEPAD_BUTTON_NORTH : WGR_GAMEPAD_BUTTON_WEST;  /* BTN_Y */
        case BTN_TL: return WGR_GAMEPAD_BUTTON_LEFT_BUMPER;
        case BTN_TR: return WGR_GAMEPAD_BUTTON_RIGHT_BUMPER;
        case BTN_TL2: return WGR_GAMEPAD_BUTTON_LEFT_TRIGGER;
        case BTN_TR2: return WGR_GAMEPAD_BUTTON_RIGHT_TRIGGER;
        case BTN_SELECT: return WGR_GAMEPAD_BUTTON_BACK;
        case BTN_START: return WGR_GAMEPAD_BUTTON_START;
        case BTN_MODE: return WGR_GAMEPAD_BUTTON_GUIDE;
        case BTN_THUMBL: return WGR_GAMEPAD_BUTTON_LEFT_STICK;
        case BTN_THUMBR: return WGR_GAMEPAD_BUTTON_RIGHT_STICK;
        case BTN_DPAD_UP: return WGR_GAMEPAD_BUTTON_DPAD_UP;
        case BTN_DPAD_DOWN: return WGR_GAMEPAD_BUTTON_DPAD_DOWN;
        case BTN_DPAD_LEFT: return WGR_GAMEPAD_BUTTON_DPAD_LEFT;
        case BTN_DPAD_RIGHT: return WGR_GAMEPAD_BUTTON_DPAD_RIGHT;
        default: return -1;
    }
}

static void apply_abs(const wgr_evdev_t *dev, wgr_pad_t *pad, int code, int value)
{
    switch (code) {
        case ABS_X: pad->axes[WGR_GAMEPAD_AXIS_LEFT_X] = abs_value(dev, code, value, false); break;
        case ABS_Y: pad->axes[WGR_GAMEPAD_AXIS_LEFT_Y] = abs_value(dev, code, value, false); break;
        case ABS_RX: pad->axes[WGR_GAMEPAD_AXIS_RIGHT_X] = abs_value(dev, code, value, false); break;
        case ABS_RY: pad->axes[WGR_GAMEPAD_AXIS_RIGHT_Y] = abs_value(dev, code, value, false); break;
        case ABS_Z:
            if (dev->right_on_z) pad->axes[WGR_GAMEPAD_AXIS_RIGHT_X] = abs_value(dev, code, value, false);
            else pad->axes[WGR_GAMEPAD_AXIS_LEFT_TRIGGER] = abs_value(dev, code, value, true);
            break;
        case ABS_RZ:
            if (dev->right_on_z) pad->axes[WGR_GAMEPAD_AXIS_RIGHT_Y] = abs_value(dev, code, value, false);
            else pad->axes[WGR_GAMEPAD_AXIS_RIGHT_TRIGGER] = abs_value(dev, code, value, true);
            break;
        case ABS_BRAKE: pad->axes[WGR_GAMEPAD_AXIS_LEFT_TRIGGER] = abs_value(dev, code, value, true); break;
        case ABS_GAS: pad->axes[WGR_GAMEPAD_AXIS_RIGHT_TRIGGER] = abs_value(dev, code, value, true); break;
        case ABS_HAT0X:
            pad->buttons[WGR_GAMEPAD_BUTTON_DPAD_LEFT] = value < 0;
            pad->buttons[WGR_GAMEPAD_BUTTON_DPAD_RIGHT] = value > 0;
            break;
        case ABS_HAT0Y:
            pad->buttons[WGR_GAMEPAD_BUTTON_DPAD_UP] = value < 0;
            pad->buttons[WGR_GAMEPAD_BUTTON_DPAD_DOWN] = value > 0;
            break;
        default: break;
    }
}

static bool is_open(const char *path)
{
    for (int i = 0; i < WGR_INPUT_MAX_GAMEPADS; i++) {
        if (wgr_evdevs[i].fd >= 0 && strcmp(wgr_evdevs[i].path, path) == 0) return true;
    }
    return false;
}

/* The kernel driver behind /dev/input/eventN, from sysfs: "xpad" for Xbox pads. */
static bool driver_is(const char *event_name, const char *driver)
{
    char link[320], target[256];
    ssize_t n;
    snprintf(link, sizeof(link), "/sys/class/input/%s/device/device/driver", event_name);
    n = readlink(link, target, sizeof(target) - 1);
    if (n <= 0) return false;
    target[n] = '\0';
    const char *base = strrchr(target, '/');
    return strcmp(base != NULL ? base + 1 : target, driver) == 0;
}

/* Open a device if it's a gamepad and a slot is free; its current state goes into the
 * slot's pad (buttons held and sticks already off-center count from the start). */
static void try_open(const char *event_name, wgr_pad_t pads[WGR_INPUT_MAX_GAMEPADS])
{
    unsigned long key_bits[(KEY_MAX + LONG_BITS) / LONG_BITS] = {0};
    unsigned long abs_bits[(ABS_MAX + LONG_BITS) / LONG_BITS] = {0};
    unsigned long keys_down[(KEY_MAX + LONG_BITS) / LONG_BITS] = {0};
    char path[288];
    int slot = -1, fd;

    snprintf(path, sizeof(path), "/dev/input/%s", event_name);
    if (is_open(path)) return;
    for (int i = 0; i < WGR_INPUT_MAX_GAMEPADS && slot < 0; i++) {
        if (wgr_evdevs[i].fd < 0) slot = i;
    }
    if (slot < 0) return;
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0 || !HAS_BIT(key_bits, BTN_GAMEPAD)) {
        close(fd); /* not a gamepad (keyboards, mice, ...) */
        return;
    }
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);

    wgr_evdev_t *dev = &wgr_evdevs[slot];
    wgr_pad_t *pad = &pads[slot];
    memset(dev, 0, sizeof(*dev));
    memset(pad, 0, sizeof(*pad));
    dev->fd = fd;
    snprintf(dev->path, sizeof(dev->path), "%s", path);
    dev->xpad = driver_is(event_name, "xpad");
    dev->right_on_z = !HAS_BIT(abs_bits, ABS_RX) && HAS_BIT(abs_bits, ABS_Z) && HAS_BIT(abs_bits, ABS_RZ);
    if (ioctl(fd, EVIOCGNAME(sizeof(pad->name)), pad->name) < 0) {
        snprintf(pad->name, sizeof(pad->name), "gamepad");
    }
    pad->name[NAME_SIZE - 1] = '\0';
    for (int code = 0; code <= ABS_HAT0Y; code++) {
        if (HAS_BIT(abs_bits, code) && ioctl(fd, EVIOCGABS(code), &dev->abs[code]) == 0) {
            apply_abs(dev, pad, code, dev->abs[code].value);
        }
    }
    if (ioctl(fd, EVIOCGKEY(sizeof(keys_down)), keys_down) >= 0) {
        for (int code = BTN_MISC; code <= BTN_DPAD_RIGHT; code++) {
            const int button = button_of(dev, code);
            if (button >= 0 && HAS_BIT(keys_down, code)) pad->buttons[button] = true;
        }
    }
    pad->connected = true;
    log_info("gamepad %d: %s (%s)", slot, pad->name, path);
}

static void rescan(wgr_pad_t pads[WGR_INPUT_MAX_GAMEPADS])
{
    DIR *dir = opendir("/dev/input");
    struct dirent *entry;
    if (dir == NULL) return;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) == 0) try_open(entry->d_name, pads);
    }
    closedir(dir);
}

static void close_slot(int slot, wgr_pad_t pads[WGR_INPUT_MAX_GAMEPADS])
{
    if (wgr_evdevs[slot].fd >= 0) {
        close(wgr_evdevs[slot].fd);
        log_info("gamepad %d: disconnected", slot);
    }
    wgr_evdevs[slot].fd = -1;
    memset(&pads[slot], 0, sizeof(pads[slot]));
}

static void platform_open(void)
{
    for (int i = 0; i < WGR_INPUT_MAX_GAMEPADS; i++) wgr_evdevs[i].fd = -1;
    wgr_next_scan = 0.0; /* the first poll scans */
}

static void platform_close(void)
{
    for (int i = 0; i < WGR_INPUT_MAX_GAMEPADS; i++) {
        if (wgr_evdevs[i].fd >= 0) close(wgr_evdevs[i].fd);
        wgr_evdevs[i].fd = -1;
    }
}

static void platform_poll(wgr_pad_t pads[WGR_INPUT_MAX_GAMEPADS])
{
    const double now = now_seconds();
    if (now >= wgr_next_scan) {
        rescan(pads);
        wgr_next_scan = now + RESCAN_SECONDS;
    }
    for (int slot = 0; slot < WGR_INPUT_MAX_GAMEPADS; slot++) {
        wgr_evdev_t *dev = &wgr_evdevs[slot];
        struct input_event events[64];
        if (dev->fd < 0) continue;
        for (;;) {
            const ssize_t n = read(dev->fd, events, sizeof(events));
            if (n < 0) {
                if (errno != EAGAIN && errno != EINTR) close_slot(slot, pads); /* ENODEV: unplugged */
                break;
            }
            for (int e = 0; e < (int)(n / (ssize_t)sizeof(events[0])); e++) {
                const struct input_event *ev = &events[e];
                if (ev->type == EV_KEY) {
                    const int button = button_of(dev, ev->code);
                    if (button >= 0) pads[slot].buttons[button] = ev->value != 0;
                } else if (ev->type == EV_ABS && ev->code <= ABS_HAT0Y) {
                    apply_abs(dev, &pads[slot], ev->code, ev->value);
                }
            }
            if (n < (ssize_t)sizeof(events)) break;
        }
    }
}

#elif defined(_WIN32)
#include <windows.h>

/* XInput's own types (xinput.h isn't in every toolchain), loaded at runtime so the
 * program doesn't depend on a particular xinput DLL. */
typedef struct {
    WORD buttons;
    BYTE left_trigger, right_trigger;
    SHORT lx, ly, rx, ry;
} wgr_xinput_gamepad_t;
typedef struct {
    DWORD packet;
    wgr_xinput_gamepad_t gamepad;
} wgr_xinput_state_t;
typedef DWORD(WINAPI *wgr_xinput_get_state_fn)(DWORD, wgr_xinput_state_t *);

static HMODULE wgr_xinput;
static wgr_xinput_get_state_fn wgr_xinput_get_state;
static DWORD wgr_next_empty_check[WGR_INPUT_MAX_GAMEPADS]; /* asking about an empty slot is slow */

static void platform_open(void)
{
    static const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (int i = 0; i < 3 && wgr_xinput == NULL; i++) wgr_xinput = LoadLibraryA(dlls[i]);
    if (wgr_xinput != NULL) {
        wgr_xinput_get_state = (wgr_xinput_get_state_fn)(void *)GetProcAddress(wgr_xinput, "XInputGetState");
    }
    memset(wgr_next_empty_check, 0, sizeof(wgr_next_empty_check));
}

static void platform_close(void)
{
    if (wgr_xinput != NULL) FreeLibrary(wgr_xinput);
    wgr_xinput = NULL;
    wgr_xinput_get_state = NULL;
}

static float stick(SHORT value, bool flip)
{
    const float v = (float)value / 32767.0f;
    return flip ? -v : v;
}

static void platform_poll(wgr_pad_t pads[WGR_INPUT_MAX_GAMEPADS])
{
    static const struct {
        WORD mask;
        int button;
    } map[] = {
        {0x1000, WGR_GAMEPAD_BUTTON_SOUTH},      {0x2000, WGR_GAMEPAD_BUTTON_EAST},
        {0x4000, WGR_GAMEPAD_BUTTON_WEST},       {0x8000, WGR_GAMEPAD_BUTTON_NORTH},
        {0x0100, WGR_GAMEPAD_BUTTON_LEFT_BUMPER}, {0x0200, WGR_GAMEPAD_BUTTON_RIGHT_BUMPER},
        {0x0020, WGR_GAMEPAD_BUTTON_BACK},       {0x0010, WGR_GAMEPAD_BUTTON_START},
        {0x0040, WGR_GAMEPAD_BUTTON_LEFT_STICK}, {0x0080, WGR_GAMEPAD_BUTTON_RIGHT_STICK},
        {0x0001, WGR_GAMEPAD_BUTTON_DPAD_UP},    {0x0002, WGR_GAMEPAD_BUTTON_DPAD_DOWN},
        {0x0004, WGR_GAMEPAD_BUTTON_DPAD_LEFT},  {0x0008, WGR_GAMEPAD_BUTTON_DPAD_RIGHT},
    };
    const DWORD now = GetTickCount();
    if (wgr_xinput_get_state == NULL) return;
    for (int i = 0; i < WGR_INPUT_MAX_GAMEPADS; i++) {
        wgr_xinput_state_t state;
        if (!pads[i].connected && (LONG)(now - wgr_next_empty_check[i]) < 0) continue;
        if (wgr_xinput_get_state((DWORD)i, &state) != ERROR_SUCCESS) {
            memset(&pads[i], 0, sizeof(pads[i]));
            wgr_next_empty_check[i] = now + (DWORD)(RESCAN_SECONDS * 1000.0);
            continue;
        }
        pads[i].connected = true;
        snprintf(pads[i].name, sizeof(pads[i].name), "XInput controller %d", i + 1);
        for (size_t b = 0; b < sizeof(map) / sizeof(map[0]); b++) {
            pads[i].buttons[map[b].button] = (state.gamepad.buttons & map[b].mask) != 0;
        }
        pads[i].axes[WGR_GAMEPAD_AXIS_LEFT_X] = stick(state.gamepad.lx, false);
        pads[i].axes[WGR_GAMEPAD_AXIS_LEFT_Y] = stick(state.gamepad.ly, true); /* XInput: up is positive */
        pads[i].axes[WGR_GAMEPAD_AXIS_RIGHT_X] = stick(state.gamepad.rx, false);
        pads[i].axes[WGR_GAMEPAD_AXIS_RIGHT_Y] = stick(state.gamepad.ry, true);
        pads[i].axes[WGR_GAMEPAD_AXIS_LEFT_TRIGGER] = (float)state.gamepad.left_trigger / 255.0f;
        pads[i].axes[WGR_GAMEPAD_AXIS_RIGHT_TRIGGER] = (float)state.gamepad.right_trigger / 255.0f;
    }
}

#else /* macOS and others: none yet */

static void platform_open(void) {}
static void platform_close(void) {}
static void platform_poll(wgr_pad_t pads[WGR_INPUT_MAX_GAMEPADS]) { (void)pads; }

#endif

/* ------------------------------------------------------------ frames ---- */

void wgr_gamepad_init(void)
{
    memset(&wgr_gp, 0, sizeof(wgr_gp));
    wgr_gp.deadzone = 0.15f;
    platform_open();
}

void wgr_gamepad_deinit(void)
{
    platform_close();
    memset(&wgr_gp, 0, sizeof(wgr_gp));
}

/* Poll, then record what changed since the last poll in both edge sets. A pad that
 * went away releases what it held. */
void wgr_gamepad_begin_frame(void)
{
    wgr_pad_t next[WGR_INPUT_MAX_GAMEPADS];
    if (!wgr_gp.testing) platform_poll(wgr_gp.raw);
    memcpy(next, wgr_gp.testing ? wgr_gp.test_pads : wgr_gp.raw, sizeof(next));

    for (int i = 0; i < WGR_INPUT_MAX_GAMEPADS; i++) {
        wgr_pad_t *pad = &next[i];
        if (!pad->connected) {
            memset(pad, 0, sizeof(*pad));
        } else {
            for (int a = 0; a < WGR_GAMEPAD_AXIS_COUNT; a++) {
                normalize(&pad->axes[a], a >= WGR_GAMEPAD_AXIS_LEFT_TRIGGER ? 0.0f : -1.0f, 1.0f);
            }
            /* triggers are buttons too, past half-way (whatever the platform said) */
            pad->buttons[WGR_GAMEPAD_BUTTON_LEFT_TRIGGER] =
                pad->buttons[WGR_GAMEPAD_BUTTON_LEFT_TRIGGER] || pad->axes[WGR_GAMEPAD_AXIS_LEFT_TRIGGER] > 0.5f;
            pad->buttons[WGR_GAMEPAD_BUTTON_RIGHT_TRIGGER] =
                pad->buttons[WGR_GAMEPAD_BUTTON_RIGHT_TRIGGER] || pad->axes[WGR_GAMEPAD_AXIS_RIGHT_TRIGGER] > 0.5f;
        }
        for (int b = 0; b < WGR_GAMEPAD_BUTTON_COUNT; b++) {
            const bool was = wgr_gp.pads[i].buttons[b], is = pad->buttons[b];
            if (is && !was) wgr_gp.frame_edges[i].pressed[b] = wgr_gp.tick_edges[i].pressed[b] = true;
            if (!is && was) wgr_gp.frame_edges[i].released[b] = wgr_gp.tick_edges[i].released[b] = true;
        }
    }
    memcpy(wgr_gp.pads, next, sizeof(next));
}

void wgr_gamepad_end_tick(void)
{
    memset(wgr_gp.tick_edges, 0, sizeof(wgr_gp.tick_edges));
}

void wgr_gamepad_frame_done(void)
{
    memset(wgr_gp.frame_edges, 0, sizeof(wgr_gp.frame_edges));
}

void wgr_gamepad_set_test_pad(int pad, bool connected, const char *name, const bool buttons[WGR_GAMEPAD_BUTTON_COUNT],
                             const float axes[WGR_GAMEPAD_AXIS_COUNT])
{
    wgr_pad_t *p;
    if (pad < 0 || pad >= WGR_INPUT_MAX_GAMEPADS) return;
    wgr_gp.testing = true;
    p = &wgr_gp.test_pads[pad];
    memset(p, 0, sizeof(*p));
    p->connected = connected;
    snprintf(p->name, sizeof(p->name), "%s", name != NULL ? name : "");
    if (buttons != NULL) memcpy(p->buttons, buttons, sizeof(p->buttons));
    if (axes != NULL) memcpy(p->axes, axes, sizeof(p->axes));
}

/* ------------------------------------------------------------ public API ---- */

static const wgr_pad_t *lookup_pad(int pad)
{
    return pad >= 0 && pad < WGR_INPUT_MAX_GAMEPADS && wgr_gp.pads[pad].connected ? &wgr_gp.pads[pad] : NULL;
}

WGR_KEEP bool wgr_input_is_gamepad_connected(int pad)
{
    return lookup_pad(pad) != NULL;
}

WGR_KEEP const char *wgr_input_get_gamepad_name(int pad)
{
    const wgr_pad_t *pad_ptr = lookup_pad(pad);
    return pad_ptr != NULL ? pad_ptr->name : "";
}

WGR_KEEP int wgr_input_get_gamepad_button(int pad, wgr_gamepad_button_t button)
{
    const wgr_pad_edges_t *edges;
    if (pad < 0 || pad >= WGR_INPUT_MAX_GAMEPADS || button < 0 || button >= WGR_GAMEPAD_BUTTON_COUNT) {
        return WGR_BUTTON_UP;
    }
    /* edges outlive a disconnect by a frame (the release), so read them regardless */
    edges = wgr_input_get_context() == WGR_INPUT_CONTEXT_TICK ? &wgr_gp.tick_edges[pad] : &wgr_gp.frame_edges[pad];
    if (edges->pressed[button]) return WGR_BUTTON_PRESSED;
    if (edges->released[button]) return WGR_BUTTON_RELEASED;
    return wgr_gp.pads[pad].buttons[button] ? WGR_BUTTON_DOWN : WGR_BUTTON_UP;
}

/* A stick past the dead zone, rescaled so it still reaches 1 at the edge. */
static float stick_axis(const wgr_pad_t *pad_ptr, int x_axis, bool want_y)
{
    const float x = pad_ptr->axes[x_axis], y = pad_ptr->axes[x_axis + 1];
    const float length = sqrtf(x * x + y * y);
    float scale;
    if (length <= wgr_gp.deadzone) return 0.0f;
    scale = (length - wgr_gp.deadzone) / (1.0f - wgr_gp.deadzone) / length;
    if (length * scale > 1.0f) scale = 1.0f / length; /* square-ish sticks reach past 1 */
    return (want_y ? y : x) * scale;
}

WGR_KEEP float wgr_input_get_gamepad_axis(int pad, wgr_gamepad_axis_t axis)
{
    const wgr_pad_t *pad_ptr = lookup_pad(pad);
    if (pad_ptr == NULL || axis < 0 || axis >= WGR_GAMEPAD_AXIS_COUNT) return 0.0f;
    switch (axis) {
        case WGR_GAMEPAD_AXIS_LEFT_X: return stick_axis(pad_ptr, WGR_GAMEPAD_AXIS_LEFT_X, false);
        case WGR_GAMEPAD_AXIS_LEFT_Y: return stick_axis(pad_ptr, WGR_GAMEPAD_AXIS_LEFT_X, true);
        case WGR_GAMEPAD_AXIS_RIGHT_X: return stick_axis(pad_ptr, WGR_GAMEPAD_AXIS_RIGHT_X, false);
        case WGR_GAMEPAD_AXIS_RIGHT_Y: return stick_axis(pad_ptr, WGR_GAMEPAD_AXIS_RIGHT_X, true);
        default: return pad_ptr->axes[axis];
    }
}

WGR_KEEP bool wgr_input_set_gamepad_deadzone(float radius)
{
    if (!(radius >= 0.0f && radius <= 0.9f)) return false;
    wgr_gp.deadzone = radius;
    return true;
}

/* An optional subsystem: part of the runtime when a program asks about gamepads
 * (internal/wgr_module.h). Polled before the frame's ticks, so they see it too. */
static wgr_module_t wgr_gamepad_module = {.name = "gamepad", .order = 5, .init = wgr_gamepad_init,
                                        .deinit = wgr_gamepad_deinit, .begin_frame = wgr_gamepad_begin_frame,
                                        .end_tick = wgr_gamepad_end_tick, .frame_done = wgr_gamepad_frame_done};
WGR_MODULE(wgr_gamepad_module)
