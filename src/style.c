#include "style.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

/* ── Helpers ────────────────────────────────────────────────────────── */

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void clamp_vel(MusicEvent *ev)
{
    if (ev->velocity > 127) ev->velocity = 127;
    if (ev->velocity < 1)   ev->velocity = 1;
}

static void clamp_note(MusicEvent *ev)
{
    while (ev->midi_note < 24)  ev->midi_note += 12;
    while (ev->midi_note > 108) ev->midi_note -= 12;
}

static void push_ev(Composition *c, MusicEvent ev)
{
    if (c->num_events >= c->capacity) {
        c->capacity = c->capacity ? c->capacity * 2 : 4096;
        c->events = realloc(c->events, (size_t)c->capacity * sizeof(MusicEvent));
    }
    c->events[c->num_events++] = ev;
}

static void remove_channel(Composition *c, int channel)
{
    int w = 0;
    for (int i = 0; i < c->num_events; i++) {
        if (c->events[i].channel != channel)
            c->events[w++] = c->events[i];
    }
    c->num_events = w;
}

static int ev_cmp(const void *a, const void *b)
{
    const MusicEvent *ea = (const MusicEvent *)a;
    const MusicEvent *eb = (const MusicEvent *)b;
    if (ea->tick != eb->tick) return ea->tick - eb->tick;
    return (int)ea->channel - (int)eb->channel;
}

/* ── Drum pattern rebuilder ─────────────────────────────────────────── */

static void rebuild_drums(Composition *c,
                          uint16_t kick_pat, uint16_t snare_pat,
                          uint16_t hat_pat,
                          int kick_vel, int snare_vel, int hat_vel)
{
    remove_channel(c, CH_DRUMS);

    for (int s = 0; s < c->num_sections; s++) {
        SongSection *sec = &c->sections[s];
        float sv = 0.6f + sec->intensity * 0.4f;

        for (int tick = sec->start_tick; tick < sec->end_tick; tick++) {
            int step = tick % 16;

            if (kick_vel > 0 && ((kick_pat >> (15 - step)) & 1)) {
                push_ev(c, (MusicEvent){
                    .tick = tick, .midi_note = DRUM_KICK,
                    .duration_ticks = 2,
                    .velocity = (int)((float)kick_vel * sv),
                    .waveform = WAVE_NOISE, .channel = CH_DRUMS,
                    .duty_cycle = 0.5f,
                });
            }
            if (snare_vel > 0 && ((snare_pat >> (15 - step)) & 1)) {
                push_ev(c, (MusicEvent){
                    .tick = tick, .midi_note = DRUM_SNARE,
                    .duration_ticks = 2,
                    .velocity = (int)((float)snare_vel * sv),
                    .waveform = WAVE_NOISE, .channel = CH_DRUMS,
                    .duty_cycle = 0.5f,
                });
            }
            if (hat_vel > 0 && ((hat_pat >> (15 - step)) & 1)) {
                push_ev(c, (MusicEvent){
                    .tick = tick, .midi_note = DRUM_HIHAT,
                    .duration_ticks = 1,
                    .velocity = (int)((float)hat_vel * sv),
                    .waveform = WAVE_NOISE, .channel = CH_DRUMS,
                    .duty_cycle = 0.5f,
                });
            }
        }
    }
}

/* ════════════════════════════════════════════════════════════════════════
   Style transforms — each modifies a Composition in-place.
   ════════════════════════════════════════════════════════════════════════ */

/* ── 1. Synthwave / Outrun ──────────────────────────────────────────── *
 * Driving 80s retro-futuristic: pulsating sawtooth bass, lush pulse
 * leads with deep vibrato, bright arpeggios, rigid 4-on-the-floor.    */

