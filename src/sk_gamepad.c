#include "sk_input.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "internal/exports.h"
#include "internal/sk_gamepad.h"
#include "internal/sk_internal.h"
#include "internal/sk_module.h"
#include "sk_logger.h"

/* Gamepads: each platform reports its pads in one shape (sk_pad_t: buttons by
 * position, sticks -1..1 with y down, triggers 0..1), polled at the start of each
 * frame; edges come from comparing with the previous poll, recorded twice like the
 * keyboard's (frame and tick, sk_input.c). Slots are the platform's: kept while a pad
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
    bool buttons[SK_GAMEPAD_BUTTON_COUNT];
    float axes[SK_GAMEPAD_AXIS_COUNT];
} sk_pad_t;

typedef struct {
    bool pressed[SK_GAMEPAD_BUTTON_COUNT];
    bool released[SK_GAMEPAD_BUTTON_COUNT];
} sk_pad_edges_t;

static struct {
    sk_pad_t raw[SK_INPUT_MAX_GAMEPADS];  /* as the platform reports them (kept: evdev sends changes) */
    sk_pad_t pads[SK_INPUT_MAX_GAMEPADS]; /* as the program sees them, this frame */
    sk_pad_edges_t frame_edges[SK_INPUT_MAX_GAMEPADS];
    sk_pad_edges_t tick_edges[SK_INPUT_MAX_GAMEPADS];
    float deadzone;
    bool testing; /* sk_gamepad_set_test_pad: the platform is ignored */
    sk_pad_t test_pads[SK_INPUT_MAX_GAMEPADS];
} sk_gp;

static void normalize(float *value, float lo, float hi)
{
    *value = *value < lo ? lo : (*value > hi ? hi : *value);
}

/* ------------------------------------------------------------ platforms ---- */

#if defined(SK_HEADLESS)

static void platform_open(void) {}
static void platform_close(void) {}
static void platform_poll(sk_pad_t pads[SK_INPUT_MAX_GAMEPADS]) { (void)pads; }

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

