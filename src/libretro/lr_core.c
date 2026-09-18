/* libretro entry points for 86Box.

   The SDL and Qt frontends run the emulation on a thread of their own, paced
   against wall-clock time, with the window and a 1 Hz statistics timer on the
   main thread. A libretro core owns none of that: retro_run is called by the
   frontend at the advertised rate, so the whole pacing layer collapses into an
   accumulator here.

   Content is an 86Box machine .cfg, handed to pc_init() as `-C` exactly the way
   `86Box -C foo.cfg` would, with `-P` pointed at the directory the .cfg sits in
   so that the images it names resolve beside it and the machine is one
   relocatable directory. ROMs come from the system directory. */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/keyboard.h>
#include <86box/mouse.h>
#include <86box/timer.h>
#include <86box/nvr.h>
#include <86box/path.h>
#include <86box/plat.h>
#include <86box/sound.h>
#include <86box/timer.h>
#include <86box/ui.h>
#include <86box/version.h>
#include <86box/video.h>

#include "cpu.h"
#include "lr_ui.h"

retro_environment_t        lr_environ_cb;
retro_video_refresh_t      lr_video_cb;
retro_audio_sample_batch_t lr_audio_batch_cb;
retro_input_poll_t         lr_input_poll_cb;
retro_input_state_t        lr_input_state_cb;
struct retro_log_callback  lr_log;

char lr_base_path[1024];

/* Globals the emulator expects its frontend to own. */
volatile int cpu_thread_run = 1;
int          rctrl_is_lalt;
int          update_icons;
int          kbd_req_capture;
int          hide_status_bar;
int          hide_tool_bar;
bool         fast_forward;

static char roms_dir[1024];
static char save_dir[1024];
static char vm_dir[1024];
static char cfg_file[1024];

static double target_fps = 60.0;
/* Emulated milliseconds still owed to pc_run(), which advances exactly one
   millisecond per call (or ten with force_10ms). */
static double emu_ms_owed;
static double onesec_owed;

static int game_loaded;
static int nvr_frames;

void
lr_log_printf(enum retro_log_level level, const char *fmt, ...)
{
    char    buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (lr_log.log)
        lr_log.log(level, "%s", buf);
    else
        fputs(buf, stderr);
}

/* --- frontend hooks the emulator calls -------------------------------- */

/* Called when the guest shuts itself down, and by plat_power_off(). */
void
do_stop(void)
{
    is_quit = 1;

    if (lr_environ_cb)
        lr_environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

void
do_start(void)
{
    is_quit = 0;
}

/* --- core options ------------------------------------------------------ */

static const struct retro_variable core_options[] = {
    /* Legacy v0 options on purpose: demarc answers GET_CORE_OPTIONS_VERSION
       with 0, so a v2 option array is rejected. */
    { "86box_fps", "Tick rate; 60|50|100" },
    { "86box_dynarec", "Dynamic recompiler; config|on|off" },
    { NULL, NULL },
};

static const char *
option_value(const char *key)
{
    struct retro_variable var = { key, NULL };

    if (lr_environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        return var.value;
    return NULL;
}

static void
apply_options(void)
{
    const char *v = option_value("86box_fps");
    double      fps = v ? atof(v) : 60.0;

    if (fps < 1.0)
        fps = 60.0;

    if ((fps != target_fps) && game_loaded) {
        struct retro_system_av_info av;

        target_fps = fps;
        retro_get_system_av_info(&av);
        lr_environ_cb(RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO, &av);
    } else
        target_fps = fps;

    v = option_value("86box_dynarec");
    if (v && !strcmp(v, "on"))
        cpu_use_dynarec = 1;
    else if (v && !strcmp(v, "off"))
        cpu_use_dynarec = 0;
}

/* --- paths -------------------------------------------------------------- */

static void
setup_paths(void)
{
    const char *sys_dir = NULL;
    const char *sav_dir = NULL;

    lr_environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sys_dir);
    if (!lr_environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &sav_dir) || !sav_dir)
        sav_dir = sys_dir;

    if (!sys_dir)
        sys_dir = ".";
    if (!sav_dir)
        sav_dir = ".";

    snprintf(roms_dir, sizeof(roms_dir), "%s/86box/roms/", sys_dir);
    snprintf(lr_base_path, sizeof(lr_base_path), "%s/86box/", sav_dir);

    snprintf(save_dir, sizeof(save_dir), "%s/86box/", sav_dir);
    plat_dir_create(save_dir);
}

/* --- libretro API ------------------------------------------------------- */

unsigned
retro_api_version(void)
{
    return RETRO_API_VERSION;
}

void
retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name    = "86Box";
    info->library_version = EMU_VERSION;
    info->valid_extensions = "cfg";
    /* The .cfg names disc images and hard disc files by path, so the core has
       to see a real file on disc, not a memory blob. */
    info->need_fullpath  = true;
    info->block_extract  = true;
}

void
retro_get_system_av_info(struct retro_system_av_info *info)
{
    memset(info, 0, sizeof(*info));
    info->geometry.base_width   = 640;
    info->geometry.base_height  = 480;
    info->geometry.max_width    = 2048;
    info->geometry.max_height   = 2048;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps            = target_fps;
    info->timing.sample_rate    = (double) (sound_sample_rate ? sound_sample_rate : 48000);
}

void
retro_set_environment(retro_environment_t cb)
{
    bool no_game = false;

    lr_environ_cb = cb;
    cb(RETRO_ENVIRONMENT_SET_VARIABLES, (void *) core_options);
    /* A machine .cfg is always required, so say so explicitly. */
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);
}