static void transform_synthwave(Composition *c)
{
    c->global_bpm = clampf(c->global_bpm, 118.0f, 130.0f);
    c->swing = 0.02f;

    for (int i = 0; i < c->num_events; i++) {
        MusicEvent *ev = &c->events[i];
        if (ev->channel == CH_DRUMS) continue;

        switch (ev->channel) {
        case CH_BASS:
            ev->waveform = WAVE_SAWTOOTH;
            ev->duration_ticks = (int)((float)ev->duration_ticks * 1.8f);
            if (ev->duration_ticks < 2) ev->duration_ticks = 2;
            ev->velocity = (int)((float)ev->velocity * 1.15f);
            ev->vibrato_depth = 0.05f;
            ev->vibrato_rate = 2.0f;
            break;
        case CH_LEAD:
            ev->waveform = WAVE_PULSE;
            ev->duty_cycle = 0.45f;
            ev->vibrato_depth = 0.30f;
            ev->vibrato_rate = 3.5f;
            break;
        case CH_PAD:
            ev->waveform = WAVE_SAWTOOTH;
            ev->duration_ticks += 4;
            ev->velocity = (int)((float)ev->velocity * 1.3f);
            ev->vibrato_depth = 0.10f;
            ev->vibrato_rate = 2.0f;
            break;
        case CH_ARPEGGIO:
            ev->waveform = WAVE_PULSE;
            ev->duty_cycle = 0.25f;
            if (ev->midi_note < 72) ev->midi_note += 12;
            break;
        case CH_HARMONY:
            ev->waveform = WAVE_PULSE;
            ev->duty_cycle = 0.35f;
            ev->vibrato_depth = 0.15f;
            ev->vibrato_rate = 4.0f;
            break;
        }
        clamp_vel(ev);
        clamp_note(ev);
    }

    /* driving 4-on-the-floor, snare 2&4, busy 8th hats */
    rebuild_drums(c, 0x8888, 0x0808, 0xAAAA, 105, 90, 55);
}

/* ── 2. Dungeon Synth / Dark Fantasy RPG ────────────────────────────── *
 * Slow, atmospheric, eerie.  Dark minor-key melodies with warm,
 * slowly evolving synthetic choirs/pads.  Sparse, haunting lead.      */

static void transform_dungeon_synth(Composition *c)
{
    c->global_bpm = clampf(c->global_bpm * 0.55f, 60.0f, 82.0f);
    c->swing = 0.03f;

    for (int i = 0; i < c->num_events; i++) {
        MusicEvent *ev = &c->events[i];
        if (ev->channel == CH_DRUMS) continue;

        switch (ev->channel) {
        case CH_LEAD:
            ev->waveform = WAVE_TRIANGLE;
            ev->velocity = (int)((float)ev->velocity * 0.55f);
            ev->duration_ticks = (int)((float)ev->duration_ticks * 1.5f);
            if (ev->duration_ticks < 2) ev->duration_ticks = 2;
            ev->vibrato_depth = 0.35f;
            ev->vibrato_rate = 2.5f;
            break;
        case CH_BASS:
            ev->waveform = WAVE_TRIANGLE;
            ev->duration_ticks = (int)((float)ev->duration_ticks * 2.0f);
            if (ev->duration_ticks < 3) ev->duration_ticks = 3;
            ev->velocity = (int)((float)ev->velocity * 0.8f);
            if (ev->midi_note > 48) ev->midi_note -= 12;
            break;
        case CH_PAD:
            ev->waveform = WAVE_SAWTOOTH;
            ev->duration_ticks = (int)((float)ev->duration_ticks * 2.5f);
            ev->velocity = (int)((float)ev->velocity * 1.4f);
            ev->vibrato_depth = 0.20f;
            ev->vibrato_rate = 1.5f;
            break;
        case CH_ARPEGGIO:
            ev->velocity = (int)((float)ev->velocity * 0.3f);
            ev->duration_ticks *= 3;
            ev->waveform = WAVE_TRIANGLE;
            break;
        case CH_HARMONY:
            ev->waveform = WAVE_SAWTOOTH;
            ev->velocity = (int)((float)ev->velocity * 0.6f);
            ev->duration_ticks *= 2;
            ev->vibrato_depth = 0.25f;
            ev->vibrato_rate = 2.0f;
            break;
        }
        clamp_vel(ev);
        clamp_note(ev);
    }

    /* minimal, sparse drums — just a slow kick pulse */
    rebuild_drums(c, 0x8000, 0x0000, 0x0000, 60, 0, 0);
}