static void platform_poll(sk_pad_t pads[SK_INPUT_MAX_GAMEPADS])
{
    static float axes[SK_INPUT_MAX_GAMEPADS * 6];
    static int buttons[SK_INPUT_MAX_GAMEPADS * 17];
    static char names[SK_INPUT_MAX_GAMEPADS * NAME_SIZE];
    const int connected = web_poll_gamepads(axes, buttons, names, SK_INPUT_MAX_GAMEPADS, NAME_SIZE);
    for (int i = 0; i < SK_INPUT_MAX_GAMEPADS; i++) {
        pads[i].connected = (connected >> i) & 1;
        if (!pads[i].connected) continue;
        for (int b = 0; b < SK_GAMEPAD_BUTTON_COUNT; b++) pads[i].buttons[b] = buttons[i * 17 + b] != 0;
        for (int a = 0; a < SK_GAMEPAD_AXIS_COUNT; a++) pads[i].axes[a] = axes[i * 6 + a];
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
} sk_evdev_t;

static sk_evdev_t sk_evdevs[SK_INPUT_MAX_GAMEPADS];
static double sk_next_scan;

static double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static float abs_value(const sk_evdev_t *dev, int code, int value, bool trigger)
{
    const struct input_absinfo *info = &dev->abs[code];
    const float range = (float)(info->maximum - info->minimum);
    float t;
    if (range <= 0.0f) return 0.0f;
    t = (float)(value - info->minimum) / range;
    return trigger ? t : t * 2.0f - 1.0f;
}

static int button_of(const sk_evdev_t *dev, int code)
{
    switch (code) {
        case BTN_SOUTH: return SK_GAMEPAD_BUTTON_SOUTH;
        case BTN_EAST: return SK_GAMEPAD_BUTTON_EAST;
        case BTN_NORTH: return dev->xpad ? SK_GAMEPAD_BUTTON_WEST : SK_GAMEPAD_BUTTON_NORTH; /* BTN_X */
        case BTN_WEST: return dev->xpad ? SK_GAMEPAD_BUTTON_NORTH : SK_GAMEPAD_BUTTON_WEST;  /* BTN_Y */
        case BTN_TL: return SK_GAMEPAD_BUTTON_LEFT_BUMPER;
        case BTN_TR: return SK_GAMEPAD_BUTTON_RIGHT_BUMPER;
        case BTN_TL2: return SK_GAMEPAD_BUTTON_LEFT_TRIGGER;
        case BTN_TR2: return SK_GAMEPAD_BUTTON_RIGHT_TRIGGER;
        case BTN_SELECT: return SK_GAMEPAD_BUTTON_BACK;
        case BTN_START: return SK_GAMEPAD_BUTTON_START;
        case BTN_MODE: return SK_GAMEPAD_BUTTON_GUIDE;
        case BTN_THUMBL: return SK_GAMEPAD_BUTTON_LEFT_STICK;
        case BTN_THUMBR: return SK_GAMEPAD_BUTTON_RIGHT_STICK;
        case BTN_DPAD_UP: return SK_GAMEPAD_BUTTON_DPAD_UP;
        case BTN_DPAD_DOWN: return SK_GAMEPAD_BUTTON_DPAD_DOWN;
        case BTN_DPAD_LEFT: return SK_GAMEPAD_BUTTON_DPAD_LEFT;
        case BTN_DPAD_RIGHT: return SK_GAMEPAD_BUTTON_DPAD_RIGHT;
        default: return -1;
    }
}

static void apply_abs(const sk_evdev_t *dev, sk_pad_t *pad, int code, int value)
{
    switch (code) {
        case ABS_X: pad->axes[SK_GAMEPAD_AXIS_LEFT_X] = abs_value(dev, code, value, false); break;
        case ABS_Y: pad->axes[SK_GAMEPAD_AXIS_LEFT_Y] = abs_value(dev, code, value, false); break;
        case ABS_RX: pad->axes[SK_GAMEPAD_AXIS_RIGHT_X] = abs_value(dev, code, value, false); break;
        case ABS_RY: pad->axes[SK_GAMEPAD_AXIS_RIGHT_Y] = abs_value(dev, code, value, false); break;
        case ABS_Z:
            if (dev->right_on_z) pad->axes[SK_GAMEPAD_AXIS_RIGHT_X] = abs_value(dev, code, value, false);
            else pad->axes[SK_GAMEPAD_AXIS_LEFT_TRIGGER] = abs_value(dev, code, value, true);
            break;
        case ABS_RZ:
            if (dev->right_on_z) pad->axes[SK_GAMEPAD_AXIS_RIGHT_Y] = abs_value(dev, code, value, false);
            else pad->axes[SK_GAMEPAD_AXIS_RIGHT_TRIGGER] = abs_value(dev, code, value, true);
            break;
        case ABS_BRAKE: pad->axes[SK_GAMEPAD_AXIS_LEFT_TRIGGER] = abs_value(dev, code, value, true); break;
        case ABS_GAS: pad->axes[SK_GAMEPAD_AXIS_RIGHT_TRIGGER] = abs_value(dev, code, value, true); break;
        case ABS_HAT0X:
            pad->buttons[SK_GAMEPAD_BUTTON_DPAD_LEFT] = value < 0;
            pad->buttons[SK_GAMEPAD_BUTTON_DPAD_RIGHT] = value > 0;
            break;
        case ABS_HAT0Y:
            pad->buttons[SK_GAMEPAD_BUTTON_DPAD_UP] = value < 0;
            pad->buttons[SK_GAMEPAD_BUTTON_DPAD_DOWN] = value > 0;
            break;
        default: break;
    }
}

static bool is_open(const char *path)
{
    for (int i = 0; i < SK_INPUT_MAX_GAMEPADS; i++) {
        if (sk_evdevs[i].fd >= 0 && strcmp(sk_evdevs[i].path, path) == 0) return true;
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
static void try_open(const char *event_name, sk_pad_t pads[SK_INPUT_MAX_GAMEPADS])
{
    unsigned long key_bits[(KEY_MAX + LONG_BITS) / LONG_BITS] = {0};
    unsigned long abs_bits[(ABS_MAX + LONG_BITS) / LONG_BITS] = {0};
    unsigned long keys_down[(KEY_MAX + LONG_BITS) / LONG_BITS] = {0};
    char path[288];
    int slot = -1, fd;

    snprintf(path, sizeof(path), "/dev/input/%s", event_name);
    if (is_open(path)) return;
    for (int i = 0; i < SK_INPUT_MAX_GAMEPADS && slot < 0; i++) {
        if (sk_evdevs[i].fd < 0) slot = i;
    }
    if (slot < 0) return;
    fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0 || !HAS_BIT(key_bits, BTN_GAMEPAD)) {
        close(fd); /* not a gamepad (keyboards, mice, ...) */
        return;
    }
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);

    sk_evdev_t *dev = &sk_evdevs[slot];
    sk_pad_t *pad = &pads[slot];
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

static void rescan(sk_pad_t pads[SK_INPUT_MAX_GAMEPADS])
{
    DIR *dir = opendir("/dev/input");
    struct dirent *entry;
    if (dir == NULL) return;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "event", 5) == 0) try_open(entry->d_name, pads);
    }
    closedir(dir);
}

