#ifndef CHEAPBIN_ANALYZER_H
#define CHEAPBIN_ANALYZER_H

#include <stddef.h>
#include <stdint.h>

#define SECTION_SIZE 256

typedef struct {
    double entropy;
    double mean;
    double variance;
    double zero_ratio;
} SectionStats;

/* Global musical DNA extracted directly from binary byte content */
typedef struct {
    int   scale_index;          /* 0-9: which musical scale */
    int   progression_index;    /* 0-7: which chord progression */
    float base_tempo;           /* 100-180 BPM */
    float swing;                /* 0.0-0.35 groove amount */
    int   root_note;            /* 0-11: semitone offset for key */
    int   style;                /* 0-3: chill/driving/dark/epic */

    int   motifs[4][8];         /* 4 melodic motifs, 8 scale degrees each */

    uint16_t kick_pattern;      /* 16-step bit patterns */
    uint16_t snare_pattern;
    uint16_t hat_pattern;

    uint16_t melody_rhythm;     /* which of 16 steps have melody notes */
    uint16_t arp_rhythm;        /* which of 16 steps have arp notes */

    float duty_base;            /* 0.125-0.625 base pulse width */
    float vibrato_depth;        /* 0.05-0.4 semitones */
    float echo_feedback;        /* 0.15-0.45 */
} GlobalAnalysis;

void analyze_section(const uint8_t *data, size_t len, SectionStats *out);
void analyze_global(const uint8_t *data, size_t size, GlobalAnalysis *out);

#endif
