/* Audio sink for the libretro frontend, replacing sound/openal.c.

   86Box hands finished buffers to givealbuffer_common() from wherever the
   source happens to run: the PC timer inside cpu_exec() for the main mix, and
   worker threads for CD, floppy and hard disc sound. None of those may block,
   so each pushes into a shared accumulator ring that retro_run drains at a
   fixed number of frames per tick.

   OpenAL gave every source its own AL source and let the mixer line them up.
   Here each source keeps an absolute position in output frames and is
   resampled onto the ring as it arrives, which does the same job: the OPL
   (49716 Hz), the CD (44100 Hz) and the main mix (48000 Hz) all land in the
   one stream. */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <86box/86box.h>
#include <86box/midi.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>
#include <86box/sound.h>

#include "lr_ui.h"

/* Power of two so the wrap is a mask; ~1.4 s at 48 kHz. */
#define RING_FRAMES (1 << 16)
#define RING_MASK   (RING_FRAMES - 1)

/* How far ahead of the drain a source is placed when it first appears or after
   it has fallen behind. One drain's worth of slack, give or take. */
#define RING_LEAD 1024

static int32_t  ring[RING_FRAMES][2];
static uint64_t ring_read;
static uint64_t src_pos[I_MAX];
static double   src_frac[I_MAX];
static int      initialized;

/* givealbuffer_common() runs on several threads and the drain on ours, so the
   ring needs a lock. It is held only for the length of one buffer. */
#include <pthread.h>
static pthread_mutex_t ring_lock = PTHREAD_MUTEX_INITIALIZER;

void
al_set_midi(const int freq, const int buf_size)
{
    midi_freq     = freq;
    midi_buf_size = buf_size;
}

const char *
sound_get_output_devices(void)
{
    return NULL;
}

int
sound_get_device_sample_rate(UNUSED(const char *device_name))
{
    return sound_sample_rate;
}

int
sound_get_device_supported_rates(UNUSED(const char *device_name), int *rates_out, int max_rates)
{
    if ((rates_out == NULL) || (max_rates < 1))
        return 0;

    rates_out[0] = sound_sample_rate;
    return 1;
}

void
inital(void)
{
    if (initialized)
        return;

    src_freqs[I_NORMAL] = src_freqs[I_FDD] = src_freqs[I_HDD] = sound_sample_rate;
    src_freqs[I_MIDI]                                         = midi_freq;

    lr_audio_init();

    initialized = 1;
}

void
closeal(void)
{
    initialized = 0;
}

void
lr_audio_init(void)
{
    pthread_mutex_lock(&ring_lock);
    memset(ring, 0, sizeof(ring));
    memset(src_pos, 0, sizeof(src_pos));
    memset(src_frac, 0, sizeof(src_frac));
    ring_read = 0;
    pthread_mutex_unlock(&ring_lock);
}

void
lr_audio_close(void)
{
    lr_audio_init();
}

/* One interleaved stereo frame of the incoming buffer, as a pair of floats. */
static inline void
read_frame(const void *buf, const int frame, double *l, double *r)
{
    if (sound_is_float) {
        const float *f = (const float *) buf;

        *l = f[frame * 2];
        *r = f[(frame * 2) + 1];
    } else {
        const int16_t *s = (const int16_t *) buf;

        *l = (double) s[frame * 2] / 32768.0;
        *r = (double) s[(frame * 2) + 1] / 32768.0;
    }
}

void
givealbuffer_common(const void *buf, const uint8_t src, const int size)
{
    const int in_frames = size / 2;

    if (!initialized || (buf == NULL) || (in_frames <= 0) || (src >= I_MAX))
        return;

    const double gain  = sound_muted ? 0.0 : pow(10.0, (double) sound_gain / 20.0);
    const double ratio = (double) sound_sample_rate / (double) (src_freqs[src] ? src_freqs[src] : sound_sample_rate);

    pthread_mutex_lock(&ring_lock);

    /* A source that has fallen behind the drain, or has just started, is
       placed a little ahead of it rather than written into the past. */
    if ((src_pos[src] < ring_read) || (src_pos[src] > (ring_read + RING_FRAMES - 4096))) {
        src_pos[src]  = ring_read + RING_LEAD;
        src_frac[src] = 0.0;
    }

    for (int i = 0; i < in_frames; i++) {
        double l;
        double r;

        read_frame(buf, i, &l, &r);

        /* Hold each input frame for as many output frames as the rate ratio
           asks for; at 48 kHz in and out that is exactly one. */
        for (src_frac[src] += ratio; src_frac[src] >= 1.0; src_frac[src] -= 1.0) {
            const unsigned slot = (unsigned) (src_pos[src] & RING_MASK);

            ring[slot][0] += (int32_t) (l * gain * 32767.0);
            ring[slot][1] += (int32_t) (r * gain * 32767.0);
            src_pos[src]++;
        }
    }

    pthread_mutex_unlock(&ring_lock);
}

void
lr_audio_drain(int frames)
{
    int16_t out[1024 * 2];
    int     done = 0;

    while (done < frames) {
        int chunk = frames - done;

        if (chunk > (int) (sizeof(out) / sizeof(out[0]) / 2))
            chunk = sizeof(out) / sizeof(out[0]) / 2;

        pthread_mutex_lock(&ring_lock);
        for (int i = 0; i < chunk; i++) {
            const unsigned slot = (unsigned) (ring_read & RING_MASK);

            for (int ch = 0; ch < 2; ch++) {
                int32_t s = ring[slot][ch];

                ring[slot][ch] = 0;

                if (s < -32768)
                    s = -32768;
                else if (s > 32767)
                    s = 32767;

                out[(i * 2) + ch] = (int16_t) s;
            }

            ring_read++;
        }
        pthread_mutex_unlock(&ring_lock);

        lr_audio_batch_cb(out, chunk);
        done += chunk;
    }
}