static void close_slot(int slot, sk_pad_t pads[SK_INPUT_MAX_GAMEPADS])
{
    if (sk_evdevs[slot].fd >= 0) {
        close(sk_evdevs[slot].fd);
        log_info("gamepad %d: disconnected", slot);
    }
    sk_evdevs[slot].fd = -1;
    memset(&pads[slot], 0, sizeof(pads[slot]));
}

static void platform_open(void)
{
    for (int i = 0; i < SK_INPUT_MAX_GAMEPADS; i++) sk_evdevs[i].fd = -1;
    sk_next_scan = 0.0; /* the first poll scans */
}

static void platform_close(void)
{
    for (int i = 0; i < SK_INPUT_MAX_GAMEPADS; i++) {
        if (sk_evdevs[i].fd >= 0) close(sk_evdevs[i].fd);
        sk_evdevs[i].fd = -1;
    }
}

static void platform_poll(sk_pad_t pads[SK_INPUT_MAX_GAMEPADS])
{
    const double now = now_seconds();
    if (now >= sk_next_scan) {
        rescan(pads);
        sk_next_scan = now + RESCAN_SECONDS;
    }
    for (int slot = 0; slot < SK_INPUT_MAX_GAMEPADS; slot++) {
        sk_evdev_t *dev = &sk_evdevs[slot];
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
} sk_xinput_gamepad_t;
typedef struct {
    DWORD packet;
    sk_xinput_gamepad_t gamepad;
} sk_xinput_state_t;
typedef DWORD(WINAPI *sk_xinput_get_state_fn)(DWORD, sk_xinput_state_t *);

static HMODULE sk_xinput;
static sk_xinput_get_state_fn sk_xinput_get_state;
static DWORD sk_next_empty_check[SK_INPUT_MAX_GAMEPADS]; /* asking about an empty slot is slow */

static void platform_open(void)
{
    static const char *dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (int i = 0; i < 3 && sk_xinput == NULL; i++) sk_xinput = LoadLibraryA(dlls[i]);
    if (sk_xinput != NULL) {
        sk_xinput_get_state = (sk_xinput_get_state_fn)(void *)GetProcAddress(sk_xinput, "XInputGetState");
    }
    memset(sk_next_empty_check, 0, sizeof(sk_next_empty_check));
}

static void platform_close(void)
{
    if (sk_xinput != NULL) FreeLibrary(sk_xinput);
    sk_xinput = NULL;
    sk_xinput_get_state = NULL;
}

static float stick(SHORT value, bool flip)
{
    const float v = (float)value / 32767.0f;
    return flip ? -v : v;
}

static void platform_poll(sk_pad_t pads[SK_INPUT_MAX_GAMEPADS])
{
    static const struct {
        WORD mask;
        int button;
    } map[] = {
        {0x1000, SK_GAMEPAD_BUTTON_SOUTH},      {0x2000, SK_GAMEPAD_BUTTON_EAST},
        {0x4000, SK_GAMEPAD_BUTTON_WEST},       {0x8000, SK_GAMEPAD_BUTTON_NORTH},
        {0x0100, SK_GAMEPAD_BUTTON_LEFT_BUMPER}, {0x0200, SK_GAMEPAD_BUTTON_RIGHT_BUMPER},
        {0x0020, SK_GAMEPAD_BUTTON_BACK},       {0x0010, SK_GAMEPAD_BUTTON_START},
        {0x0040, SK_GAMEPAD_BUTTON_LEFT_STICK}, {0x0080, SK_GAMEPAD_BUTTON_RIGHT_STICK},
        {0x0001, SK_GAMEPAD_BUTTON_DPAD_UP},    {0x0002, SK_GAMEPAD_BUTTON_DPAD_DOWN},
        {0x0004, SK_GAMEPAD_BUTTON_DPAD_LEFT},  {0x0008, SK_GAMEPAD_BUTTON_DPAD_RIGHT},
    };
    const DWORD now = GetTickCount();
    if (sk_xinput_get_state == NULL) return;
    for (int i = 0; i < SK_INPUT_MAX_GAMEPADS; i++) {
        sk_xinput_state_t state;
        if (!pads[i].connected && (LONG)(now - sk_next_empty_check[i]) < 0) continue;
        if (sk_xinput_get_state((DWORD)i, &state) != ERROR_SUCCESS) {
            memset(&pads[i], 0, sizeof(pads[i]));
            sk_next_empty_check[i] = now + (DWORD)(RESCAN_SECONDS * 1000.0);
            continue;
        }
        pads[i].connected = true;
        snprintf(pads[i].name, sizeof(pads[i].name), "XInput controller %d", i + 1);
        for (size_t b = 0; b < sizeof(map) / sizeof(map[0]); b++) {
            pads[i].buttons[map[b].button] = (state.gamepad.buttons & map[b].mask) != 0;
        }
        pads[i].axes[SK_GAMEPAD_AXIS_LEFT_X] = stick(state.gamepad.lx, false);
        pads[i].axes[SK_GAMEPAD_AXIS_LEFT_Y] = stick(state.gamepad.ly, true); /* XInput: up is positive */
        pads[i].axes[SK_GAMEPAD_AXIS_RIGHT_X] = stick(state.gamepad.rx, false);
        pads[i].axes[SK_GAMEPAD_AXIS_RIGHT_Y] = stick(state.gamepad.ry, true);
        pads[i].axes[SK_GAMEPAD_AXIS_LEFT_TRIGGER] = (float)state.gamepad.left_trigger / 255.0f;
        pads[i].axes[SK_GAMEPAD_AXIS_RIGHT_TRIGGER] = (float)state.gamepad.right_trigger / 255.0f;
    }
}

#else /* macOS and others: none yet */

static void platform_open(void) {}
static void platform_close(void) {}
static void platform_poll(sk_pad_t pads[SK_INPUT_MAX_GAMEPADS]) { (void)pads; }

#endif

/* ------------------------------------------------------------ frames ---- */

void sk_gamepad_init(void)
{
    memset(&sk_gp, 0, sizeof(sk_gp));
    sk_gp.deadzone = 0.15f;
    platform_open();
}

void sk_gamepad_deinit(void)
{
    platform_close();
    memset(&sk_gp, 0, sizeof(sk_gp));
}

/* Poll, then record what changed since the last poll in both edge sets. A pad that
 * went away releases what it held. */
void sk_gamepad_begin_frame(void)
{
    sk_pad_t next[SK_INPUT_MAX_GAMEPADS];
    if (!sk_gp.testing) platform_poll(sk_gp.raw);
    memcpy(next, sk_gp.testing ? sk_gp.test_pads : sk_gp.raw, sizeof(next));

    for (int i = 0; i < SK_INPUT_MAX_GAMEPADS; i++) {
        sk_pad_t *pad = &next[i];
        if (!pad->connected) {
            memset(pad, 0, sizeof(*pad));
        } else {
            for (int a = 0; a < SK_GAMEPAD_AXIS_COUNT; a++) {
                normalize(&pad->axes[a], a >= SK_GAMEPAD_AXIS_LEFT_TRIGGER ? 0.0f : -1.0f, 1.0f);
            }
            /* triggers are buttons too, past half-way (whatever the platform said) */
            pad->buttons[SK_GAMEPAD_BUTTON_LEFT_TRIGGER] =
                pad->buttons[SK_GAMEPAD_BUTTON_LEFT_TRIGGER] || pad->axes[SK_GAMEPAD_AXIS_LEFT_TRIGGER] > 0.5f;
            pad->buttons[SK_GAMEPAD_BUTTON_RIGHT_TRIGGER] =
                pad->buttons[SK_GAMEPAD_BUTTON_RIGHT_TRIGGER] || pad->axes[SK_GAMEPAD_AXIS_RIGHT_TRIGGER] > 0.5f;
        }
        for (int b = 0; b < SK_GAMEPAD_BUTTON_COUNT; b++) {
            const bool was = sk_gp.pads[i].buttons[b], is = pad->buttons[b];
            if (is && !was) sk_gp.frame_edges[i].pressed[b] = sk_gp.tick_edges[i].pressed[b] = true;
            if (!is && was) sk_gp.frame_edges[i].released[b] = sk_gp.tick_edges[i].released[b] = true;
        }
    }
    memcpy(sk_gp.pads, next, sizeof(next));
}

void sk_gamepad_end_tick(void)
{
    memset(sk_gp.tick_edges, 0, sizeof(sk_gp.tick_edges));
}

void sk_gamepad_frame_done(void)
{
    memset(sk_gp.frame_edges, 0, sizeof(sk_gp.frame_edges));
}

void sk_gamepad_set_test_pad(int pad, bool connected, const char *name, const bool buttons[SK_GAMEPAD_BUTTON_COUNT],
                             const float axes[SK_GAMEPAD_AXIS_COUNT])
{
    sk_pad_t *p;
    if (pad < 0 || pad >= SK_INPUT_MAX_GAMEPADS) return;
    sk_gp.testing = true;
    p = &sk_gp.test_pads[pad];
    memset(p, 0, sizeof(*p));
    p->connected = connected;
    snprintf(p->name, sizeof(p->name), "%s", name != NULL ? name : "");
    if (buttons != NULL) memcpy(p->buttons, buttons, sizeof(p->buttons));
    if (axes != NULL) memcpy(p->axes, axes, sizeof(p->axes));
}

/* ------------------------------------------------------------ public API ---- */

static const sk_pad_t *lookup_pad(int pad)
{
    return pad >= 0 && pad < SK_INPUT_MAX_GAMEPADS && sk_gp.pads[pad].connected ? &sk_gp.pads[pad] : NULL;
}

SK_KEEP bool sk_input_is_gamepad_connected(int pad)
{
    return lookup_pad(pad) != NULL;
}

SK_KEEP const char *sk_input_get_gamepad_name(int pad)
{
    const sk_pad_t *pad_ptr = lookup_pad(pad);
    return pad_ptr != NULL ? pad_ptr->name : "";
}

SK_KEEP int sk_input_get_gamepad_button(int pad, sk_gamepad_button_t button)
{
    const sk_pad_edges_t *edges;
    if (pad < 0 || pad >= SK_INPUT_MAX_GAMEPADS || button < 0 || button >= SK_GAMEPAD_BUTTON_COUNT) {
        return SK_BUTTON_UP;
    }
    /* edges outlive a disconnect by a frame (the release), so read them regardless */
    edges = sk_input_get_context() == SK_INPUT_CONTEXT_TICK ? &sk_gp.tick_edges[pad] : &sk_gp.frame_edges[pad];
    if (edges->pressed[button]) return SK_BUTTON_PRESSED;
    if (edges->released[button]) return SK_BUTTON_RELEASED;
    return sk_gp.pads[pad].buttons[button] ? SK_BUTTON_DOWN : SK_BUTTON_UP;
}

/* A stick past the dead zone, rescaled so it still reaches 1 at the edge. */
static float stick_axis(const sk_pad_t *pad_ptr, int x_axis, bool want_y)
{
    const float x = pad_ptr->axes[x_axis], y = pad_ptr->axes[x_axis + 1];
    const float length = sqrtf(x * x + y * y);
    float scale;
    if (length <= sk_gp.deadzone) return 0.0f;
    scale = (length - sk_gp.deadzone) / (1.0f - sk_gp.deadzone) / length;
    if (length * scale > 1.0f) scale = 1.0f / length; /* square-ish sticks reach past 1 */
    return (want_y ? y : x) * scale;
}

SK_KEEP float sk_input_get_gamepad_axis(int pad, sk_gamepad_axis_t axis)
{
    const sk_pad_t *pad_ptr = lookup_pad(pad);
    if (pad_ptr == NULL || axis < 0 || axis >= SK_GAMEPAD_AXIS_COUNT) return 0.0f;
    switch (axis) {
        case SK_GAMEPAD_AXIS_LEFT_X: return stick_axis(pad_ptr, SK_GAMEPAD_AXIS_LEFT_X, false);
        case SK_GAMEPAD_AXIS_LEFT_Y: return stick_axis(pad_ptr, SK_GAMEPAD_AXIS_LEFT_X, true);
        case SK_GAMEPAD_AXIS_RIGHT_X: return stick_axis(pad_ptr, SK_GAMEPAD_AXIS_RIGHT_X, false);
        case SK_GAMEPAD_AXIS_RIGHT_Y: return stick_axis(pad_ptr, SK_GAMEPAD_AXIS_RIGHT_X, true);
        default: return pad_ptr->axes[axis];
    }
}

SK_KEEP bool sk_input_set_gamepad_deadzone(float radius)
{
    if (!(radius >= 0.0f && radius <= 0.9f)) return false;
    sk_gp.deadzone = radius;
    return true;
}

/* An optional subsystem: part of the runtime when a program asks about gamepads
 * (internal/sk_module.h). Polled before the frame's ticks, so they see it too. */
static sk_module_t sk_gamepad_module = {.name = "gamepad", .order = 5, .init = sk_gamepad_init,
                                        .deinit = sk_gamepad_deinit, .begin_frame = sk_gamepad_begin_frame,
                                        .end_tick = sk_gamepad_end_tick, .frame_done = sk_gamepad_frame_done};
SK_MODULE(sk_gamepad_module)
