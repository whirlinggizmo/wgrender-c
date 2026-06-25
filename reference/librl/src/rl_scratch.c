#include <string.h>
#include "raylib.h"
#include "internal/exports.h"
#include "rl_scratch.h"
#include <stddef.h>
#include "logger/logger.h"

RL_KEEP
static rl_scratch_offsets_t rl_scratch_offsets;

RL_KEEP
const rl_scratch_offsets_t *rl_scratch_get_offsets(void)
{
    rl_scratch_offsets = (rl_scratch_offsets_t){
        .vector2 = offsetof(rl_scratch_t, vector2),
        .vector3 = offsetof(rl_scratch_t, vector3),
        .vector4 = offsetof(rl_scratch_t, vector4),
        .matrix = offsetof(rl_scratch_t, matrix),
        .quaternion = offsetof(rl_scratch_t, quaternion),
        .color = offsetof(rl_scratch_t, color),
        .rect = offsetof(rl_scratch_t, rect),
        .pick_result = {
            .hit = offsetof(rl_scratch_t, pick_result.hit),
            .handle = offsetof(rl_scratch_t, pick_result.handle),
            .distance = offsetof(rl_scratch_t, pick_result.distance),
            .point = offsetof(rl_scratch_t, pick_result.point),
            .normal = offsetof(rl_scratch_t, pick_result.normal),
        },
        .mouse = {
            .x = offsetof(rl_scratch_t, mouse.x),
            .y = offsetof(rl_scratch_t, mouse.y),
            .wheel = offsetof(rl_scratch_t, mouse.wheel),
            .buttons = offsetof(rl_scratch_t, mouse.buttons),
            .dx = offsetof(rl_scratch_t, mouse.dx),
            .dy = offsetof(rl_scratch_t, mouse.dy)},
        .keyboard = {
            .max_num_keys = offsetof(rl_scratch_t, keyboard.max_num_keys), 
            .keys = offsetof(rl_scratch_t, keyboard.keys), 
            .pressed_key = offsetof(rl_scratch_t, keyboard.pressed_key),
            .pressed_char = offsetof(rl_scratch_t, keyboard.pressed_char),
            .num_pressed_keys = offsetof(rl_scratch_t, keyboard.num_pressed_keys),
            .pressed_keys = offsetof(rl_scratch_t, keyboard.pressed_keys),
            .num_pressed_chars = offsetof(rl_scratch_t, keyboard.num_pressed_chars),
            .pressed_chars = offsetof(rl_scratch_t, keyboard.pressed_chars)
        },
        .gamepads = {
            .max_num_gamepads = offsetof(rl_scratch_t, gamepads.max_num_gamepads),
            .gamepad = offsetof(rl_scratch_t, gamepads.gamepad),
            .id = offsetof(rl_scratch_t, gamepads.gamepad[0].id),
            .axis = offsetof(rl_scratch_t, gamepads.gamepad[0].axis),
            .buttons = offsetof(rl_scratch_t, gamepads.gamepad[0].buttons),
            .stride = sizeof(((rl_scratch_t *)0)->gamepads.gamepad[0]),
        },
        .touchpoints = {
            .count = offsetof(rl_scratch_t, touchpoints.count), 
            .touchpoint = offsetof(rl_scratch_t, touchpoints.touchpoint), 
            .x = offsetof(rl_scratch_t, touchpoints.touchpoint[0].x), 
            .y = offsetof(rl_scratch_t, touchpoints.touchpoint[0].y),
            .id = offsetof(rl_scratch_t, touchpoints.touchpoint[0].id), 
            .stride = sizeof(((rl_scratch_t *)0)->touchpoints.touchpoint[0]),
        },
        .string_table = {
            .offsets = offsetof(rl_scratch_t, string_offsets),
            .bytes = offsetof(rl_scratch_t, string_bytes),
            .max_entries = RL_SCRATCH_MAX_STRING_TABLE_ENTRIES,
            .max_bytes = RL_SCRATCH_MAX_STRING_TABLE_BYTES,
        }
    };
    return &rl_scratch_offsets;
}

static rl_scratch_t rl_scratch;

void rl_scratch_set_vector2(float x, float y)
{
    rl_scratch.vector2[0] = x;
    rl_scratch.vector2[1] = y;
}

