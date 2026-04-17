#ifndef CHEAPBIN_AUDIO_H
#define CHEAPBIN_AUDIO_H

#include "synth.h"

/* Initialize Core Audio playback for the given synth state.
   Returns 0 on success, -1 on error. */
int audio_init(SynthState *synth);

/* Start playback. */
int audio_start(void);

/* Pause / resume toggle. */
void audio_pause(void);
void audio_resume(void);

/* Stop playback and release resources. */
void audio_stop(void);

#endif