void
retro_set_video_refresh(retro_video_refresh_t cb)
{
    lr_video_cb = cb;
}

void
retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
    lr_audio_batch_cb = cb;
}

void
retro_set_input_poll(retro_input_poll_t cb)
{
    lr_input_poll_cb = cb;
}

void
retro_set_input_state(retro_input_state_t cb)
{
    lr_input_state_cb = cb;
}

/* Every sample goes out through the batch callback; this exists because the
   symbol is mandatory. */
void
retro_set_audio_sample(retro_audio_sample_t cb)
{
    (void) cb;
}

void
retro_set_controller_port_device(unsigned port, unsigned device)
{
    (void) port;
    (void) device;
}

static void
keyboard_event_trampoline(bool down, unsigned keycode, uint32_t character, uint16_t key_modifiers)
{
    lr_keyboard_event(down, keycode, character, key_modifiers);
}

void
retro_init(void)
{
    struct retro_keyboard_callback kb = { keyboard_event_trampoline };

    if (!lr_environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &lr_log))
        lr_log.log = NULL;

    setup_paths();

    lr_environ_cb(RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK, &kb);
}

bool
retro_load_game(const struct retro_game_info *game)
{
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    char                   *argv[8];
    int                     argc = 0;

    if (!game || !game->path) {
        lr_log_printf(RETRO_LOG_ERROR, "86Box needs a machine .cfg as content\n");
        return false;
    }

    if (!lr_environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        lr_log_printf(RETRO_LOG_ERROR, "XRGB8888 is not supported by this frontend\n");
        return false;
    }

    strncpy(cfg_file, game->path, sizeof(cfg_file) - 1);
    path_get_dirname(vm_dir, cfg_file);

    argv[argc++] = EMU_NAME;
    argv[argc++] = "-P";
    argv[argc++] = vm_dir;
    argv[argc++] = "-R";
    argv[argc++] = roms_dir;
    argv[argc++] = "-C";
    argv[argc++] = cfg_file;
    argv[argc]   = NULL;

    is_quit = 0;
    /* The .cfg is content handed to us by the frontend, not a VM of our own. */
    config_readonly = 1;

    if (!pc_init(argc, argv)) {
        lr_log_printf(RETRO_LOG_ERROR, "86Box could not read %s\n", cfg_file);
        return false;
    }

    if (!pc_init_roms()) {
        lr_log_printf(RETRO_LOG_ERROR, "No usable ROMs under %s\n", roms_dir);
        return false;
    }

    /* buffer32 and the blit sink must exist before the video card comes up. */
    lr_video_init();
    lr_input_init();

    pc_init_modules();
    pc_reset_hard_init();

    apply_options();
    plat_pause(0);

    emu_ms_owed = 0.0;
    onesec_owed = 0.0;
    nvr_frames  = 0;
    game_loaded = 1;

    return true;
}

bool
retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
    (void) type;
    (void) info;
    (void) num;
    return false;
}

void
retro_unload_game(void)
{
    if (!game_loaded)
        return;

    is_quit        = 1;
    cpu_thread_run = 0;

    pc_close(NULL);
    lr_video_close();
    lr_audio_close();

    game_loaded = 0;
}

void
retro_deinit(void)
{
    retro_unload_game();
    lr_environ_cb = NULL;
}

void
retro_reset(void)
{
    if (game_loaded)
        pc_reset_hard();
}

void
retro_run(void)
{
    bool updated = false;

    if (!game_loaded || is_quit)
        return;

    if (lr_environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
        apply_options();

    lr_input_poll_cb();
    lr_input_poll();

    /* pc_run() advances exactly one millisecond of emulated time (ten with
       force_10ms), and several device timers are built around that cadence, so
       pace the calls rather than resizing them. */
    const double step = force_10ms ? 10.0 : 1.0;

    emu_ms_owed += 1000.0 / target_fps;
    while (emu_ms_owed >= step) {
        pc_run();
        emu_ms_owed -= step;

        /* The SDL frontend saves the machine status every 2000 frames. */
        if ((++nvr_frames >= (force_10ms ? 200 : 2000)) && nvr_dosave) {
            nvr_save();
            nvr_dosave = 0;
            nvr_frames = 0;
        }
    }

    /* The SDL frontend drove this from a 1 Hz timer; it only refreshes the
       fps/speed counters. */
    onesec_owed += 1.0 / target_fps;
    if (onesec_owed >= 1.0) {
        pc_onesec();
        onesec_owed -= 1.0;
    }

    lr_video_present();
    lr_audio_drain((int) ((double) sound_sample_rate / target_fps));
}

/* --- unsupported, but mandatory ---------------------------------------- */

/* 86Box has no savestate support: every device keeps opaque private state and
   there is no serialisation anywhere in the tree. */
size_t
retro_serialize_size(void)
{
    return 0;
}

bool
retro_serialize(void *data, size_t size)
{
    (void) data;
    (void) size;
    return false;
}

bool
retro_unserialize(const void *data, size_t size)
{
    (void) data;
    (void) size;
    return false;
}

void
retro_cheat_reset(void)
{
}

void
retro_cheat_set(unsigned index, bool enabled, const char *code)
{
    (void) index;
    (void) enabled;
    (void) code;
}

void *
retro_get_memory_data(unsigned id)
{
    (void) id;
    return NULL;
}

size_t
retro_get_memory_size(unsigned id)
{
    (void) id;
    return 0;
}

unsigned
retro_get_region(void)
{
    return RETRO_REGION_NTSC;
}
