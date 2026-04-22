#include "composer.h"
#include "analyzer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── Scales (semitone offsets from root) ─────────────────────────────── */

static const int SCALES[NUM_SCALES][7] = {
    {0, 2, 4, 5, 7, 9, 11},   /* major */
    {0, 2, 3, 5, 7, 8, 10},   /* natural minor */
    {0, 2, 3, 5, 7, 9, 10},   /* dorian */
    {0, 2, 4, 5, 7, 9, 10},   /* mixolydian */
    {0, 2, 3, 5, 7, 8, 11},   /* harmonic minor */
    {0, 2, 4, 7, 9, 12, 14},  /* major pentatonic + ext */
    {0, 3, 5, 7, 10, 12, 15}, /* minor pentatonic + ext */
    {0, 1, 5, 7, 8, 12, 13},  /* japanese in-sen + ext */
    {0, 2, 3, 6, 7, 8, 11},   /* hungarian minor */
    {0, 1, 4, 5, 7, 8, 11},   /* double harmonic */
    {0, 2, 4, 6, 8, 10, 12},  /* whole tone (tonos enteros) — degree 6 is octave */
};

/* ── Chord progressions (scale degree indices 0-6) ──────────────────── */

static const int PROGRESSIONS[8][4] = {
    {0, 4, 5, 3},   /* I  V  vi  IV */
    {0, 3, 4, 4},   /* i  iv  V  V  */
    {0, 5, 3, 4},   /* I  vi  IV  V */
    {0, 2, 3, 4},   /* I  iii iv  V */
    {0, 3, 6, 4},   /* i  iv  VII V */
    {0, 4, 2, 3},   /* I  V  iii IV */
    {0, 1, 5, 4},   /* i  ii  vi  V */
    {0, 6, 5, 4},   /* I  VII vi  V */
};

/* ── Song structure ─────────────────────────────────────────────────── */

typedef struct { const char *name; int type; int bars; float intensity; } SectionDef;

static const SectionDef SONG_FORM[] = {
    {"INTRO",     SEC_INTRO,   4, 0.3f},
    {"VERSE A",   SEC_VERSE,   8, 0.5f},
    {"CHORUS",    SEC_CHORUS,  8, 0.9f},
    {"BRIDGE",    SEC_BRIDGE,  4, 0.6f},
    {"VERSE B",   SEC_VERSE,   8, 0.55f},
    {"CHORUS 2",  SEC_CHORUS,  8, 1.0f},
    {"OUTRO",     SEC_OUTRO,   4, 0.25f},
};
#define NUM_FORM_SECTIONS 7
#define TICKS_PER_BAR 16  /* 16th note resolution, 4/4 time */

/* ── Helpers ────────────────────────────────────────────────────────── */

static int scale_note(const int *scale, int root, int degree)
{
    /* degree can be negative or > 6, wraps with octave shifts */
    int oct = 0;
    int d = degree;
    while (d < 0) { d += 7; oct--; }
    while (d >= 7) { d -= 7; oct++; }
    return root + oct * 12 + scale[d];
}

static void push_event(Composition *c, MusicEvent ev)
{
    if (c->num_events >= c->capacity) {
        c->capacity = c->capacity ? c->capacity * 2 : 4096;
        c->events = realloc(c->events, (size_t)c->capacity * sizeof(MusicEvent));
    }
    c->events[c->num_events++] = ev;
}

static int ev_cmp(const void *a, const void *b)
{
    const MusicEvent *ea = (const MusicEvent *)a;
    const MusicEvent *eb = (const MusicEvent *)b;
    if (ea->tick != eb->tick) return ea->tick - eb->tick;
    return (int)ea->channel - (int)eb->channel;
}

/* ── Lead melody generator ──────────────────────────────────────────── */

