/* Keyboard and mouse input for the libretro frontend.

   86Box's input path is event-based: keyboard_input(down, scan) takes an XT
   scancode, with extended (0xe0-prefixed) keys expressed as 0x1xx, and the
   mouse takes relative motion through mouse_scale().

   The table below is the RETROK_* half of sdl_to_xt[] in unix/sdl_main.c.
   RETROK_* values follow SDL 1.2 keysyms, so this maps virtual keys rather
   than physical positions - close enough for a PC keyboard, which is the
   layout the table was written for in the first place. */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/plat.h>

#include "lr_ui.h"

int mouse_capture = 1;
int infocus       = 1;

struct retro_to_xt {
    unsigned retro;
    uint16_t xt;
};

static const struct retro_to_xt keymap[] = {
    { RETROK_ESCAPE, 0x01 },        { RETROK_1, 0x02 },            { RETROK_2, 0x03 },
    { RETROK_3, 0x04 },             { RETROK_4, 0x05 },            { RETROK_5, 0x06 },
    { RETROK_6, 0x07 },             { RETROK_7, 0x08 },            { RETROK_8, 0x09 },
    { RETROK_9, 0x0a },             { RETROK_0, 0x0b },            { RETROK_MINUS, 0x0c },
    { RETROK_EQUALS, 0x0d },        { RETROK_BACKSPACE, 0x0e },    { RETROK_TAB, 0x0f },
    { RETROK_q, 0x10 },             { RETROK_w, 0x11 },            { RETROK_e, 0x12 },
    { RETROK_r, 0x13 },             { RETROK_t, 0x14 },            { RETROK_y, 0x15 },
    { RETROK_u, 0x16 },             { RETROK_i, 0x17 },            { RETROK_o, 0x18 },
    { RETROK_p, 0x19 },             { RETROK_LEFTBRACKET, 0x1a },  { RETROK_RIGHTBRACKET, 0x1b },
    { RETROK_RETURN, 0x1c },        { RETROK_LCTRL, 0x1d },        { RETROK_a, 0x1e },
    { RETROK_s, 0x1f },             { RETROK_d, 0x20 },            { RETROK_f, 0x21 },
    { RETROK_g, 0x22 },             { RETROK_h, 0x23 },            { RETROK_j, 0x24 },
    { RETROK_k, 0x25 },             { RETROK_l, 0x26 },            { RETROK_SEMICOLON, 0x27 },
    { RETROK_QUOTE, 0x28 },         { RETROK_BACKQUOTE, 0x29 },    { RETROK_LSHIFT, 0x2a },
    { RETROK_BACKSLASH, 0x2b },     { RETROK_z, 0x2c },            { RETROK_x, 0x2d },
    { RETROK_c, 0x2e },             { RETROK_v, 0x2f },            { RETROK_b, 0x30 },
    { RETROK_n, 0x31 },             { RETROK_m, 0x32 },            { RETROK_COMMA, 0x33 },
    { RETROK_PERIOD, 0x34 },        { RETROK_SLASH, 0x35 },        { RETROK_RSHIFT, 0x36 },
    { RETROK_KP_MULTIPLY, 0x37 },   { RETROK_LALT, 0x38 },         { RETROK_SPACE, 0x39 },
    { RETROK_CAPSLOCK, 0x3a },      { RETROK_F1, 0x3b },           { RETROK_F2, 0x3c },
    { RETROK_F3, 0x3d },            { RETROK_F4, 0x3e },           { RETROK_F5, 0x3f },
    { RETROK_F6, 0x40 },            { RETROK_F7, 0x41 },           { RETROK_F8, 0x42 },
    { RETROK_F9, 0x43 },            { RETROK_F10, 0x44 },          { RETROK_NUMLOCK, 0x45 },
    { RETROK_SCROLLOCK, 0x46 },     { RETROK_KP7, 0x47 },          { RETROK_KP8, 0x48 },
    { RETROK_KP9, 0x49 },           { RETROK_KP_MINUS, 0x4a },     { RETROK_KP4, 0x4b },
    { RETROK_KP5, 0x4c },           { RETROK_KP6, 0x4d },          { RETROK_KP_PLUS, 0x4e },
    { RETROK_KP1, 0x4f },           { RETROK_KP2, 0x50 },          { RETROK_KP3, 0x51 },
    { RETROK_KP0, 0x52 },           { RETROK_KP_PERIOD, 0x53 },    { RETROK_F11, 0x57 },
    { RETROK_F12, 0x58 },
    /* Extended keys: 0xe0-prefixed on the wire, 0x1xx to keyboard_input(). */
    { RETROK_KP_ENTER, 0x11c },     { RETROK_RCTRL, 0x11d },       { RETROK_KP_DIVIDE, 0x135 },
    { RETROK_PRINT, 0x137 },        { RETROK_RALT, 0x138 },        { RETROK_HOME, 0x147 },
    { RETROK_UP, 0x148 },           { RETROK_PAGEUP, 0x149 },      { RETROK_LEFT, 0x14b },
    { RETROK_RIGHT, 0x14d },        { RETROK_END, 0x14f },         { RETROK_DOWN, 0x150 },
    { RETROK_PAGEDOWN, 0x151 },     { RETROK_INSERT, 0x152 },      { RETROK_DELETE, 0x153 },
    { RETROK_LSUPER, 0x15b },       { RETROK_RSUPER, 0x15c },      { RETROK_MENU, 0x15d },
};