/* ── 3. Baroque / Bach Counterpoint ─────────────────────────────────── *
 * Highly structured: thin harpsichord-like pulse tones, strict timing,
 * canon/fugue effect by echoing lead on harmony, active arpeggios,
 * walking bass, minimal percussion.                                    */

static void transform_baroque(Composition *c)
{
    c->global_bpm = clampf(c->global_bpm, 110.0f, 135.0f);
    c->swing = 0.0f;

    int orig_count = c->num_events;

    for (int i = 0; i < orig_count; i++) {
        MusicEvent *ev = &c->events[i];
        if (ev->channel == CH_DRUMS) continue;

        switch (ev->channel) {
        case CH_LEAD:
            ev->waveform = WAVE_PULSE;
            ev->duty_cycle = 0.125f;
            if (ev->duration_ticks > 2) ev->duration_ticks--;
            ev->vibrato_depth = 0.0f;
            ev->slide_from_note = 0;
            break;
        case CH_BASS:
            ev->waveform = WAVE_TRIANGLE;
            ev->vibrato_depth = 0.0f;
            ev->slide_from_note = 0;
            break;
        case CH_PAD:
            ev->waveform = WAVE_PULSE;
            ev->duty_cycle = 0.4f;
            ev->vibrato_depth = 0.0f;
            break;
        case CH_ARPEGGIO:
            ev->waveform = WAVE_PULSE;
            ev->duty_cycle = 0.125f;
            ev->duration_ticks = 1;
            ev->vibrato_depth = 0.0f;
            break;
        case CH_HARMONY:
            ev->waveform = WAVE_PULSE;
            ev->duty_cycle = 0.125f;
            ev->vibrato_depth = 0.0f;
            ev->slide_from_note = 0;
            break;
        }
        clamp_vel(ev);
    }

    /* canon effect: echo lead melody on harmony, delayed half a bar,
       one octave lower */
    for (int i = 0; i < orig_count; i++) {
        MusicEvent *ev = &c->events[i];
        if (ev->channel != CH_LEAD) continue;

        MusicEvent canon = *ev;
        canon.channel = CH_HARMONY;
        canon.tick += 8;
        canon.midi_note -= 12;
        if (canon.midi_note < 36) canon.midi_note += 12;
        canon.velocity = (int)((float)canon.velocity * 0.7f);
        canon.duty_cycle = 0.25f;

        if (canon.tick < c->total_ticks) {
            clamp_vel(&canon);
            clamp_note(&canon);
            push_ev(c, canon);
        }
    }

    /* minimal percussion — just a light metronomic pulse */
    rebuild_drums(c, 0x0000, 0x0000, 0x8888, 0, 0, 30);
}

/* ── 4. Acid House / Minimal Techno ─────────────────────────────────── *
 * 4-on-the-floor kick, squelchy sawtooth bass with heavy portamento
 * (303-style glide), sparse lead, offbeat hats.                       */

