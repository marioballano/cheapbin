#include "audio.h"
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define NUM_BUFFERS  3
#define BUFFER_SIZE  4096   /* bytes per buffer (2048 int16_t samples) */
#define NUM_SAMPLES  (BUFFER_SIZE / (int)sizeof(int16_t))

static snd_pcm_t   *s_pcm;
static pthread_t     s_thread;
static SynthState   *s_synth;
static volatile int  s_running;
static volatile int  s_paused;

/* ── Audio thread ──────────────────────────────────────────────────── */

static void *audio_thread_func(void *arg)
{
    (void)arg;
    int16_t buf[NUM_SAMPLES];

    while (s_running) {
        if (s_paused) {
            usleep(10000);
            continue;
        }

        synth_render(s_synth, buf, NUM_SAMPLES);

        snd_pcm_sframes_t frames = snd_pcm_writei(s_pcm, buf, NUM_SAMPLES);
        if (frames < 0) {
            frames = snd_pcm_recover(s_pcm, (int)frames, 0);
            if (frames < 0) {
                fprintf(stderr, "error: ALSA write failed: %s\n",
                        snd_strerror((int)frames));
                break;
            }
        }
    }
    return NULL;
}

/* ── Public API ────────────────────────────────────────────────────── */

int audio_init(SynthState *synth)
{
    s_synth   = synth;
    s_running = 0;
    s_paused  = 0;

    int err = snd_pcm_open(&s_pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "error: cannot open audio device: %s\n",
                snd_strerror(err));
        return -1;
    }

    err = snd_pcm_set_params(s_pcm,
                             SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED,
                             1,             /* mono          */
                             SAMPLE_RATE,   /* 44100 Hz      */
                             1,             /* allow resample */
                             100000);       /* 100 ms latency */
    if (err < 0) {
        fprintf(stderr, "error: cannot configure audio: %s\n",
                snd_strerror(err));
        snd_pcm_close(s_pcm);
        return -1;
    }

    return 0;
}

int audio_start(void)
{
    s_running = 1;
    int err = pthread_create(&s_thread, NULL, audio_thread_func, NULL);
    if (err != 0) {
        fprintf(stderr, "error: cannot create audio thread\n");
        return -1;
    }
    return 0;
}

void audio_pause(void)
{
    s_paused = 1;
}

void audio_resume(void)
{
    s_paused = 0;
}

void audio_stop(void)
{
    s_running = 0;
    pthread_join(s_thread, NULL);
    snd_pcm_drain(s_pcm);
    snd_pcm_close(s_pcm);
}