static void gen_lead(Composition *c, const GlobalAnalysis *ga, const int *scale,
                     int root, const SongSection *sec)
{
    if (sec->type == SEC_OUTRO) return;

    /* During intro: only play lead in the last 2 bars as a teaser */
    int lead_start = sec->start_tick;
    if (sec->type == SEC_INTRO) {
        int total_bars = (sec->end_tick - sec->start_tick) / TICKS_PER_BAR;
        lead_start = sec->start_tick + (total_bars - 2) * TICKS_PER_BAR;
    }

    int motif_idx = (sec->type == SEC_CHORUS) ? 0 : (sec->type == SEC_BRIDGE) ? 2 : 1;
    int alt_motif = (sec->type == SEC_CHORUS) ? 3 : motif_idx;
    int base_oct = 60;  /* C4 */

    for (int tick = lead_start; tick < sec->end_tick; tick++) {
        int step = tick % 16;
        if (!((ga->melody_rhythm >> (15 - step)) & 1)) continue;

        int bar_in_section = (tick - sec->start_tick) / TICKS_PER_BAR;
        int mi = (bar_in_section % 2 == 0) ? motif_idx : alt_motif;
        int note_idx = (tick / 2) % 8;
        int degree = ga->motifs[mi][note_idx] % 14 - 3;  /* -3 to +10 range */

        int note = scale_note(scale, base_oct + root, degree);
        if (note < 48) note += 12;
        if (note > 84) note -= 12;

        /* portamento: slide from previous note sometimes */
        int slide = 0;
        if (step > 0 && (ga->motifs[mi][(note_idx + 3) % 8] % 3 == 0)) {
            int prev_deg = ga->motifs[mi][(note_idx + 7) % 8] % 14 - 3;
            slide = scale_note(scale, base_oct + root, prev_deg);
            if (slide < 48) slide += 12;
            if (slide > 84) slide -= 12;
        }

        float vib = (sec->intensity > 0.7f) ? ga->vibrato_depth : ga->vibrato_depth * 0.3f;
        float vib_rate = 4.5f + sec->intensity * 2.0f;

        push_event(c, (MusicEvent){
            .tick           = tick,
            .midi_note      = note,
            .duration_ticks = 2 + (ga->motifs[mi][note_idx] % 3),
            .velocity       = (int)(80.0f + sec->intensity * 47.0f),
            .waveform       = WAVE_SQUARE,
            .channel        = CH_LEAD,
            .duty_cycle     = ga->duty_base,
            .vibrato_depth  = vib,
            .vibrato_rate   = vib_rate,
            .slide_from_note = slide,
        });
    }
}

/* ── Harmony generator (chorus/bridge only) ─────────────────────────── */

static void gen_harmony(Composition *c, const GlobalAnalysis *ga, const int *scale,
                        int root, const int *prog, const SongSection *sec)
{
    if (sec->type != SEC_CHORUS && sec->type != SEC_BRIDGE) return;

    int base_oct = 60;
    for (int tick = sec->start_tick; tick < sec->end_tick; tick += 4) {
        int bar = (tick - sec->start_tick) / TICKS_PER_BAR;
        int chord_idx = bar % 4;
        int chord_root_deg = prog[chord_idx];
        int third_deg = chord_root_deg + 2;

        int note = scale_note(scale, base_oct + root, third_deg);
        if (note < 55) note += 12;
        if (note > 79) note -= 12;

        push_event(c, (MusicEvent){
            .tick           = tick,
            .midi_note      = note,
            .duration_ticks = 4,
            .velocity       = (int)(50.0f + sec->intensity * 30.0f),
            .waveform       = WAVE_PULSE,
            .channel        = CH_HARMONY,
            .duty_cycle     = ga->duty_base + 0.15f,
            .vibrato_depth  = ga->vibrato_depth * 0.5f,
            .vibrato_rate   = 3.0f,
            .slide_from_note = 0,
        });
    }
}

/* ── Bass generator ─────────────────────────────────────────────────── */

static void gen_bass(Composition *c, const GlobalAnalysis *ga, const int *scale,
                     int root, const int *prog, const SongSection *sec)
{

    int base_oct = 36;  /* C2 */
    (void)ga;

    for (int tick = sec->start_tick; tick < sec->end_tick; tick++) {
        int step = tick % 16;
        int bar = (tick - sec->start_tick) / TICKS_PER_BAR;
        int chord_root_deg = prog[bar % 4];

        /* bass pattern: root on 1, fifth on 5/9, passing tone on 13 */
        /* during intro: only root notes on beat 1 */
        int note = 0;
        int dur = 2;
        if (sec->type == SEC_INTRO) {
            if (step == 0) {
                note = scale_note(scale, base_oct + root, chord_root_deg);
                dur = 8;
            } else {
                continue;
            }
        } else if (step == 0) {
            note = scale_note(scale, base_oct + root, chord_root_deg);
            dur = 4;
        } else if (step == 4 || step == 8) {
            note = scale_note(scale, base_oct + root, chord_root_deg + 4);
            dur = 2;
        } else if (step == 12) {
            /* passing tone: one degree below next chord root */
            int next_deg = prog[(bar + 1) % 4];
            note = scale_note(scale, base_oct + root, next_deg - 1);
            dur = 2;
        } else {
            continue;
        }

        if (note < 30) note += 12;
        if (note > 55) note -= 12;

        push_event(c, (MusicEvent){
            .tick           = tick,
            .midi_note      = note,
            .duration_ticks = dur,
            .velocity       = (int)(80.0f + sec->intensity * 30.0f),
            .waveform       = WAVE_TRIANGLE,
            .channel        = CH_BASS,
            .duty_cycle     = 0.5f,
            .vibrato_depth  = 0.0f,
            .vibrato_rate   = 0.0f,
            .slide_from_note = 0,
        });
    }
}