static void transform_acid_house(Composition *c)
{
    c->global_bpm = clampf(c->global_bpm, 126.0f, 134.0f);
    c->swing = 0.08f;

    for (int i = 0; i < c->num_events; i++) {
        MusicEvent *ev = &c->events[i];
        if (ev->channel == CH_DRUMS) continue;

        switch (ev->channel) {
        case CH_BASS:
            ev->waveform = WAVE_SAWTOOTH;
            ev->duty_cycle = 0.15f;
            ev->velocity = (int)((float)ev->velocity * 1.3f);
            /* heavy portamento for 303 glide */
            if (ev->slide_from_note == 0 && (i % 3 == 0)) {
                ev->slide_from_note = ev->midi_note + (i % 2 ? 5 : -3);
                if (ev->slide_from_note < 30) ev->slide_from_note = 30;
                if (ev->slide_from_note > 60) ev->slide_from_note = 60;
            }
            ev->duration_ticks = (i % 4 == 0) ? 4 : 1;
            break;
        case CH_LEAD:
            ev->velocity = (int)((float)ev->velocity * 0.5f);
            ev->waveform = WAVE_SQUARE;
            ev->duration_ticks = (int)((float)ev->duration_ticks * 0.7f);
            if (ev->duration_ticks < 1) ev->duration_ticks = 1;
            break;
        case CH_PAD:
            ev->velocity = (int)((float)ev->velocity * 0.3f);
            ev->waveform = WAVE_SAWTOOTH;
            break;
        case CH_ARPEGGIO:
            ev->waveform = WAVE_SQUARE;
            ev->velocity = (int)((float)ev->velocity * 0.6f);
            break;
        case CH_HARMONY:
            ev->velocity = (int)((float)ev->velocity * 0.4f);
            break;
        }
        clamp_vel(ev);
        clamp_note(ev);
    }

    /* 4-on-the-floor kick, clap on 2&4, offbeat open hats */
    rebuild_drums(c, 0x8888, 0x0808, 0x5555, 110, 85, 50);
}

/* ── 5. Doom / Sludge Metal ─────────────────────────────────────────── *
 * Slow, heavy, oppressive.  Deep down-tuned sawtooth bass, aggressive
 * distorted lead, droning pads, no arpeggios, sparse heavy drums.     */

static void transform_doom_metal(Composition *c)
{
    c->global_bpm = clampf(c->global_bpm * 0.48f, 55.0f, 75.0f);
    c->swing = 0.0f;

    for (int i = 0; i < c->num_events; i++) {
        MusicEvent *ev = &c->events[i];
        if (ev->channel == CH_DRUMS) continue;

        switch (ev->channel) {
        case CH_BASS:
            ev->waveform = WAVE_SAWTOOTH;
            ev->duration_ticks = (int)((float)ev->duration_ticks * 2.5f);
            if (ev->duration_ticks < 4) ev->duration_ticks = 4;
            ev->velocity = 127;
            ev->duty_cycle = 0.70f;
            if (ev->midi_note > 42) ev->midi_note -= 12;
            if (ev->midi_note > 42) ev->midi_note -= 12;
            ev->vibrato_depth = 0.10f;
            ev->vibrato_rate = 1.5f;
            break;
        case CH_LEAD:
            ev->waveform = WAVE_SQUARE;
            ev->duty_cycle = 0.60f;
            ev->velocity = (int)((float)ev->velocity * 1.2f);
            ev->vibrato_depth = 0.40f;
            ev->vibrato_rate = 5.0f;
            break;
        case CH_PAD:
            ev->waveform = WAVE_SAWTOOTH;
            ev->duration_ticks = (int)((float)ev->duration_ticks * 3.0f);
            ev->velocity = (int)((float)ev->velocity * 1.2f);
            if (ev->midi_note > 55) ev->midi_note -= 12;
            break;
        case CH_ARPEGGIO:
            /* doom doesn't arpeggio — slow, heavy power chords */
            ev->velocity = (int)((float)ev->velocity * 0.4f);
            ev->duration_ticks *= 3;
            ev->waveform = WAVE_SAWTOOTH;
            break;
        case CH_HARMONY:
            ev->waveform = WAVE_SAWTOOTH;
            ev->velocity = (int)((float)ev->velocity * 0.9f);
            ev->duration_ticks *= 2;
            break;
        }
        clamp_vel(ev);
        clamp_note(ev);
    }

    /* slow, heavy, sparse drums */
    rebuild_drums(c, 0x8008, 0x0800, 0x8888, 120, 110, 45);
}

/* ── 6. Eurobeat / Trance ───────────────────────────────────────────── *
 * Extremely fast-paced, sweeping arpeggios, off-beat driving bass,
 * bright soaring lead, lush pads, relentless 16th hats.               */

