/*
 * Cross-platform audio backend on SDL2 — used for the macOS test build of
 * the SDL front-end. The Linux ARM64 GKD build links audio_alsa.c instead.
 */

#include "../audio.h"

#ifdef __APPLE__
#  include <SDL2/SDL.h>
#else
#  include <SDL.h>
#endif

#include <stdio.h>
#include <string.h>

#define SDL_AUDIO_BUF_SAMPLES 1024

static SDL_AudioDeviceID s_dev;
static SynthState       *s_synth;
static int               s_paused;

static void sdl_audio_callback(void *user, Uint8 *stream, int len)
{
    SynthState *synth = (SynthState *)user;
    int n = len / (int)sizeof(int16_t);
    if (s_paused) {
        memset(stream, 0, (size_t)len);
        return;
    }
    synth_render(synth, (int16_t *)stream, n);
}

int audio_init(SynthState *synth)
{
    s_synth  = synth;
    s_paused = 0;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "error: SDL_InitSubSystem(AUDIO): %s\n", SDL_GetError());
        return -1;
    }

    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    want.freq     = SAMPLE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = SDL_AUDIO_BUF_SAMPLES;
    want.callback = sdl_audio_callback;
    want.userdata = synth;

    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s_dev == 0) {
        fprintf(stderr, "error: SDL_OpenAudioDevice: %s\n", SDL_GetError());
        return -1;
    }
    return 0;
}

int audio_start(void)
{
    SDL_PauseAudioDevice(s_dev, 0);
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
    if (s_dev) {
        SDL_PauseAudioDevice(s_dev, 1);
        SDL_CloseAudioDevice(s_dev);
        s_dev = 0;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
