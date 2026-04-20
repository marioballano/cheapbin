#ifndef CHEAPBIN_STYLE_H
#define CHEAPBIN_STYLE_H

#include "composer.h"

/* ── Music style transformation types ──────────────────────────────── */

typedef enum {
    STYLE_NONE = 0,         /* No transformation — original composition    */
    STYLE_SYNTHWAVE,        /* Synthwave / Outrun                          */
    STYLE_DUNGEON_SYNTH,    /* Dungeon Synth / Dark Fantasy RPG            */
    STYLE_BAROQUE,          /* Baroque / Bach Counterpoint                 */
    STYLE_ACID_HOUSE,       /* Acid House / Minimal Techno                 */
    STYLE_DOOM_METAL,       /* Doom / Sludge Metal                         */
    STYLE_EUROBEAT,         /* Eurobeat / Trance                           */
    STYLE_DEMOSCENE,        /* Demoscene Tracker / Keygen Music            */
    STYLE_SKA_REGGAE,       /* 8-Bit Ska / Reggae                          */
    STYLE_TRAP_LOFI,        /* Trap / Lo-Fi Hip Hop                        */
    STYLE_PROG_MATH_ROCK,   /* Progressive / Math Rock                     */
    NUM_STYLE_TYPES
} StyleType;

/* ── Public API ────────────────────────────────────────────────────── */

/* Full human-readable style name. */
const char *style_name(StyleType type);

/* Short label for UI header display. */
const char *style_short_name(StyleType type);

/* Cycle to the next style type (wraps around). */
StyleType style_next(StyleType current);

/* Cycle to the previous style type (wraps around). */
StyleType style_prev(StyleType current);

/* Parse a style name from a CLI string.  Returns -1 on failure. */
int style_parse(const char *name);

/* Transform a composition in-place to apply the given style.
   Modifies events (waveforms, velocities, durations, drums, etc.),
   global_bpm, and swing.  May add or remove events.
   Does nothing if style == STYLE_NONE. */
void style_transform(Composition *comp, StyleType style);

#endif