void rl_scratch_set_vector3(float x, float y, float z)
{
    rl_scratch.vector3[0] = x;
    rl_scratch.vector3[1] = y;
    rl_scratch.vector3[2] = z;
}

void rl_scratch_set_vector4(float x, float y, float z, float w)
{
    rl_scratch.vector4[0] = x;
    rl_scratch.vector4[1] = y;
    rl_scratch.vector4[2] = z;
    rl_scratch.vector4[3] = w;
}

void rl_scratch_set_matrix(float m[16])
{
    memcpy(rl_scratch.matrix, m, sizeof(float) * 16);
}

void rl_scratch_set_quaternion(float x, float y, float z, float w)
{
    rl_scratch.quaternion[0] = x;
    rl_scratch.quaternion[1] = y;
    rl_scratch.quaternion[2] = z;
    rl_scratch.quaternion[3] = w;
}

void rl_scratch_set_color(int r, int g, int b, int a)
{
    rl_scratch.color[0] = r;
    rl_scratch.color[1] = g;
    rl_scratch.color[2] = b;
    rl_scratch.color[3] = a;
}

void rl_scratch_set_rect(int x, int y, int width, int height)
{
    rl_scratch.rect[0] = x;
    rl_scratch.rect[1] = y;
    rl_scratch.rect[2] = width;
    rl_scratch.rect[3] = height;
}

void rl_scratch_set_pick_result(rl_pick_result_t result)
{
    rl_scratch.pick_result = result;
}

void rl_scratch_set_mouse(int x, int y, int wheel, int left, int right, int middle, int dx, int dy)
{
    rl_scratch.mouse.x = x;
    rl_scratch.mouse.y = y;
    rl_scratch.mouse.wheel = wheel;
    rl_scratch.mouse.buttons[0] = left;
    rl_scratch.mouse.buttons[1] = right;
    rl_scratch.mouse.buttons[2] = middle;
    rl_scratch.mouse.dx = dx;
    rl_scratch.mouse.dy = dy;
}

void rl_scratch_set_touchpoint(int index, int x, int y, int id)
{
    rl_scratch.touchpoints.count = index + 1;
    rl_scratch.touchpoints.touchpoint[index].x = x;
    rl_scratch.touchpoints.touchpoint[index].y = y;
    rl_scratch.touchpoints.touchpoint[index].id = id;
}

void rl_scratch_set_keyboard_key(int key, int state)
{
    rl_scratch.keyboard.keys[key] = state;
}

void rl_scratch_set_gamepad_axis(int id, int axis, float value)
{
    rl_scratch.gamepads.gamepad[id].axis[axis] = value;
}

void rl_scratch_set_gamepad_button(int id, int button, int state)
{
    rl_scratch.gamepads.gamepad[id].buttons[button] = state;
}

// Return the struct pointer as an uintptr, otherwise emscripten tries to convert it to a BigInt
RL_KEEP
uintptr_t rl_scratch_get_base()
{
    return (uintptr_t)&rl_scratch;
}

RL_KEEP
void rl_scratch_clear()
{
    memset(&rl_scratch, 0, sizeof(rl_scratch_t));

    rl_scratch.keyboard.max_num_keys = RL_SCRATCH_MAX_NUM_KEYBOARD_KEYS;
    rl_scratch.gamepads.max_num_gamepads = RL_SCRATCH_MAX_NUM_GAMEPADS;
}

void rl_scratch_init()
{
    rl_scratch_clear();
}

void rl_scratch_deinit()
{
    // do nothing?
    //printf("rl_scratch_deinit\n");
}