static void transform_eurobeat(Composition *c)
{
    c->global_bpm = clampf(c->global_bpm * 1.35f, 155.0f, 175.0f);
    c->swing = 0.0f;

    for (int i = 0; i < c->num_events; i++) {
        MusicEvent *ev = &c->events[i];
        if (ev->channel == CH_DRUMS) continue;

        switch (ev->channel) {
        case CH_LEAD:
            ev->waveform = WAVE_SAWTOOTH;
            ev->velocity = (int)((float)ev->velocity * 1.1f);
            ev->vibrato_depth = 0.25f;
            ev->vibrato_rate = 5.5f;
            break;
        case CH_BASS:
            ev->waveform = WAVE_SAWTOOTH;
            ev->duration_ticks = 2;
            /* shift on-beat bass to off-beats for driving eurobeat feel */
            if ((ev->tick % 4) == 0) ev->tick += 2;
            break;
        case CH_PAD:
            ev->waveform = WAVE_SAWTOOTH;
            ev->velocity = (int)((float)ev->velocity * 1.3f);
            ev->vibrato_depth = 0.15f;
            ev->vibrato_rate = 3.0f;
            break;
        case CH_ARPEGGIO:
            ev->waveform = WAVE_PULSE;
            ev->duty_cycle = 0.25f;
            ev->velocity = (int)((float)ev->velocity * 1.2f);
            /* transpose up for soaring sweeping arpeggios */
            if (ev->midi_note < 78) ev->midi_note += 12;
            break;
        case CH_HARMONY:
            ev->waveform = WAVE_SAWTOOTH;
            ev->velocity = (int)((float)ev->velocity * 1.1f);
            break;
        }
        clamp_vel(ev);
        clamp_note(ev);
    }

    /* 4-on-the-floor, snare 2&4, relentless 16th hats */
    rebuild_drums(c, 0x8888, 0x0808, 0xFFFF, 110, 95, 45);
}

/* ── 7. Demoscene Tracker / Keygen Music ────────────────────────────── *
 * Rapid-fire square arpeggios, punchy staccato bass, crisp syncopated
 * drums, no vibrato/portamento — clean chip-like precision.           */

static void transform_demoscene(Composition *c)
{
    c->global_bpm = clampf(c->global_bpm * 1.2f, 140.0f, 165.0f);
    c->swing = 0.0f;

    for (int i = 0; i < c->num_events; i++) {
        MusicEvent *ev = &c->events[i];
        if (ev->channel == CH_DRUMS) continue;

        switch (ev->channel) {
        case CH_LEAD:
            ev->waveform = WAVE_SQUARE;
            ev->duty_cycle = 0.50f;
            if (ev->duration_ticks > 2) ev->duration_ticks--;
            ev->vibrato_depth = 0.0f;
            ev->slide_from_note = 0;
            break;
        case CH_BASS:
            ev->waveform = WAVE_SQUARE;
            ev->duty_cycle = 0.50f;
            ev->duration_ticks = 1;
            ev->velocity = (int)((float)ev->velocity * 1.1f);
            ev->vibrato_depth = 0.0f;
            ev->slide_from_note = 0;
            break;
        case CH_ARPEGGIO:
            ev->waveform = WAVE_SQUARE;
            ev->duty_cycle = 0.25f;
            ev->duration_ticks = 1;
            ev->vibrato_depth = 0.0f;
            break;
        case CH_PAD:
            ev->waveform = WAVE_SQUARE;
            ev->duty_cycle = 0.50f;
            ev->velocity = (int)((float)ev->velocity * 0.6f);
            ev->vibrato_depth = 0.0f;
            break;
        case CH_HARMONY:
            ev->waveform = WAVE_PULSE;
            ev->duty_cycle = 0.125f;
            ev->velocity = (int)((float)ev->velocity * 0.8f);
            ev->vibrato_depth = 0.0f;
            ev->slide_from_note = 0;
            break;
        }
        clamp_vel(ev);
    }

    /* crisp, syncopated drums */
    rebuild_drums(c, 0x8828, 0x2808, 0xEEEE, 105, 95, 55);
}

/* ── 8. 8-Bit Ska / Reggae ─────────────────────────────────────────── *
 * Bouncy, upbeat groove.  Off-beat "skank" chords on harmony/arpeggio,
 * walking melodic bass, one-drop drum pattern (snare on beat 3).      */