/* ── Arpeggio generator ─────────────────────────────────────────────── */

static void gen_arp(Composition *c, const GlobalAnalysis *ga, const int *scale,
                    int root, const int *prog, const SongSection *sec)
{
    if (sec->type == SEC_OUTRO) return;
    if (sec->type == SEC_BRIDGE) return;

    int base_oct = 60;
    int shape = ga->style;  /* 0=up, 1=down, 2=updown, 3=random-ish */

    for (int tick = sec->start_tick; tick < sec->end_tick; tick++) {
        int step = tick % 16;
        if (!((ga->arp_rhythm >> (15 - step)) & 1)) continue;

        int bar = (tick - sec->start_tick) / TICKS_PER_BAR;
        int chord_root_deg = prog[bar % 4];

        /* arp notes: root, third, fifth, octave */
        int arp_degs[4] = {chord_root_deg, chord_root_deg + 2,
                          chord_root_deg + 4, chord_root_deg + 7};
        int arp_idx;
        int pos = tick % 4;

        switch (shape) {
        case 0: arp_idx = pos; break;
        case 1: arp_idx = 3 - pos; break;
        case 2: arp_idx = (pos < 2) ? pos : 4 - pos; break;
        default: arp_idx = (tick * 3 + ga->motifs[0][tick % 8]) % 4; break;
        }

        int note = scale_note(scale, base_oct + root, arp_degs[arp_idx]);
        if (note < 55) note += 12;
        if (note > 84) note -= 12;

        push_event(c, (MusicEvent){
            .tick           = tick,
            .midi_note      = note,
            .duration_ticks = 1,
            .velocity       = (int)(45.0f + sec->intensity * 35.0f),
            .waveform       = WAVE_SQUARE,
            .channel        = CH_ARPEGGIO,
            .duty_cycle     = 0.25f,
            .vibrato_depth  = 0.0f,
            .vibrato_rate   = 0.0f,
            .slide_from_note = 0,
        });
    }
}

/* ── Pad generator (sustained chords) ───────────────────────────────── */

static void gen_pad(Composition *c, const GlobalAnalysis *ga, const int *scale,
                    int root, const int *prog, const SongSection *sec)
{
    if (sec->type == SEC_OUTRO) return;
    (void)ga;

    int base_oct = 48;  /* C3 */

    for (int bar = 0; bar < (sec->end_tick - sec->start_tick) / TICKS_PER_BAR; bar++) {
        int tick = sec->start_tick + bar * TICKS_PER_BAR;
        int chord_root_deg = prog[bar % 4];

        /* play root + third of chord as sustained notes */
        int notes[2] = {
            scale_note(scale, base_oct + root, chord_root_deg),
            scale_note(scale, base_oct + root, chord_root_deg + 2),
        };

        for (int n = 0; n < 2; n++) {
            if (notes[n] < 40) notes[n] += 12;
            if (notes[n] > 72) notes[n] -= 12;

            push_event(c, (MusicEvent){
                .tick           = tick,
                .midi_note      = notes[n],
                .duration_ticks = TICKS_PER_BAR - 1,
                .velocity       = (int)(30.0f + sec->intensity * 25.0f),
                .waveform       = WAVE_SAWTOOTH,
                .channel        = CH_PAD,
                .duty_cycle     = 0.5f,
                .vibrato_depth  = 0.0f,
                .vibrato_rate   = 0.0f,
                .slide_from_note = 0,
            });
        }
    }
}

/* ── Drum generator ─────────────────────────────────────────────────── */