#define KEYMAP_LEN (sizeof(keymap) / sizeof(keymap[0]))

/* Two sources, OR-ed together, because frontends differ in which they offer:
   SET_KEYBOARD_CALLBACK pushes events into cb_key[], RETRO_DEVICE_KEYBOARD
   polling fills poll_key[]. Keeping them apart stops the poll from clobbering
   callback state on a frontend that only sends events, and vice versa. */
static uint8_t cb_key[KEYMAP_LEN];
static uint8_t poll_key[KEYMAP_LEN];
static uint8_t key_state[KEYMAP_LEN];

static int16_t retro_to_slot[RETROK_LAST];

void
lr_input_init(void)
{
    memset(cb_key, 0, sizeof(cb_key));
    memset(poll_key, 0, sizeof(poll_key));
    memset(key_state, 0, sizeof(key_state));

    for (unsigned c = 0; c < RETROK_LAST; c++)
        retro_to_slot[c] = -1;
    for (unsigned c = 0; c < KEYMAP_LEN; c++)
        retro_to_slot[keymap[c].retro] = (int16_t) c;
}

/* keyboard_input() wants edges, so only changes are passed on. */
static void
emit_changed_keys(void)
{
    for (unsigned c = 0; c < KEYMAP_LEN; c++) {
        const uint8_t down = (cb_key[c] | poll_key[c]) ? 1 : 0;

        if (down != key_state[c]) {
            key_state[c] = down;
            keyboard_input(down, keymap[c].xt);
        }
    }
}

void
lr_keyboard_event(bool down, unsigned keycode, UNUSED(uint32_t character), UNUSED(uint16_t key_modifiers))
{
    if (keycode >= RETROK_LAST)
        return;

    const int16_t slot = retro_to_slot[keycode];

    if (slot >= 0) {
        cb_key[slot] = down ? 1 : 0;
        emit_changed_keys();
    }
}

void
lr_input_poll(void)
{
    int buttons = 0;

    for (unsigned c = 0; c < KEYMAP_LEN; c++)
        poll_key[c] = lr_input_state_cb(0, RETRO_DEVICE_KEYBOARD, 0, keymap[c].retro) ? 1 : 0;

    emit_changed_keys();

    mouse_scale(lr_input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X),
                lr_input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y));

    if (lr_input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELUP))
        mouse_set_z(1);
    else if (lr_input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_WHEELDOWN))
        mouse_set_z(-1);

    if (lr_input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT))
        buttons |= 1;
    if (lr_input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT))
        buttons |= 2;
    if (lr_input_state_cb(0, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE))
        buttons |= 4;

    mouse_set_buttons_ex(buttons);
}