static void transform_ska_reggae(Composition *c)
{
    c->global_bpm = clampf(c->global_bpm, 100.0f, 120.0f);
    c->swing = 0.18f;

    for (int i = 0; i < c->num_events; i++) {
        MusicEvent *ev = &c->events[i];
        if (ev->channel == CH_DRUMS) continue;

        switch (ev->channel) {
        case CH_LEAD:
            ev->waveform = WAVE_SQUARE;
            if (ev->duration_ticks > 2) ev->duration_ticks--;
            break;
        case CH_BASS:
            ev->waveform = WAVE_TRIANGLE;
            ev->velocity = (int)((float)ev->velocity * 1.1f);
            break;
        case CH_HARMONY:
        case CH_ARPEGGIO:
            /* off-beat skank: shift on-beats to off-beats */
            ev->waveform = WAVE_SQUARE;
            ev->duty_cycle = 0.50f;
            ev->duration_ticks = 1;
            ev->velocity = (int)((float)ev->velocity * 1.1f);
            if ((ev->tick % 4) == 0) ev->tick += 2;
            break;
        case CH_PAD:
            ev->waveform = WAVE_TRIANGLE;
            ev->velocity = (int)((float)ev->velocity * 0.6f);
            break;
        }
        clamp_vel(ev);
        clamp_note(ev);
    }

    /* one-drop: kick on 2&4, snare on beat 3, off-beat hats */
    rebuild_drums(c, 0x0808, 0x0080, 0x5555, 90, 100, 50);
}

/* ── 9. Trap / Lo-Fi Hip Hop ────────────────────────────────────────── *
 * Slow head-nodding grooves.  Booming 808 sub-bass with glide, sparse
 * mellow lead, heavy swing, rapid hi-hat rolls.                       */

static void transform_trap_lofi(Composition *c)
{
    c->global_bpm = clampf(c->global_bpm * 0.55f, 68.0f, 85.0f);
    c->swing = 0.25f;

    for (int i = 0; i < c->num_events; i++) {
        MusicEvent *ev = &c->events[i];
        if (ev->channel == CH_DRUMS) continue;

        switch (ev->channel) {
        case CH_BASS:
            ev->waveform = WAVE_TRIANGLE;
            ev->duration_ticks = (int)((float)ev->duration_ticks * 3.0f);
            if (ev->duration_ticks < 4) ev->duration_ticks = 4;
            ev->velocity = (int)((float)ev->velocity * 1.2f);
            /* heavy 808 glide */
            if (ev->slide_from_note == 0 && (i % 2 == 0)) {
                ev->slide_from_note = ev->midi_note + 7;
                if (ev->slide_from_note > 55) ev->slide_from_note = 55;
            }
            if (ev->midi_note > 48) ev->midi_note -= 12;
            break;
        case CH_LEAD:
            ev->waveform = WAVE_PULSE;
            ev->duty_cycle = 0.40f;
            ev->velocity = (int)((float)ev->velocity * 0.6f);
            ev->vibrato_depth = 0.20f;
            ev->vibrato_rate = 4.0f;
            break;
        case CH_PAD:
            ev->waveform = WAVE_SAWTOOTH;
            ev->velocity = (int)((float)ev->velocity * 0.7f);
            ev->duration_ticks = (int)((float)ev->duration_ticks * 1.5f);
            break;
        case CH_ARPEGGIO:
            ev->velocity = (int)((float)ev->velocity * 0.5f);
            ev->waveform = WAVE_PULSE;
            ev->duty_cycle = 0.30f;
            break;
        case CH_HARMONY:
            ev->velocity = (int)((float)ev->velocity * 0.5f);
            ev->waveform = WAVE_PULSE;
            break;
        }
        clamp_vel(ev);
        clamp_note(ev);
    }

    /* trap: boomy kick on 1&4, snare on 3, rapid 16th hi-hat rolls */
    rebuild_drums(c, 0x8028, 0x0080, 0xFFFF, 120, 90, 40);
}