static void gen_drums(Composition *c, const GlobalAnalysis *ga, const SongSection *sec)
{

    int last_bar = (sec->end_tick - sec->start_tick) / TICKS_PER_BAR - 1;

    for (int tick = sec->start_tick; tick < sec->end_tick; tick++) {
        int step = tick % 16;
        int bar = (tick - sec->start_tick) / TICKS_PER_BAR;
        int is_fill = (bar == last_bar && step >= 12);
        int is_first_beat = (tick == sec->start_tick && sec->type == SEC_CHORUS);

        /* crash on chorus entry */
        if (is_first_beat) {
            push_event(c, (MusicEvent){
                .tick = tick, .midi_note = DRUM_CRASH, .duration_ticks = 8,
                .velocity = 110, .waveform = WAVE_NOISE, .channel = CH_DRUMS,
                .duty_cycle = 0.5f,
            });
        }

        /* kick — skip during intro except last 2 bars for build-up */
        if ((ga->kick_pattern >> (15 - step)) & 1) {
            if (sec->type == SEC_INTRO && bar < last_bar - 1) goto skip_kick;
            int vel = is_fill ? 120 : (int)(80.0f + sec->intensity * 35.0f);
            if (sec->type == SEC_INTRO) vel = (int)(50.0f + 20.0f * (float)(bar - (last_bar - 1)));
            push_event(c, (MusicEvent){
                .tick = tick, .midi_note = DRUM_KICK, .duration_ticks = 2,
                .velocity = vel, .waveform = WAVE_NOISE, .channel = CH_DRUMS,
                .duty_cycle = 0.5f,
            });
        }
        skip_kick:
        /* snare — skip during intro */
        if ((ga->snare_pattern >> (15 - step)) & 1 && sec->type != SEC_INTRO) {
            int vel = is_fill ? 115 : (int)(75.0f + sec->intensity * 35.0f);
            push_event(c, (MusicEvent){
                .tick = tick, .midi_note = DRUM_SNARE, .duration_ticks = 2,
                .velocity = vel, .waveform = WAVE_NOISE, .channel = CH_DRUMS,
                .duty_cycle = 0.5f,
            });
        }
        /* hi-hat */
        if ((ga->hat_pattern >> (15 - step)) & 1) {
            push_event(c, (MusicEvent){
                .tick = tick, .midi_note = DRUM_HIHAT, .duration_ticks = 1,
                .velocity = (int)(40.0f + sec->intensity * 30.0f),
                .waveform = WAVE_NOISE, .channel = CH_DRUMS,
                .duty_cycle = 0.5f,
            });
        }

        /* fills: extra snare rolls at section end */
        if (is_fill && (step % 2 == 0)) {
            push_event(c, (MusicEvent){
                .tick = tick, .midi_note = DRUM_SNARE, .duration_ticks = 1,
                .velocity = 100, .waveform = WAVE_NOISE, .channel = CH_DRUMS,
                .duty_cycle = 0.5f,
            });
        }
    }
}

/* ── Main compose ───────────────────────────────────────────────────── */

int compose(const uint8_t *data, size_t size, Composition *out)
{
    return compose_with_scale(data, size, -1, out);
}

int compose_with_scale(const uint8_t *data, size_t size,
                       int scale_override, Composition *out)
{
    memset(out, 0, sizeof(*out));
    if (size == 0) return -1;

    /* Analyze the binary */
    GlobalAnalysis ga;
    analyze_global(data, size, &ga);

    if (scale_override >= 0 && scale_override < NUM_SCALES)
        ga.scale_index = scale_override;

    const int *scale = SCALES[ga.scale_index];
    const int *prog  = PROGRESSIONS[ga.progression_index];
    int root = ga.root_note;

    out->global_bpm  = ga.base_tempo;
    out->swing       = ga.swing;
    out->scale_index = ga.scale_index;

    /* Build song sections */
    out->num_sections = NUM_FORM_SECTIONS;
    out->sections = malloc((size_t)NUM_FORM_SECTIONS * sizeof(SongSection));

    int tick = 0;
    for (int i = 0; i < NUM_FORM_SECTIONS; i++) {
        const SectionDef *def = &SONG_FORM[i];
        SongSection *sec = &out->sections[i];
        strncpy(sec->name, def->name, sizeof(sec->name) - 1);
        sec->name[sizeof(sec->name) - 1] = '\0';
        sec->type       = def->type;
        sec->start_tick = tick;
        sec->end_tick   = tick + def->bars * TICKS_PER_BAR;
        sec->intensity  = def->intensity;
        tick = sec->end_tick;
    }
    out->total_ticks = tick;

    /* Generate events for each section */
    for (int i = 0; i < NUM_FORM_SECTIONS; i++) {
        SongSection *sec = &out->sections[i];
        gen_lead(out, &ga, scale, root, sec);
        gen_harmony(out, &ga, scale, root, prog, sec);
        gen_bass(out, &ga, scale, root, prog, sec);
        gen_arp(out, &ga, scale, root, prog, sec);
        gen_pad(out, &ga, scale, root, prog, sec);
        gen_drums(out, &ga, sec);
    }

    /* Sort events by tick */
    if (out->num_events > 1)
        qsort(out->events, (size_t)out->num_events, sizeof(MusicEvent), ev_cmp);

    return 0;
}