RL_KEEP
void rl_scratch_refresh()
{
#ifdef HEADLESS
    rl_scratch.mouse.x = 0;
    rl_scratch.mouse.y = 0;
    rl_scratch.mouse.wheel = 0;
    memset(rl_scratch.mouse.buttons, 0, sizeof(rl_scratch.mouse.buttons));
    rl_scratch.mouse.dx = 0;
    rl_scratch.mouse.dy = 0;

    rl_scratch.keyboard.pressed_key = 0;
    rl_scratch.keyboard.pressed_char = 0;
    rl_scratch.keyboard.num_pressed_keys = 0;
    rl_scratch.keyboard.num_pressed_chars = 0;
    memset(rl_scratch.keyboard.keys, 0, sizeof(rl_scratch.keyboard.keys));
    memset(rl_scratch.keyboard.pressed_keys, 0, sizeof(rl_scratch.keyboard.pressed_keys));
    memset(rl_scratch.keyboard.pressed_chars, 0, sizeof(rl_scratch.keyboard.pressed_chars));

    memset(&rl_scratch.gamepads.gamepad, 0, sizeof(rl_scratch.gamepads.gamepad));
    rl_scratch.touchpoints.count = 0;
    memset(&rl_scratch.touchpoints.touchpoint, 0, sizeof(rl_scratch.touchpoints.touchpoint));
    return;
#else

    // TODO: num mouse buttons (currently assumed to be 3)

    // mouse
    int mouse_button_status_0 = 0;
    int mouse_button_status_1 = 0;
    int mouse_button_status_2 = 0;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        mouse_button_status_0 = RL_BUTTON_PRESSED;
    }
    else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        mouse_button_status_0 = RL_BUTTON_RELEASED;
    }
    else if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        mouse_button_status_0 = RL_BUTTON_DOWN;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
    {
        mouse_button_status_1 = RL_BUTTON_PRESSED;
    }
    else if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
    {
        mouse_button_status_1 = RL_BUTTON_RELEASED;
    }
    else if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        mouse_button_status_1 = RL_BUTTON_DOWN;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE))
    {
        mouse_button_status_2 = RL_BUTTON_PRESSED;
    }
    else if (IsMouseButtonReleased(MOUSE_BUTTON_MIDDLE))
    {
        mouse_button_status_2 = RL_BUTTON_RELEASED;
    }
    else if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
    {
        mouse_button_status_2 = RL_BUTTON_DOWN;
    }

    Vector2 mouse_delta = GetMouseDelta();
    rl_scratch_set_mouse(GetMouseX(), GetMouseY(), GetMouseWheelMove(), mouse_button_status_0, mouse_button_status_1, mouse_button_status_2, (int)mouse_delta.x, (int)mouse_delta.y);

    // keyboard
    rl_scratch.keyboard.pressed_key = 0;
    rl_scratch.keyboard.pressed_char = 0;
    rl_scratch.keyboard.num_pressed_keys = 0;
    rl_scratch.keyboard.num_pressed_chars = 0;

    for (int i = 0; i < RL_SCRATCH_MAX_NUM_KEYBOARD_KEYS; i++)
    {
        int key_status = 0;
        if (IsKeyPressed(i))
        {
            key_status = 1; // just pressed
            // fprintf(stderr, "key %d just pressed\n", i);
        }
        if (IsKeyDown(i))
        {
            key_status = 2; // held
        }
        if (IsKeyReleased(i))
        {
            key_status = 3; // just released
        }
        rl_scratch_set_keyboard_key(i, key_status);
    }

    while (rl_scratch.keyboard.num_pressed_keys < RL_KEYBOARD_MAX_PRESSED_KEYS)
    {
        int key = GetKeyPressed();
        if (key == 0)
        {
            break;
        }
        rl_scratch.keyboard.pressed_key = key;
        rl_scratch.keyboard.pressed_keys[rl_scratch.keyboard.num_pressed_keys++] = key;
    }

    while (rl_scratch.keyboard.num_pressed_chars < RL_KEYBOARD_MAX_PRESSED_CHARS)
    {
        int ch = GetCharPressed();
        if (ch == 0)
        {
            break;
        }
        rl_scratch.keyboard.pressed_char = ch;
        rl_scratch.keyboard.pressed_chars[rl_scratch.keyboard.num_pressed_chars++] = ch;
    }

    // gamepad
    for (int id = 0; id < RL_SCRATCH_MAX_NUM_GAMEPADS; id++)
    {
        if (!IsGamepadAvailable(id))
        {
            continue;
        }
        for (int i = 0; i < RL_SCRATCH_NUM_GAMEPAD_BUTTONS; i++)
        {
            int button_status = 0;
            if (IsGamepadButtonPressed(id, i))
            {
                button_status = 1; // just pressed
            }
            if (IsGamepadButtonDown(id, i))
            {
                button_status = 2; // held
            }
            if (IsGamepadButtonReleased(id, i))
            {
                button_status = 3; // just released
            }
            rl_scratch_set_gamepad_button(id, i, button_status);
        }

        // gamepad axis
        int num_axis = GetGamepadAxisCount(id);
        if (num_axis < RL_SCRATCH_MAX_NUM_GAMEPAD_AXIS)
        {
            log_warn("Gamepad %d has %d axis, more than we allocated for (%d)", id, GetGamepadAxisCount(id), RL_SCRATCH_MAX_NUM_GAMEPAD_AXIS);
            continue;
        }

        for (int i = 0; i < num_axis; i++)
        {
            rl_scratch_set_gamepad_axis(id, i, GetGamepadAxisMovement(id, i));
        }
    }

    // touch
    if (GetTouchPointCount() > 0)
    {
        // set the touch point 0
        rl_scratch_set_touchpoint(0, GetTouchX(), GetTouchY(), GetTouchPointId(0));

        // set the other touch points
        for (int i = 1; i < GetTouchPointCount(); i++)
        {
            Vector2 position = GetTouchPosition(i);
            rl_scratch_set_touchpoint(i, position.x, position.y, GetTouchPointId(i));
        }
    }
    else
    {
        rl_scratch_set_touchpoint(0, 0, 0, 0);
    }
