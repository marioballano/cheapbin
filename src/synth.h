#ifndef CHEAPBIN_SYNTH_H
#define CHEAPBIN_SYNTH_H

#include "composer.h"
#include "chipemu.h"
#include "style.h"
#include <stdint.h>
#include <stdbool.h>

#define SAMPLE_RATE 44100
#define DELAY_SIZE 16384

typedef enum {
    ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE,
} EnvStage;

typedef struct {
    float attack_rate;
    float decay_rate;
    float sustain_level;
    float release_rate;
    float level;
    EnvStage stage;
} Envelope;

typedef struct {
    double phase;
    double freq;
    double freq_decay;
    float  env;
    float  env_decay;
    float  noise_mix;
    uint32_t noise_reg;
} DrumVoice;

typedef struct {
    double   phase;
    double   phase_inc;
    double   target_phase_inc;
    float    duty;
    float    duty_base;
    float    duty_sweep;
    int      waveform;
    Envelope env;
    float    volume;
    int      midi_note;
    int      samples_left;
    uint32_t noise_reg;
    bool     active;
    float    vib_depth;
    float    vib_rate;
    double   vib_phase;
    float    porta_speed;
    int      porta_samples;
} Channel;

typedef struct {
    Channel      channels[NUM_CHANNELS];
    DrumVoice    drum_voices[4];

    MusicEvent  *events;
    int          num_events;
    int          event_idx;
    int          current_tick;
    float        bpm;
    float        swing;
    int          samples_per_tick;
    int          tick_counter;
    bool         finished;
    bool         paused;

    float        delay_buf[DELAY_SIZE];
    int          delay_write;
    int          delay_read;
    float        delay_feedback;
    float        delay_mix;

    float        lpf_state;
    float        lpf_alpha;

    float        master_fade;
    float        fade_target;
    float        fade_speed;

    SongSection *sections;
    int          num_sections;
    int          current_section;
    char         section_name[16];

    float        ch_levels[NUM_CHANNELS];
    int          current_note;
    float        progress;

    ChipType     chip_type;
    ChipState    chip_state;

    StyleType    style_type;
    MusicEvent  *styled_events;    /* malloc'd transformed copy, or NULL */
} SynthState;

void synth_init(SynthState *s, Composition *comp);
void synth_set_chip(SynthState *s, ChipType chip);
void synth_apply_style(SynthState *s, StyleType style,
                       const Composition *original);
void synth_render(SynthState *s, int16_t *buffer, int num_samples);

#endif