void composition_free(Composition *comp)
{
    free(comp->events);
    free(comp->sections);
    memset(comp, 0, sizeof(*comp));
}

/* ── Scale metadata ─────────────────────────────────────────────────── */

const char *scale_name(ScaleType type)
{
    switch (type) {
    case SCALE_MAJOR:            return "Major";
    case SCALE_NATURAL_MINOR:    return "Natural Minor";
    case SCALE_DORIAN:           return "Dorian";
    case SCALE_MIXOLYDIAN:       return "Mixolydian";
    case SCALE_HARMONIC_MINOR:   return "Harmonic Minor";
    case SCALE_MAJOR_PENTATONIC: return "Major Pentatonic";
    case SCALE_MINOR_PENTATONIC: return "Minor Pentatonic";
    case SCALE_JAPANESE_IN_SEN:  return "Japanese In-Sen";
    case SCALE_HUNGARIAN_MINOR:  return "Hungarian Minor";
    case SCALE_DOUBLE_HARMONIC:  return "Double Harmonic";
    case SCALE_WHOLE_TONE:       return "WHOLE_TONE";
    default:                     return "?";
    }
}

const char *scale_short_name(ScaleType type)
{
    switch (type) {
    case SCALE_MAJOR:            return "Major";
    case SCALE_NATURAL_MINOR:    return "Minor";
    case SCALE_DORIAN:           return "Dorian";
    case SCALE_MIXOLYDIAN:       return "Mixo";
    case SCALE_HARMONIC_MINOR:   return "HarmMin";
    case SCALE_MAJOR_PENTATONIC: return "MajPent";
    case SCALE_MINOR_PENTATONIC: return "MinPent";
    case SCALE_JAPANESE_IN_SEN:  return "In-Sen";
    case SCALE_HUNGARIAN_MINOR:  return "HunMin";
    case SCALE_DOUBLE_HARMONIC:  return "DblHarm";
    case SCALE_WHOLE_TONE:       return "WholeTn";
    default:                     return "?";
    }
}

ScaleType scale_next(ScaleType current)
{
    int next = (int)current + 1;
    if (next >= NUM_SCALES) next = 0;
    return (ScaleType)next;
}

ScaleType scale_prev(ScaleType current)
{
    int prev = (int)current - 1;
    if (prev < 0) prev = NUM_SCALES - 1;
    return (ScaleType)prev;
}

int scale_parse(const char *name)
{
    if (!name) return -1;
    if (strcasecmp(name, "major") == 0)                   return SCALE_MAJOR;
    if (strcasecmp(name, "minor") == 0 ||
        strcasecmp(name, "natural-minor") == 0)           return SCALE_NATURAL_MINOR;
    if (strcasecmp(name, "dorian") == 0)                  return SCALE_DORIAN;
    if (strcasecmp(name, "mixolydian") == 0 ||
        strcasecmp(name, "mixo") == 0)                    return SCALE_MIXOLYDIAN;
    if (strcasecmp(name, "harmonic-minor") == 0 ||
        strcasecmp(name, "harmonic") == 0)                return SCALE_HARMONIC_MINOR;
    if (strcasecmp(name, "major-pentatonic") == 0 ||
        strcasecmp(name, "majpent") == 0)                 return SCALE_MAJOR_PENTATONIC;
    if (strcasecmp(name, "minor-pentatonic") == 0 ||
        strcasecmp(name, "pentatonic") == 0 ||
        strcasecmp(name, "minpent") == 0)                 return SCALE_MINOR_PENTATONIC;
    if (strcasecmp(name, "in-sen") == 0 ||
        strcasecmp(name, "japanese") == 0)                return SCALE_JAPANESE_IN_SEN;
    if (strcasecmp(name, "hungarian-minor") == 0 ||
        strcasecmp(name, "hungarian") == 0)               return SCALE_HUNGARIAN_MINOR;
    if (strcasecmp(name, "double-harmonic") == 0)         return SCALE_DOUBLE_HARMONIC;
    if (strcasecmp(name, "whole-tone") == 0 ||
        strcasecmp(name, "wholetone") == 0 ||
        strcasecmp(name, "tonos-enteros") == 0 ||
        strcasecmp(name, "whole_tone") == 0)              return SCALE_WHOLE_TONE;
    return -1;
}
