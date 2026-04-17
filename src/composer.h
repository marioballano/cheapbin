#ifndef CHEAPBIN_COMPOSER_H
#define CHEAPBIN_COMPOSER_H

#include <stddef.h>
#include <stdint.h>

/* Waveform types */
enum {
    WAVE_SQUARE   = 0,
    WAVE_TRIANGLE = 1,
    WAVE_SAWTOOTH = 2,
    WAVE_NOISE    = 3,
    WAVE_PULSE    = 4,   /* variable-width pulse */
};

/* 6-channel layout */
enum {
    CH_LEAD     = 0,
    CH_HARMONY  = 1,
    CH_BASS     = 2,
    CH_ARPEGGIO = 3,
    CH_PAD      = 4,
    CH_DRUMS    = 5,
    NUM_CHANNELS = 6,
};

/* Song section types */
enum {
    SEC_INTRO   = 0,
    SEC_VERSE   = 1,
    SEC_CHORUS  = 2,
    SEC_BRIDGE  = 3,
    SEC_OUTRO   = 4,
};

/* Drum types encoded in midi_note for drum channel */
enum {
    DRUM_KICK   = 36,
    DRUM_SNARE  = 38,
    DRUM_HIHAT  = 42,
    DRUM_CRASH  = 49,
    DRUM_REST   = 0,
};

typedef struct {
    int   tick;              /* absolute tick position (16th-note resolution) */
    int   midi_note;         /* MIDI note (or DRUM_* for drums) */
    int   duration_ticks;    /* length in ticks */
    int   velocity;          /* 0-127 */
    int   waveform;          /* WAVE_* */
    int   channel;           /* CH_* */
    float duty_cycle;        /* pulse width 0.125-0.75 */
    float vibrato_depth;     /* semitones of vibrato (0=none) */
    float vibrato_rate;      /* Hz of vibrato LFO */
    int   slide_from_note;   /* portamento: start sliding from this note (0=none) */
} MusicEvent;

typedef struct {
    char name[16];
    int  type;               /* SEC_* */
    int  start_tick;
    int  end_tick;
    float intensity;         /* 0.0-1.0 */
} SongSection;

typedef struct {
    MusicEvent  *events;
    int          num_events;
    int          capacity;

    SongSection *sections;
    int          num_sections;

    float        global_bpm;
    float        swing;          /* 0.0-0.35 */
    int          total_ticks;
} Composition;

int  compose(const uint8_t *data, size_t size, Composition *out);
void composition_free(Composition *comp);

#endif