/* ── 10. Progressive / Math Rock ────────────────────────────────────── *
 * Complex and unpredictable.  Shifting odd-time feel (displaced
 * accents), clean triangle lead, angular accented bass, irregular
 * drum patterns.                                                      */

static void transform_prog_math_rock(Composition *c)
{
    c->global_bpm = clampf(c->global_bpm * 1.1f, 130.0f, 150.0f);
    c->swing = 0.0f;

    for (int i = 0; i < c->num_events; i++) {
        MusicEvent *ev = &c->events[i];
        if (ev->channel == CH_DRUMS) continue;

        switch (ev->channel) {
        case CH_LEAD:
            ev->waveform = WAVE_TRIANGLE;
            /* disjointed phrasing: push/pull some notes in time */
            if (i % 7 == 0) ev->tick += 1;
            if (i % 5 == 0 && ev->tick > 0) ev->tick -= 1;
            ev->vibrato_depth = 0.0f;
            ev->slide_from_note = 0;
            break;
        case CH_BASS:
            ev->waveform = WAVE_SAWTOOTH;
            ev->velocity = (int)((float)ev->velocity * 1.1f);
            /* angular, accented */
            if (i % 3 == 0) ev->velocity = (int)((float)ev->velocity * 1.3f);
            break;
        case CH_ARPEGGIO:
            ev->waveform = WAVE_TRIANGLE;
            /* mathematical interval jumps */
            if (i % 7 == 0) ev->midi_note += 7;
            if (i % 5 == 0) ev->midi_note += 5;
            break;
        case CH_PAD:
            ev->waveform = WAVE_TRIANGLE;
            ev->velocity = (int)((float)ev->velocity * 0.5f);
            break;
        case CH_HARMONY:
            ev->waveform = WAVE_TRIANGLE;
            if (i % 5 == 0 && ev->tick > 0) ev->tick += 1;
            break;
        }
        clamp_vel(ev);
        clamp_note(ev);
    }

    /* complex, displaced accents — odd groupings */
    rebuild_drums(c, 0x9248, 0x0490, 0xB5AD, 100, 95, 55);
}

/* ══════════════════════════════════════════════════════════════════════
   Public API
   ══════════════════════════════════════════════════════════════════════ */

const char *style_name(StyleType type)
{
    switch (type) {
    case STYLE_SYNTHWAVE:      return "Synthwave / Outrun";
    case STYLE_DUNGEON_SYNTH:  return "Dungeon Synth / Dark Fantasy RPG";
    case STYLE_BAROQUE:        return "Baroque / Bach Counterpoint";
    case STYLE_ACID_HOUSE:     return "Acid House / Minimal Techno";
    case STYLE_DOOM_METAL:     return "Doom / Sludge Metal";
    case STYLE_EUROBEAT:       return "Eurobeat / Trance";
    case STYLE_DEMOSCENE:      return "Demoscene Tracker / Keygen Music";
    case STYLE_SKA_REGGAE:     return "8-Bit Ska / Reggae";
    case STYLE_TRAP_LOFI:      return "Trap / Lo-Fi Hip Hop";
    case STYLE_PROG_MATH_ROCK: return "Progressive / Math Rock";
    default:                   return "None \xe2\x94\x82 Original";
    }
}

const char *style_short_name(StyleType type)
{
    switch (type) {
    case STYLE_SYNTHWAVE:      return "Synthwave";
    case STYLE_DUNGEON_SYNTH:  return "Dungeon Synth";
    case STYLE_BAROQUE:        return "Baroque";
    case STYLE_ACID_HOUSE:     return "Acid House";
    case STYLE_DOOM_METAL:     return "Doom Metal";
    case STYLE_EUROBEAT:       return "Eurobeat";
    case STYLE_DEMOSCENE:      return "Demoscene";
    case STYLE_SKA_REGGAE:     return "Ska/Reggae";
    case STYLE_TRAP_LOFI:      return "Trap/Lo-Fi";
    case STYLE_PROG_MATH_ROCK: return "Prog Rock";
    default:                   return "None";
    }
}