#endif /* !HEADLESS */
}

vec2_t rl_scratch_get_vector2()
{
    vec2_t v2;
    v2.x = rl_scratch.vector2[0];
    v2.y = rl_scratch.vector2[1];
    return v2;
}

vec3_t rl_scratch_get_vector3()
{
    vec3_t v3;
    v3.x = rl_scratch.vector3[0];
    v3.y = rl_scratch.vector3[1];
    v3.z = rl_scratch.vector3[2];
    return v3;
}

vec4_t rl_scratch_get_vector4()
{
    vec4_t v4;
    v4.x = rl_scratch.vector4[0];
    v4.y = rl_scratch.vector4[1];
    v4.z = rl_scratch.vector4[2];
    v4.w = rl_scratch.vector4[3];
    return v4;
}

matrix_t rl_scratch_get_matrix()
{
    matrix_t m;
    m.m0 = rl_scratch.matrix[0];
    m.m1 = rl_scratch.matrix[1];
    m.m2 = rl_scratch.matrix[2];
    m.m3 = rl_scratch.matrix[3];
    m.m4 = rl_scratch.matrix[4];
    m.m5 = rl_scratch.matrix[5];
    m.m6 = rl_scratch.matrix[6];
    m.m7 = rl_scratch.matrix[7];
    m.m8 = rl_scratch.matrix[8];
    m.m9 = rl_scratch.matrix[9];
    m.m10 = rl_scratch.matrix[10];
    m.m11 = rl_scratch.matrix[11];
    m.m12 = rl_scratch.matrix[12];
    m.m13 = rl_scratch.matrix[13];
    m.m14 = rl_scratch.matrix[14];
    m.m15 = rl_scratch.matrix[15];
    return m;
}

quat_t rl_scratch_get_quaternion()
{
    quat_t q;
    q.x = rl_scratch.quaternion[0];
    q.y = rl_scratch.quaternion[1];
    q.z = rl_scratch.quaternion[2];
    q.w = rl_scratch.quaternion[3];
    return q;
}

color_t rl_scratch_get_color()
{
    color_t c;
    c.r = rl_scratch.color[0];
    c.g = rl_scratch.color[1];
    c.b = rl_scratch.color[2];
    c.a = rl_scratch.color[3];
    return c;
}

rect_t rl_scratch_get_rect()
{
    rect_t r;
    r.x = rl_scratch.rect[0];
    r.y = rl_scratch.rect[1];
    r.width = rl_scratch.rect[2];
    r.height = rl_scratch.rect[3];
    return r;
}

rl_pick_result_t rl_scratch_get_pick_result()
{
    return rl_scratch.pick_result;
}

rl_mouse_t rl_scratch_get_mouse()
{
    return rl_scratch.mouse;
}

rl_keyboard_t rl_scratch_get_keyboard()
{
    return rl_scratch.keyboard;
}

rl_gamepad_t rl_scratch_get_gamepad(int id)
{
    return rl_scratch.gamepads.gamepad[id];
}

rl_touchpoint_t rl_scratch_get_touchpoint(int id) {
    return rl_scratch.touchpoints.touchpoint[id];
}
