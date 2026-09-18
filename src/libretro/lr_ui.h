#ifndef LR_UI_H
#define LR_UI_H

#include <stdint.h>
#include "libretro.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Callbacks the core keeps hold of, set up in retro_set_*. */
extern retro_environment_t      lr_environ_cb;
extern retro_video_refresh_t    lr_video_cb;
extern retro_audio_sample_batch_t lr_audio_batch_cb;
extern retro_input_poll_t       lr_input_poll_cb;
extern retro_input_state_t      lr_input_state_cb;
extern struct retro_log_callback lr_log;

/* Where plat_get_exe_name() roots itself: the frontend's save directory. */
extern char lr_base_path[1024];

/* The frontend owns the window; this only keeps plat_pause() happy. */
extern char *ui_window_title(char *s);

void lr_log_printf(enum retro_log_level level, const char *fmt, ...);

/* video */
void lr_video_init(void);
void lr_video_close(void);
/* Hands the latched frame to the frontend, or signals a dupe when the emulated
   video card produced nothing this tick. Also flushes any pending geometry
   change, which is why it must run at a frame boundary. */
void lr_video_present(void);

/* sound */
void lr_audio_init(void);
void lr_audio_close(void);
/* Drains `frames` stereo frames from the ring into the frontend. */
void lr_audio_drain(int frames);

/* input */
void lr_input_init(void);
void lr_input_poll(void);
void lr_keyboard_event(bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers);

#ifdef __cplusplus
}
#endif

#endif /* LR_UI_H */