StyleType style_next(StyleType current)
{
    int next = (int)current + 1;
    if (next >= NUM_STYLE_TYPES) next = 0;
    return (StyleType)next;
}

StyleType style_prev(StyleType current)
{
    int prev = (int)current - 1;
    if (prev < 0) prev = NUM_STYLE_TYPES - 1;
    return (StyleType)prev;
}

int style_parse(const char *name)
{
    if (!name) return -1;

    if (strcasecmp(name, "synthwave") == 0 ||
        strcasecmp(name, "outrun") == 0)
        return STYLE_SYNTHWAVE;

    if (strcasecmp(name, "dungeon") == 0 ||
        strcasecmp(name, "dungeon-synth") == 0 ||
        strcasecmp(name, "darkfantasy") == 0)
        return STYLE_DUNGEON_SYNTH;

    if (strcasecmp(name, "baroque") == 0 ||
        strcasecmp(name, "bach") == 0)
        return STYLE_BAROQUE;

    if (strcasecmp(name, "acid") == 0 ||
        strcasecmp(name, "acidhouse") == 0 ||
        strcasecmp(name, "techno") == 0)
        return STYLE_ACID_HOUSE;

    if (strcasecmp(name, "doom") == 0 ||
        strcasecmp(name, "sludge") == 0 ||
        strcasecmp(name, "metal") == 0)
        return STYLE_DOOM_METAL;

    if (strcasecmp(name, "eurobeat") == 0 ||
        strcasecmp(name, "trance") == 0)
        return STYLE_EUROBEAT;

    if (strcasecmp(name, "demoscene") == 0 ||
        strcasecmp(name, "keygen") == 0 ||
        strcasecmp(name, "tracker") == 0)
        return STYLE_DEMOSCENE;

    if (strcasecmp(name, "ska") == 0 ||
        strcasecmp(name, "reggae") == 0)
        return STYLE_SKA_REGGAE;

    if (strcasecmp(name, "trap") == 0 ||
        strcasecmp(name, "lofi") == 0 ||
        strcasecmp(name, "lo-fi") == 0 ||
        strcasecmp(name, "hiphop") == 0)
        return STYLE_TRAP_LOFI;

    if (strcasecmp(name, "prog") == 0 ||
        strcasecmp(name, "progrock") == 0 ||
        strcasecmp(name, "mathrock") == 0 ||
        strcasecmp(name, "math") == 0)
        return STYLE_PROG_MATH_ROCK;

    if (strcasecmp(name, "none") == 0 ||
        strcasecmp(name, "off") == 0 ||
        strcasecmp(name, "default") == 0)
        return STYLE_NONE;

    return -1;
}

void style_transform(Composition *comp, StyleType style)
{
    if (style == STYLE_NONE) return;

    switch (style) {
    case STYLE_SYNTHWAVE:      transform_synthwave(comp);      break;
    case STYLE_DUNGEON_SYNTH:  transform_dungeon_synth(comp);  break;
    case STYLE_BAROQUE:        transform_baroque(comp);        break;
    case STYLE_ACID_HOUSE:     transform_acid_house(comp);     break;
    case STYLE_DOOM_METAL:     transform_doom_metal(comp);     break;
    case STYLE_EUROBEAT:       transform_eurobeat(comp);       break;
    case STYLE_DEMOSCENE:      transform_demoscene(comp);      break;
    case STYLE_SKA_REGGAE:     transform_ska_reggae(comp);     break;
    case STYLE_TRAP_LOFI:      transform_trap_lofi(comp);      break;
    case STYLE_PROG_MATH_ROCK: transform_prog_math_rock(comp); break;
    default: break;
    }

    /* clamp any negative ticks produced by time-shifting transforms */
    for (int i = 0; i < comp->num_events; i++) {
        if (comp->events[i].tick < 0)
            comp->events[i].tick = 0;
    }

    /* re-sort events by tick after transformations */
    if (comp->num_events > 1)
        qsort(comp->events, (size_t)comp->num_events,
              sizeof(MusicEvent), ev_cmp);
}
