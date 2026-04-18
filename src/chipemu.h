#ifndef CHEAPBIN_CHIPEMU_H
#define CHEAPBIN_CHIPEMU_H

#include <stdint.h>
#include <stddef.h>

/* ── Sound chip emulation types ────────────────────────────────────── */

typedef enum {
    CHIP_DEFAULT  = 0,   /* Original cheapbin sound (no chip emulation)    */
    CHIP_SID      = 1,   /* Commodore 64 SID — MOS 6581                   */
    CHIP_NES      = 2,   /* Ricoh 2A03 — NES / Famicom                    */
    CHIP_GENESIS  = 3,   /* Yamaha YM2612 — Sega Genesis / Mega Drive     */
    CHIP_SPECTRUM = 4,   /* General Instrument AY-3-8910 — ZX Spectrum    */
    NUM_CHIP_TYPES = 5
} ChipType;

/* ── Per-chip DSP state (lives inside SynthState) ──────────────────── */

typedef struct {
    ChipType type;

    /* SID: dual state-variable resonant filter + ring mod */
    float sid_bp;
    float sid_lp;
    float sid_cutoff;
    float sid_resonance;
    float sid_lfo_phase;
    float sid_ring_phase;        /* ring modulator oscillator */
    float sid_crackle;           /* analog drift noise */

    /* NES: hardware high-pass chain + nonlinear mixer */
    float nes_hp1_cap;           /* first-stage high-pass (37 Hz) */
    float nes_hp2_cap;           /* second-stage high-pass (667 Hz) */
    float nes_lp_state;          /* 14 kHz low-pass */
    float nes_dither;            /* dither accumulator for quantization */

    /* Genesis: FM operator state per channel */
    double fm_op1[6];            /* operator 1 feedback memory */
    double fm_op2[6];            /* operator 2 feedback memory */
    float  fm_mod_index;         /* base modulation depth */
    float  fm_feedback;          /* self-feedback amount */
    float  fm_lfo_phase;         /* hardware LFO (tremolo/vibrato) */
    float  fm_dac_error;         /* accumulated DAC quantization error */

    /* Spectrum: high-pass thinning filter */
    float ay_hp_prev_in;
    float ay_hp_prev_out;
    float ay_env_phase;
    float ay_env_speed;
} ChipState;

/* ── Public API ────────────────────────────────────────────────────── */

/* Deterministically select a chip based on file content. */
ChipType chip_select_from_data(const uint8_t *data, size_t size);

/* Full human-readable chip name.  e.g. "Commodore 64 SID (MOS 6581)" */
const char *chip_name(ChipType type);

/* Short label for UI display.  e.g. "SID", "2A03" */
const char *chip_short_name(ChipType type);

/* Initialize / reset chip DSP state for a given type. */
void chip_init(ChipState *cs, ChipType type);

/* Cycle to the next chip type (wraps around). */
ChipType chip_next(ChipType current);

/* Parse a chip name from a CLI string.  Returns -1 on failure. */
int chip_parse(const char *name);

#endif
