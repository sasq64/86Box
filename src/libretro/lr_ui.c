/* The GUI half of the frontend contract: status bar, message boxes, window
   title. A libretro core has none of that, so these are stubs - except
   ui_msgbox(), which is the only way the core ever tells the user something
   went wrong, and so goes to the log. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>
#include <86box/ui.h>
#include <86box/version.h>

#include "lr_ui.h"

static char window_title[512] = EMU_NAME;

char *
ui_window_title(char *s)
{
    if (s != NULL) {
        strncpy(window_title, s, sizeof(window_title) - 1);
        window_title[sizeof(window_title) - 1] = '\0';
    }

    return window_title;
}

int
ui_msgbox(int flags, char *message)
{
    return ui_msgbox_header(flags, NULL, message);
}

int
ui_msgbox_header(int flags, char *header, char *message)
{
    const enum retro_log_level level = (flags & (MBX_ERROR | MBX_FATAL)) ? RETRO_LOG_ERROR : RETRO_LOG_WARN;

    lr_log_printf(level, "%s: %s\n", (header != NULL) ? header : EMU_NAME, (message != NULL) ? message : "");

    return 0;
}

void
ui_emu_status(UNUSED(int speed_percent))
{
}

void
ui_hard_reset_completed(void)
{
}

void
ui_sb_set_ready(UNUSED(int ready))
{
}

void
ui_sb_update_panes(void)
{
}

void
ui_sb_update_text(void)
{
}

void
ui_sb_update_tip(UNUSED(int meaning))
{
}

void
ui_sb_update_icon(UNUSED(int tag), UNUSED(int active))
{
}

void
ui_sb_update_icon_write(UNUSED(int tag), UNUSED(int write))
{
}

void
ui_sb_update_icon_state(UNUSED(int tag), UNUSED(int state))
{
}

void
ui_sb_update_icon_wp(UNUSED(int tag), UNUSED(int state))
{
}

void
ui_sb_set_text(UNUSED(char *str))
{
}

void
ui_sb_bugui(UNUSED(char *str))
{
}

void
ui_sb_mt32lcd(UNUSED(char *str))
{
}

void
ui_update_force_interpreter(void)
{
}
