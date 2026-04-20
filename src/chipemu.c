#include "chipemu.h"
#include <string.h>
#include <strings.h>

/* ── Chip selection from file content ──────────────────────────────── */

ChipType chip_select_from_data(const uint8_t *data, size_t size)
{
    if (!data || size == 0) return CHIP_SID;

    /* FNV-1a hash over sampled file bytes for fast, uniform distribution */
    uint32_t hash = 0x811C9DC5u;
    size_t step = (size > 4096) ? (size / 4096) : 1;
    for (size_t i = 0; i < size; i += step) {
        hash ^= data[i];
        hash *= 0x01000193u;
    }
    /* Mix in total file size so truncated copies differ */
    hash ^= (uint32_t)(size & 0xFFFFFFFFu);
    hash *= 0x01000193u;

    /* Map to one of the 4 hardware chips (skip CHIP_DEFAULT) */
    return (ChipType)(1 + (hash % 4));
}

/* ── Names ─────────────────────────────────────────────────────────── */

const char *chip_name(ChipType type)
{
    switch (type) {
    case CHIP_SID:      return "MOS 6581 SID — Commodore 64";
    case CHIP_NES:      return "Ricoh 2A03 — Nintendo NES";
    case CHIP_GENESIS:  return "Yamaha YM2612 — Sega Genesis";
    case CHIP_SPECTRUM: return "AY-3-8910 — ZX Spectrum 128K";
    default:            return "Clean — No Chip Emulation";
    }
}

const char *chip_short_name(ChipType type)
{
    switch (type) {
    case CHIP_SID:      return "SID \xe2\x94\x82 C64";
    case CHIP_NES:      return "2A03 \xe2\x94\x82 NES";
    case CHIP_GENESIS:  return "YM2612 \xe2\x94\x82 Genesis";
    case CHIP_SPECTRUM: return "AY-3-8910 \xe2\x94\x82 ZX Spectrum";
    default:            return "Clean";
    }
}

/* ── Init ──────────────────────────────────────────────────────────── */

void chip_init(ChipState *cs, ChipType type)
{
    memset(cs, 0, sizeof(*cs));
    cs->type = type;

    switch (type) {
    case CHIP_SID:
        cs->sid_cutoff    = 0.45f;
        cs->sid_resonance = 0.75f;
        cs->sid_lfo_phase = 0.0f;
        cs->sid_ring_phase = 0.0f;
        cs->sid_crackle   = 0.0f;
        break;
    case CHIP_NES:
        cs->nes_hp1_cap = 0.0f;
        cs->nes_hp2_cap = 0.0f;
        cs->nes_lp_state = 0.0f;
        cs->nes_dither = 0.0f;
        break;
    case CHIP_GENESIS:
        cs->fm_mod_index = 3.5f;
        cs->fm_feedback  = 0.4f;
        cs->fm_lfo_phase = 0.0f;
        cs->fm_dac_error = 0.0f;
        break;
    case CHIP_SPECTRUM:
        cs->ay_env_speed = 0.0001f;
        break;
    default:
        break;
    }
}

/* ── Cycling ───────────────────────────────────────────────────────── */

ChipType chip_next(ChipType current)
{
    int next = (int)current + 1;
    if (next >= NUM_CHIP_TYPES) next = 0;
    return (ChipType)next;
}

ChipType chip_prev(ChipType current)
{
    int prev = (int)current - 1;
    if (prev < 0) prev = NUM_CHIP_TYPES - 1;
    return (ChipType)prev;
}

/* ── CLI parsing ───────────────────────────────────────────────────── */

int chip_parse(const char *name)
{
    if (!name) return -1;
    if (strcasecmp(name, "sid") == 0 || strcasecmp(name, "c64") == 0)
        return CHIP_SID;
    if (strcasecmp(name, "nes") == 0 || strcasecmp(name, "2a03") == 0 ||
        strcasecmp(name, "famicom") == 0)
        return CHIP_NES;
    if (strcasecmp(name, "genesis") == 0 || strcasecmp(name, "ym2612") == 0 ||
        strcasecmp(name, "megadrive") == 0)
        return CHIP_GENESIS;
    if (strcasecmp(name, "spectrum") == 0 || strcasecmp(name, "ay") == 0 ||
        strcasecmp(name, "ay-3-8910") == 0 || strcasecmp(name, "zx") == 0)
        return CHIP_SPECTRUM;
    if (strcasecmp(name, "default") == 0 || strcasecmp(name, "none") == 0 ||
        strcasecmp(name, "clean") == 0)
        return CHIP_DEFAULT;
    return -1;
}
