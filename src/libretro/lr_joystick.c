/* Joystick input for the libretro frontend.

   Two RETRO_DEVICE_JOYPADs are presented as two 2-axis, 4-button analogue
   sticks, which is what the emulated gameport expects. joystick_process() is
   called from pc_run(), so the state is read there directly rather than in
   lr_input_poll(). */

#include <stdint.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/device.h>
#include <86box/gameport.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>

#include "lr_ui.h"

int                   joysticks_present = 0;
joystick_state_t      joystick_state[GAMEPORT_MAX][MAX_JOYSTICKS];
plat_joystick_state_t plat_joystick_state[MAX_PLAT_JOYSTICKS];

#define LR_STICKS 2

static const unsigned button_ids[4] = {
    RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_A,
    RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_X
};

void
joystick_init(void)
{
    memset(plat_joystick_state, 0, sizeof(plat_joystick_state));

    joysticks_present = LR_STICKS;

    for (int js = 0; js < LR_STICKS; js++) {
        plat_joystick_state_t *st = &plat_joystick_state[js];

        snprintf(st->name, sizeof(st->name), "libretro joypad %d", js + 1);
        st->nr_axes    = 2;
        st->nr_buttons = 4;
        st->nr_povs    = 0;

        strcpy(st->axis[0].name, "X");
        strcpy(st->axis[1].name, "Y");
        for (int b = 0; b < 4; b++)
            snprintf(st->button[b].name, sizeof(st->button[b].name), "Button %d", b + 1);
    }
}

void
joystick_close(void)
{
    joysticks_present = 0;
}

void
joystick_process(UNUSED(uint8_t gp))
{
    if (lr_input_state_cb == NULL)
        return;

    for (int js = 0; js < LR_STICKS; js++) {
        plat_joystick_state_t *st = &plat_joystick_state[js];
        int                    x  = 0;
        int                    y  = 0;

        /* The analogue stick if the pad has one, the d-pad otherwise. */
        x = lr_input_state_cb(js, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        y = lr_input_state_cb(js, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);

        if (x == 0) {
            if (lr_input_state_cb(js, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT))
                x = -32767;
            else if (lr_input_state_cb(js, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT))
                x = 32767;
        }
        if (y == 0) {
            if (lr_input_state_cb(js, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP))
                y = -32767;
            else if (lr_input_state_cb(js, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN))
                y = 32767;
        }

        st->a[0] = x;
        st->a[1] = y;

        for (int b = 0; b < 4; b++)
            st->b[b] = lr_input_state_cb(js, RETRO_DEVICE_JOYPAD, 0, button_ids[b]) ? 1 : 0;
    }

    for (int gp = 0; gp < GAMEPORT_MAX; gp++)
        for (int js = 0; js < MAX_JOYSTICKS; js++) {
            joystick_state_t *out = &joystick_state[gp][js];
            const int         nr  = out->plat_joystick_nr;

            if (nr == 0)
                continue;

            const plat_joystick_state_t *in = &plat_joystick_state[nr - 1];

            for (int axis = 0; axis < MAX_JOY_AXES; axis++)
                out->axis[axis] = (out->axis_mapping[axis] < in->nr_axes) ? in->a[out->axis_mapping[axis]] : 0;
            for (int b = 0; b < MAX_JOY_BUTTONS; b++)
                out->button[b] = (out->button_mapping[b] < in->nr_buttons) ? in->b[out->button_mapping[b]] : 0;
        }
}
