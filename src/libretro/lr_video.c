/* Video sink for the libretro frontend.

   86Box's video cards render into the monitor's `target_buffer` and call
   video_blit_memtoscreen(), which wakes that monitor's blit thread; the thread
   calls whatever video_setblit() registered. So the blit lands on a thread of
   its own, part way through retro_run, and is latched here for
   lr_video_present() to hand over at the end of the tick.

   No pixel conversion is needed: 86Box builds its pixels as
   b | (g << 8) | (r << 16), which is RETRO_PIXEL_FORMAT_XRGB8888. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <86box/86box.h>
#include <86box/plat.h>
#include <86box/ui.h>
#include <86box/video.h>

#include "lr_ui.h"

#define LR_MAX_WIDTH  2048
#define LR_MAX_HEIGHT 2048

static uint32_t       *screen;
static pthread_mutex_t screen_lock = PTHREAD_MUTEX_INITIALIZER;

/* The Qt/SDL frontends let the user pin the window to a fixed size; nothing
   here does, but the emulator reads them. */
int fixed_size_x = 640;
int fixed_size_y = 480;

static int frame_ready;
static int frame_w = 640;
static int frame_h = 480;

/* Geometry follows the blit: libretro wants base_width/base_height to be the
   framebuffer we hand over, with the display shape expressed separately as
   aspect_ratio. SET_GEOMETRY must not be issued from the blit thread, so a
   change is parked here and flushed by lr_video_present(). */
static int geom_w;
static int geom_h;
static int geom_pending;

static void
lr_blit(int x, int y, int w, int h, int monitor_index)
{
    /* Only the primary monitor is presented; a second head is still blitted
       so the card's handshake completes. */
    if ((monitor_index == 0) && (buffer32 != NULL) && (x >= 0) && (y >= 0) && (w > 0) && (h > 0) && (w <= LR_MAX_WIDTH) && (h <= LR_MAX_HEIGHT)) {
        pthread_mutex_lock(&screen_lock);

        for (int row = 0; row < h; row++)
            video_copy(&screen[row * LR_MAX_WIDTH], &(buffer32->line[y + row][x]), w * sizeof(uint32_t));

        if ((w != frame_w) || (h != frame_h)) {
            geom_w       = w;
            geom_h       = h;
            geom_pending = 1;
        }

        frame_w     = w;
        frame_h     = h;
        frame_ready = 1;

        pthread_mutex_unlock(&screen_lock);
    }

    video_blit_complete_monitor(monitor_index);
}

void
lr_video_init(void)
{
    if (screen == NULL)
        screen = calloc((size_t) LR_MAX_WIDTH * LR_MAX_HEIGHT, sizeof(uint32_t));

    frame_ready  = 0;
    geom_pending = 0;

    video_setblit(lr_blit);
}

void
lr_video_close(void)
{
    video_setblit(NULL);

    free(screen);
    screen = NULL;
}

void
lr_video_present(void)
{
    if (geom_pending) {
        struct retro_game_geometry geom;

        memset(&geom, 0, sizeof(geom));
        geom.base_width  = geom_w;
        geom.base_height = geom_h;
        geom.max_width   = LR_MAX_WIDTH;
        geom.max_height  = LR_MAX_HEIGHT;
        /* Every mode these machines produced was shown on a 4:3 monitor,
           whatever its pixel count. */
        geom.aspect_ratio = 4.0f / 3.0f;
        lr_environ_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geom);
        geom_pending = 0;
    }

    pthread_mutex_lock(&screen_lock);
    if (frame_ready) {
        lr_video_cb(screen, frame_w, frame_h, LR_MAX_WIDTH * sizeof(uint32_t));
        frame_ready = 0;
    } else {
        /* Emulated refresh is not locked to our tick rate, so ticks with no
           new frame are normal. */
        lr_video_cb(NULL, frame_w, frame_h, LR_MAX_WIDTH * sizeof(uint32_t));
    }
    pthread_mutex_unlock(&screen_lock);
}

/* These bracket pc_run() to serialise emulation against the render thread.
   There is no render thread here; retro_run is the only caller. */
void
startblit(void)
{
}

void
endblit(void)
{
}

/* The window is the frontend's. */
void
plat_resize(UNUSED(int w), UNUSED(int h), UNUSED(int monitor_index))
{
}

void
plat_resize_request(UNUSED(int w), UNUSED(int h), UNUSED(int monitor_index))
{
}

void
plat_mouse_capture(UNUSED(int on))
{
}

int
plat_vidapi(UNUSED(const char *name))
{
    return 0;
}

void
ui_init_monitor(UNUSED(int monitor_index))
{
}

void
ui_deinit_monitor(UNUSED(int monitor_index))
{
}
