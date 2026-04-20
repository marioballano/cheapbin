#include "synth.h"
#include "chipemu.h"
#include "style.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── MIDI note to frequency ─────────────────────────────────────────── */

static double midi_to_freq(int note)
{
    return 440.0 * pow(2.0, (note - 69) / 12.0);
}

/* ── Envelope ───────────────────────────────────────────────────────── */

static void env_config(Envelope *e, float atk_ms, float dec_ms,
                       float sus, float rel_ms)
{
    float sr = (float)SAMPLE_RATE;
    e->attack_rate   = (atk_ms > 0) ? 1.0f / (atk_ms * 0.001f * sr) : 1.0f;
    e->decay_rate    = (dec_ms > 0) ? (1.0f - sus) / (dec_ms * 0.001f * sr) : 1.0f;
    e->sustain_level = sus;
    e->release_rate  = (rel_ms > 0) ? sus / (rel_ms * 0.001f * sr) : 1.0f;
    e->level = 0.0f;
    e->stage = ENV_IDLE;
}

static void env_trigger(Envelope *e)
{
    e->stage = ENV_ATTACK;
    e->level = 0.0f;
}

static void env_release_env(Envelope *e)
{
    if (e->stage != ENV_IDLE)
        e->stage = ENV_RELEASE;
}

static float env_process(Envelope *e)
{
    switch (e->stage) {
    case ENV_ATTACK:
        e->level += e->attack_rate;
        if (e->level >= 1.0f) { e->level = 1.0f; e->stage = ENV_DECAY; }
        break;
    case ENV_DECAY:
        e->level -= e->decay_rate;
        if (e->level <= e->sustain_level) {
            e->level = e->sustain_level;
            e->stage = ENV_SUSTAIN;
        }
        break;
    case ENV_SUSTAIN:
        break;
    case ENV_RELEASE:
        e->level -= e->release_rate;
        if (e->level <= 0.0f) { e->level = 0.0f; e->stage = ENV_IDLE; }
        break;
    case ENV_IDLE:
        e->level = 0.0f;
        break;
    }
    return e->level;
}

/* ── Waveform generators ────────────────────────────────────────────── */

static float gen_square(double phase, float duty)
{
    return (phase < (double)duty) ? 1.0f : -1.0f;
}

static float gen_triangle(double phase)
{
    if (phase < 0.25) return (float)(phase * 4.0);
    if (phase < 0.75) return (float)(2.0 - phase * 4.0);
    return (float)(phase * 4.0 - 4.0);
}

static float gen_sawtooth(double phase)
{
    return (float)(2.0 * phase - 1.0);
}

static float gen_noise(uint32_t *reg)
{
    uint32_t bit = ((*reg >> 0) ^ (*reg >> 1)) & 1;
    *reg = (*reg >> 1) | (bit << 14);
    return (*reg & 1) ? 1.0f : -1.0f;
}

static float gen_sample(Channel *ch)
{
    /* vibrato modulation */
    double vib_mod = 0.0;
    if (ch->vib_depth > 0.0f) {
        vib_mod = (double)ch->vib_depth *
                  sin(ch->vib_phase * 2.0 * 3.14159265);
        ch->vib_phase += (double)ch->vib_rate / (double)SAMPLE_RATE;
        if (ch->vib_phase >= 1.0) ch->vib_phase -= 1.0;
    }

    /* portamento slide */
    if (ch->porta_samples > 0) {
        ch->phase_inc += (double)ch->porta_speed;
        ch->porta_samples--;
    }

    /* effective phase increment with vibrato */
    double eff_inc = ch->phase_inc * pow(2.0, vib_mod / 12.0);

    /* pulse width modulation */
    if (ch->duty_sweep > 0.0f) {
        ch->duty = ch->duty_base +
                   0.15f * sinf((float)ch->vib_phase * 2.3f);
        if (ch->duty < 0.125f) ch->duty = 0.125f;
        if (ch->duty > 0.75f) ch->duty = 0.75f;
    }

    float s;
    switch (ch->waveform) {
    case WAVE_SQUARE:   s = gen_square(ch->phase, ch->duty);   break;
    case WAVE_TRIANGLE: s = gen_triangle(ch->phase);           break;
    case WAVE_SAWTOOTH: s = gen_sawtooth(ch->phase);           break;
    case WAVE_NOISE:    s = gen_noise(&ch->noise_reg);         break;
    case WAVE_PULSE:    s = gen_square(ch->phase, ch->duty);   break;
    default:            s = 0.0f;                              break;
    }

    ch->phase += eff_inc;
    if (ch->phase >= 1.0) ch->phase -= 1.0;

    return s;
}

/* ── Multi-voice drum synthesis ─────────────────────────────────────── */

static void drum_trigger(DrumVoice *dv, int type)
{
    dv->phase = 0.0;
    dv->noise_reg = 0x7FFF;

    switch (type) {
    case DRUM_KICK:
        dv->freq = 180.0;
        dv->freq_decay = 0.997;
        dv->env = 1.0f;
        dv->env_decay = 0.999f;
        dv->noise_mix = 0.05f;
        break;
    case DRUM_SNARE:
        dv->freq = 220.0;
        dv->freq_decay = 0.999;
        dv->env = 1.0f;
        dv->env_decay = 0.9975f;
        dv->noise_mix = 0.7f;
        break;
    case DRUM_HIHAT:
        dv->freq = 800.0;
        dv->freq_decay = 1.0;
        dv->env = 0.7f;
        dv->env_decay = 0.995f;
        dv->noise_mix = 0.95f;
        break;
    case DRUM_CRASH:
        dv->freq = 400.0;
        dv->freq_decay = 0.9999;
        dv->env = 1.0f;
        dv->env_decay = 0.9998f;
        dv->noise_mix = 0.85f;
        break;
    default:
        dv->env = 0.0f;
        break;
    }
}

static float drum_process(DrumVoice *dv)
{
    if (dv->env < 0.001f) return 0.0f;

    /* tone component */
    float tone = sinf((float)(dv->phase * 2.0 * 3.14159265));
    dv->phase += dv->freq / (double)SAMPLE_RATE;
    if (dv->phase >= 1.0) dv->phase -= 1.0;
    dv->freq *= dv->freq_decay;

    /* noise component */
    float noise = gen_noise(&dv->noise_reg);

    float out = tone * (1.0f - dv->noise_mix) + noise * dv->noise_mix;
    out *= dv->env;
    dv->env *= dv->env_decay;

    return out;
}

/* ── Channel note trigger ───────────────────────────────────────────── */

static void channel_note_on(Channel *ch, const MusicEvent *ev,
                            DrumVoice dv[4])
{
    if (ev->channel == CH_DRUMS) {
        int vi;
        switch (ev->midi_note) {
        case DRUM_KICK:  vi = 0; break;
        case DRUM_SNARE: vi = 1; break;
        case DRUM_HIHAT: vi = 2; break;
        case DRUM_CRASH: vi = 3; break;
        default: return;
        }
        drum_trigger(&dv[vi], ev->midi_note);
        ch->active = true;
        ch->midi_note = ev->midi_note;
        ch->samples_left = (int)(0.2f * SAMPLE_RATE);
        return;
    }

    double new_freq = midi_to_freq(ev->midi_note);
    double new_inc = new_freq / (double)SAMPLE_RATE;

    /* portamento */
    if (ev->slide_from_note > 0 &&
        ev->slide_from_note != ev->midi_note) {
        double old_freq = midi_to_freq(ev->slide_from_note);
        ch->phase_inc = old_freq / (double)SAMPLE_RATE;
        ch->target_phase_inc = new_inc;
        int porta_ms = 60;
        ch->porta_samples =
            (int)((float)porta_ms * 0.001f * (float)SAMPLE_RATE);
        ch->porta_speed =
            (float)((new_inc - ch->phase_inc) / (double)ch->porta_samples);
    } else {
        ch->phase_inc = new_inc;
        ch->target_phase_inc = new_inc;
        ch->porta_samples = 0;
    }

    ch->midi_note  = ev->midi_note;
    ch->waveform   = ev->waveform;
    ch->duty       = ev->duty_cycle;
    ch->duty_base  = ev->duty_cycle;
    ch->active     = true;

    ch->vib_depth  = ev->vibrato_depth;
    ch->vib_rate   = ev->vibrato_rate;

    ch->duty_sweep = (ev->waveform == WAVE_PULSE ||
                      ev->waveform == WAVE_SQUARE) ? 1.0f : 0.0f;

    /* ADSR per channel type */
    switch (ev->channel) {
    case CH_LEAD:
        env_config(&ch->env, 5.0f, 80.0f, 0.55f, 40.0f);
        break;
    case CH_HARMONY:
        env_config(&ch->env, 10.0f, 100.0f, 0.4f, 60.0f);
        break;
    case CH_BASS:
        env_config(&ch->env, 3.0f, 40.0f, 0.7f, 20.0f);
        break;
    case CH_ARPEGGIO:
        env_config(&ch->env, 2.0f, 25.0f, 0.3f, 12.0f);
        break;
    case CH_PAD:
        env_config(&ch->env, 200.0f, 300.0f, 0.6f, 400.0f);
        break;
    default:
        env_config(&ch->env, 5.0f, 50.0f, 0.5f, 30.0f);
        break;
    }

    /* note duration in samples */
    float tick_sec = 60.0f / 120.0f / 4.0f;  /* approximate */
    ch->samples_left =
        (int)((float)ev->duration_ticks * tick_sec * (float)SAMPLE_RATE);
    env_trigger(&ch->env);
}

/* ══════════════════════════════════════════════════════════════════════
   Chip emulation DSP — waveform shaping and post-processing that
   colours the existing synthesis to sound like classic hardware.
   ══════════════════════════════════════════════════════════════════════ */

/* Adjust waveform type and duty cycle per-chip BEFORE gen_sample(). */
static void chip_adjust_voice(ChipType chip, int ch_idx,
                              int *waveform, float *duty, float vib_phase)
{
    switch (chip) {
    case CHIP_NES:
        /* 2A03: triangle channel for bass/pad (hardware triangle is
           4-bit stepped — we handle that in chip_color_sample).
           Everything else gets pulse, snapped to the 4 hardware duties.
           Noise channel stays as noise.  Arpeggio gets 12.5% for the
           classic NES "chime" sound. */
        if (*waveform == WAVE_NOISE) break;
        if (ch_idx == CH_BASS || ch_idx == CH_PAD) {
            *waveform = WAVE_TRIANGLE;
        } else {
            *waveform = WAVE_PULSE;
            if (ch_idx == CH_ARPEGGIO) {
                *duty = 0.125f;  /* thin arp chirp */
            } else if (ch_idx == CH_HARMONY) {
                *duty = 0.25f;   /* hollow harmony */
            } else {
                /* lead: snap to nearest hardware duty */
                float d = *duty;
                if      (d < 0.1875f) *duty = 0.125f;
                else if (d < 0.375f)  *duty = 0.25f;
                else if (d < 0.625f)  *duty = 0.5f;
                else                  *duty = 0.75f;
            }
        }
        break;

    case CHIP_SPECTRUM:
        /* AY-3-8910: only 50 % square waves. */
        *waveform = WAVE_SQUARE;
        *duty     = 0.5f;
        break;

    case CHIP_SID:
        /* MOS 6581: wide PWM sweep is the iconic SID sound.
           Different channels get different sweep rates for richness. */
        if (*waveform == WAVE_PULSE || *waveform == WAVE_SQUARE) {
            float sweep_rate = 1.7f + (float)ch_idx * 0.3f;
            float sweep_depth = 0.30f;
            *duty = 0.5f + sweep_depth * sinf(vib_phase * sweep_rate);
            if (*duty < 0.08f) *duty = 0.08f;
            if (*duty > 0.92f) *duty = 0.92f;
        }
        /* SID's sawtooth is also very characteristic */
        if (ch_idx == CH_BASS)
            *waveform = WAVE_SAWTOOTH;
        break;

    case CHIP_GENESIS:
        /* YM2612 is all about FM — replace smooth waveforms with
           harsher, more harmonically rich alternatives.
           Bass gets sawtooth, lead and harmony keep their shape but
           will be FM'd in chip_color_sample. */
        if (ch_idx == CH_BASS || ch_idx == CH_PAD) {
            *waveform = WAVE_SAWTOOTH;
        }
        if (*waveform == WAVE_TRIANGLE && ch_idx == CH_LEAD) {
            *waveform = WAVE_SAWTOOTH;
        }
        break;

    default:
        break;
    }
}

/* Per-channel colouring applied to a raw sample AFTER gen_sample(). */
static float chip_color_sample(ChipState *cs, float sample, int ch_idx)
{
    switch (cs->type) {
    case CHIP_NES: {
        /* 2A03 triangle is notoriously 4-bit stepped (16 levels).
           Pulse channels get quantized to 4-bit volume. */
        if (ch_idx == CH_BASS || ch_idx == CH_PAD) {
            /* 4-bit stepped triangle: 16 levels, very crunchy */
            sample = floorf(sample * 8.0f + 0.5f) / 8.0f;
        } else {
            /* Pulse channels: 4-bit volume resolution */
            sample = floorf(sample * 8.0f + 0.5f) / 8.0f;
        }
        /* NES has no smooth volume — simulate step-down per channel */
        float vol_step = floorf(fabsf(sample) * 15.0f + 0.5f) / 15.0f;
        sample = (sample >= 0.0f ? vol_step : -vol_step);
        break;
    }

    case CHIP_SPECTRUM:
        /* AY-3-8910: soft-clip to prevent square waves stacking
           into harsh overs — tanh avoids the click of hard clip. */
        sample = tanhf(sample * 1.05f);
        break;

    case CHIP_GENESIS: {
        /* YM2612 FM-style waveshaping: polynomial distortion that
           adds odd harmonics (like FM) without aliasing.  Different
           drive amounts per channel for timbral variety. */
        float drive = 1.4f;
        if (ch_idx == CH_BASS)     drive = 1.8f;  /* growly bass */
        if (ch_idx == CH_PAD)      drive = 1.1f;  /* softer pad */
        if (ch_idx == CH_ARPEGGIO) drive = 1.6f;  /* bright arp */
        if (ch_idx == CH_LEAD)     drive = 1.5f;

        /* Soft-clip waveshaper: tanh adds odd harmonics smoothly */
        float shaped = tanhf(sample * drive);

        /* Blend: keep fundamental, mix in harmonics */
        sample = sample * 0.4f + shaped * 0.6f;
        break;
    }

    case CHIP_SID: {
        /* Gentle SID warmth: very mild ring-mod coloring, not
           amplitude-chopping — just adds subtle harmonic content. */
        if (ch_idx == CH_LEAD || ch_idx == CH_ARPEGGIO) {
            float ring = sinf(cs->sid_ring_phase);
            sample += sample * ring * 0.06f;  /* additive, not multiplicative */
        }
        break;
    }

    default:
        break;
    }
    return sample;
}

/* Global post-processing on the final mono mix. */
static float chip_post_process(ChipState *cs, float sample)
{
    switch (cs->type) {
    case CHIP_SID: {
        /* SID 6581 resonant filter — gentle sweep for that warm,
           characteristic C64 sound without choking the signal. */
        float lfo_speed = 0.00006f;  /* slow, musical sweep */
        cs->sid_lfo_phase += lfo_speed;
        if (cs->sid_lfo_phase > 6.2832f) cs->sid_lfo_phase -= 6.2832f;

        /* Sweep cutoff — stays in a warm, open range */
        float cutoff_norm = 0.45f + 0.15f * sinf(cs->sid_lfo_phase);

        /* One-pole state-variable filter with mild resonance */
        float f = cutoff_norm;
        if (f > 0.9f) f = 0.9f;
        float q = 1.0f - cs->sid_resonance * 0.55f;  /* moderate Q */

        cs->sid_lp += f * cs->sid_bp;
        float hp = sample - cs->sid_lp - q * cs->sid_bp;
        cs->sid_bp += f * hp;

        /* Filter mix: mostly lowpass for warmth */
        float filtered = cs->sid_lp * 0.7f + cs->sid_bp * 0.3f;

        /* Gentle analog warmth (mild soft-clip, not hard saturation) */
        filtered = tanhf(filtered * 1.1f);

        /* Ring modulator oscillator advance (slow) */
        cs->sid_ring_phase += 0.0007f;
        if (cs->sid_ring_phase > 6.2832f) cs->sid_ring_phase -= 6.2832f;

        sample = filtered;
        break;
    }

    case CHIP_NES: {
        /* 2A03 has a two-stage high-pass filter chain:
           Stage 1: ~37 Hz (removes DC from triangle channel)
           Stage 2: ~667 Hz cut applied to the delta-sigma output
           Plus a ~14 kHz low-pass from the pin capacitance. */

        /* Stage 1 high-pass (~37 Hz) */
        float hp1_alpha = 0.9985f;
        float hp1_out = sample - cs->nes_hp1_cap;
        cs->nes_hp1_cap += (1.0f - hp1_alpha) * hp1_out;

        /* Stage 2 high-pass (~667 Hz, very subtle) */
        float hp2_alpha = 0.997f;
        float hp2_out = hp1_out - cs->nes_hp2_cap;
        cs->nes_hp2_cap += (1.0f - hp2_alpha) * hp2_out;

        /* Low-pass (~14 kHz — smooths the step edges) */
        float lp_alpha = 0.7f;
        cs->nes_lp_state = lp_alpha * cs->nes_lp_state +
                           (1.0f - lp_alpha) * hp2_out;

        sample = cs->nes_lp_state;

        /* NES non-linear mixer: pulse and triangle/noise are mixed
           through separate DAC paths with non-linear response.
           Approximate with a soft-saturating curve. */
        float x = sample * 1.8f;
        if (x > 0.0f)
            sample = x / (1.0f + x);
        else
            sample = x / (1.0f - x);

        /* 7-bit effective output resolution */
        sample = floorf(sample * 64.0f + 0.5f) / 64.0f;
        break;
    }

    case CHIP_GENESIS: {
        /* YM2612 post-processing: gentle hardware LFO tremolo
           and mild DAC character without harsh quantization. */

        /* Hardware LFO (~4 Hz tremolo, subtle) */
        cs->fm_lfo_phase += 0.0003f;
        if (cs->fm_lfo_phase > 6.2832f) cs->fm_lfo_phase -= 6.2832f;
        float tremolo = 1.0f - 0.04f * (sinf(cs->fm_lfo_phase) + 1.0f) * 0.5f;
        sample *= tremolo;

        /* Gentle overdrive — just enough to add FM grit */
        sample = tanhf(sample * 1.15f);

        /* 9-bit DAC: light quantization without error diffusion */
        sample = floorf(sample * 256.0f + 0.5f) / 256.0f;
        break;
    }

    case CHIP_SPECTRUM: {
        /* AY-3-8910: gentle DC-blocking high-pass for the thin
           beeper feel, fully applied (no raw bypass). */
        float alpha = 0.995f;
        float out = alpha * (cs->ay_hp_prev_out + sample - cs->ay_hp_prev_in);
        cs->ay_hp_prev_in  = sample;
        cs->ay_hp_prev_out = out;
        sample = out;

        /* 5-bit DAC quantisation (32 levels) — crunchy but not
           coarse enough to create audible background noise. */
        sample = floorf(sample * 16.0f + 0.5f) / 16.0f;
        break;
    }

    default:
        break;
    }
    return sample;
}

/* ── Init ───────────────────────────────────────────────────────────── */

void synth_init(SynthState *s, Composition *comp)
{
    memset(s, 0, sizeof(*s));

    s->events       = comp->events;
    s->num_events   = comp->num_events;
    s->event_idx    = 0;
    s->current_tick = 0;
    s->bpm          = comp->global_bpm;
    s->swing        = comp->swing;
    s->finished     = false;
    s->paused       = false;
    s->scale_type   = (ScaleType)comp->scale_index;

    s->sections        = comp->sections;
    s->num_sections    = comp->num_sections;
    s->current_section = 0;
    if (comp->num_sections > 0) {
        memcpy(s->section_name, comp->sections[0].name,
               sizeof(s->section_name));
        s->section_name[sizeof(s->section_name) - 1] = '\0';
    }

    /* tick timing */
    float tick_sec = 60.0f / s->bpm / 4.0f;
    s->samples_per_tick = (int)(tick_sec * (float)SAMPLE_RATE);
    s->tick_counter = s->samples_per_tick;

    /* channel volumes */
    s->channels[CH_LEAD].volume     = 0.28f;
    s->channels[CH_HARMONY].volume  = 0.15f;
    s->channels[CH_BASS].volume     = 0.25f;
    s->channels[CH_ARPEGGIO].volume = 0.14f;
    s->channels[CH_PAD].volume      = 0.10f;
    s->channels[CH_DRUMS].volume    = 0.22f;

    for (int i = 0; i < NUM_CHANNELS; i++) {
        s->channels[i].noise_reg = 0x7FFF;
        s->channels[i].phase     = 0.0;
        s->channels[i].active    = false;
    }

    for (int i = 0; i < 4; i++) {
        s->drum_voices[i].noise_reg = 0x7FFF;
        s->drum_voices[i].env = 0.0f;
    }

    /* echo/delay: ~375ms */
    int delay_samples = (int)(0.375f * SAMPLE_RATE);
    s->delay_write    = 0;
    s->delay_read     = DELAY_SIZE - delay_samples;
    s->delay_feedback = 0.3f;
    s->delay_mix      = 0.2f;

    /* LPF */
    s->lpf_state = 0.0f;
    s->lpf_alpha = 0.85f;

    /* master fade */
    s->master_fade = 0.0f;
    s->fade_target = 1.0f;
    s->fade_speed  = 1.0f / (0.5f * SAMPLE_RATE);  /* 0.5s fade in */

    /* default chip (caller should follow with synth_set_chip) */
    s->chip_type = CHIP_DEFAULT;
    chip_init(&s->chip_state, CHIP_DEFAULT);
}

/* ── Chip selection (can be called at any time, even mid-playback) ── */

void synth_set_chip(SynthState *s, ChipType chip)
{
    s->chip_type = chip;
    chip_init(&s->chip_state, chip);

    /* Tune delay / filter / volume to match each chip's character. */
    switch (chip) {
    case CHIP_SID:
        /* C64: lush reverb, warm filter, bass-heavy mix */
        s->delay_feedback = 0.40f;
        s->delay_mix      = 0.28f;
        s->lpf_alpha      = 0.75f;  /* very warm, muffled highs */
        s->channels[CH_LEAD].volume     = 0.26f;
        s->channels[CH_HARMONY].volume  = 0.18f;
        s->channels[CH_BASS].volume     = 0.30f;  /* SID bass is fat */
        s->channels[CH_ARPEGGIO].volume = 0.16f;
        s->channels[CH_PAD].volume      = 0.12f;
        s->channels[CH_DRUMS].volume    = 0.18f;  /* drums recessed */
        break;
    case CHIP_NES:
        /* NES: dry, bright, punchy, limited dynamic range */
        s->delay_feedback = 0.08f;
        s->delay_mix      = 0.05f;  /* almost no reverb */
        s->lpf_alpha      = 0.88f;  /* bright but not harsh */
        s->channels[CH_LEAD].volume     = 0.30f;
        s->channels[CH_HARMONY].volume  = 0.12f;  /* only 2 pulse ch */
        s->channels[CH_BASS].volume     = 0.28f;  /* triangle is loud */
        s->channels[CH_ARPEGGIO].volume = 0.16f;
        s->channels[CH_PAD].volume      = 0.06f;  /* NES has no pad */
        s->channels[CH_DRUMS].volume    = 0.24f;  /* noise ch punch */
        break;
    case CHIP_GENESIS:
        /* Genesis: aggressive, wide, FM sizzle */
        s->delay_feedback = 0.25f;
        s->delay_mix      = 0.18f;
        s->lpf_alpha      = 0.88f;  /* crisp FM overtones */
        s->channels[CH_LEAD].volume     = 0.30f;
        s->channels[CH_HARMONY].volume  = 0.20f;
        s->channels[CH_BASS].volume     = 0.28f;  /* Genesis bass growls */
        s->channels[CH_ARPEGGIO].volume = 0.15f;
        s->channels[CH_PAD].volume      = 0.12f;
        s->channels[CH_DRUMS].volume    = 0.26f;  /* punchy drums */
        break;
    case CHIP_SPECTRUM:
        s->delay_feedback = 0.12f;
        s->delay_mix      = 0.10f;
        s->lpf_alpha      = 0.82f;
        s->channels[CH_LEAD].volume     = 0.28f;
        s->channels[CH_HARMONY].volume  = 0.15f;
        s->channels[CH_BASS].volume     = 0.25f;
        s->channels[CH_ARPEGGIO].volume = 0.14f;
        s->channels[CH_PAD].volume      = 0.10f;
        s->channels[CH_DRUMS].volume    = 0.22f;
        break;
    default:
        s->delay_feedback = 0.30f;
        s->delay_mix      = 0.20f;
        s->lpf_alpha      = 0.85f;
        s->channels[CH_LEAD].volume     = 0.28f;
        s->channels[CH_HARMONY].volume  = 0.15f;
        s->channels[CH_BASS].volume     = 0.25f;
        s->channels[CH_ARPEGGIO].volume = 0.14f;
        s->channels[CH_PAD].volume      = 0.10f;
        s->channels[CH_DRUMS].volume    = 0.22f;
        break;
    }
}

/* ── Style application (can be called at any time, even mid-playback) ── */

void synth_apply_style(SynthState *s, StyleType style,
                       const Composition *original)
{
    MusicEvent *old_styled = s->styled_events;

    s->style_type = style;

    if (style == STYLE_NONE) {
        /* point directly to original events */
        s->events     = original->events;
        s->num_events = original->num_events;
        s->bpm        = original->global_bpm;
        s->swing      = original->swing;
        s->styled_events = NULL;
    } else {
        /* make a working copy and transform it */
        int cap = original->num_events + 8192;
        MusicEvent *copy = malloc((size_t)cap * sizeof(MusicEvent));
        memcpy(copy, original->events,
               (size_t)original->num_events * sizeof(MusicEvent));

        Composition temp;
        temp.events       = copy;
        temp.num_events   = original->num_events;
        temp.capacity     = cap;
        temp.sections     = original->sections;
        temp.num_sections = original->num_sections;
        temp.global_bpm   = original->global_bpm;
        temp.swing        = original->swing;
        temp.total_ticks  = original->total_ticks;

        style_transform(&temp, style);

        /* swap — new events are live before freeing old */
        s->styled_events = temp.events;
        s->events        = s->styled_events;
        s->num_events    = temp.num_events;
        s->bpm           = temp.global_bpm;
        s->swing         = temp.swing;
    }

    /* update tick timing */
    float tick_sec = 60.0f / s->bpm / 4.0f;
    s->samples_per_tick = (int)(tick_sec * (float)SAMPLE_RATE);

    /* seek event_idx to current tick */
    s->event_idx = 0;
    while (s->event_idx < s->num_events &&
           s->events[s->event_idx].tick < s->current_tick)
        s->event_idx++;

    /* release active notes to avoid stuck sounds */
    for (int c = 0; c < NUM_CHANNELS; c++) {
        if (s->channels[c].env.stage != ENV_IDLE)
            s->channels[c].env.stage = ENV_RELEASE;
    }

    /* now safe to free old styled events */
    free(old_styled);
}

/* ── Scale change (rebuilds composition, keeps current tick) ───────── */

void synth_set_scale(SynthState *s, const uint8_t *data, size_t size,
                     int scale_override, Composition *comp)
{
    Composition new_comp;
    if (compose_with_scale(data, size, scale_override, &new_comp) != 0)
        return;
    if (new_comp.num_events == 0) {
        composition_free(&new_comp);
        return;
    }

    MusicEvent  *old_events   = comp->events;
    SongSection *old_sections = comp->sections;

    /* Publish the new composition before anything references the old
     * arrays. The audio callback re-reads s->events / s->sections each
     * sample, so we update those fields below via synth_apply_style(). */
    comp->events       = new_comp.events;
    comp->num_events   = new_comp.num_events;
    comp->capacity     = new_comp.capacity;
    comp->sections     = new_comp.sections;
    comp->num_sections = new_comp.num_sections;
    comp->global_bpm   = new_comp.global_bpm;
    comp->swing        = new_comp.swing;
    comp->total_ticks  = new_comp.total_ticks;
    comp->scale_index  = new_comp.scale_index;

    s->sections     = comp->sections;
    s->num_sections = comp->num_sections;
    s->scale_type   = (ScaleType)comp->scale_index;

    /* Re-apply the current style (or STYLE_NONE) against the new raw
     * events. This does the real event-pointer swap, updates bpm/timing,
     * re-seeks event_idx to current_tick, and releases active envelopes. */
    synth_apply_style(s, s->style_type, comp);

    /* Safe to free the old arrays now — no live pointer reaches them. */
    free(old_events);
    free(old_sections);
}

/* ── Seek ───────────────────────────────────────────────────────────── */

void synth_seek(SynthState *s, int tick_delta)
{
    int total = (s->num_sections > 0)
              ? s->sections[s->num_sections - 1].end_tick
              : s->current_tick;
    if (total < 1) total = 1;

    int new_tick = s->current_tick + tick_delta;
    if (new_tick < 0) new_tick = 0;
    if (new_tick > total - 1) new_tick = total - 1;
    s->current_tick = new_tick;

    /* Reseek event_idx to the first event at or after new_tick; the next
     * advance_tick() call will fire everything scheduled for this tick. */
    s->event_idx = 0;
    while (s->event_idx < s->num_events &&
           s->events[s->event_idx].tick < s->current_tick)
        s->event_idx++;

    /* Release active envelopes so seeking doesn't leave notes stuck. */
    for (int c = 0; c < NUM_CHANNELS; c++) {
        if (s->channels[c].env.stage != ENV_IDLE)
            s->channels[c].env.stage = ENV_RELEASE;
        s->channels[c].samples_left = 0;
    }
    for (int d = 0; d < 4; d++)
        s->drum_voices[d].env = 0.0f;

    /* Recompute section tracking. */
    for (int i = 0; i < s->num_sections; i++) {
        if (s->current_tick >= s->sections[i].start_tick &&
            s->current_tick < s->sections[i].end_tick) {
            s->current_section = i;
            memcpy(s->section_name, s->sections[i].name,
                   sizeof(s->section_name));
            s->section_name[sizeof(s->section_name) - 1] = '\0';
            break;
        }
    }

    /* If we seeked out of the outro, cancel the fade-out. */
    if (s->current_section < s->num_sections &&
        s->sections[s->current_section].type != SEC_OUTRO) {
        s->fade_target = 1.0f;
    }

    s->tick_counter = s->samples_per_tick;
    if (s->current_tick < total - 1) s->finished = false;
}

/* ── Tick advance ───────────────────────────────────────────────────── */

static void advance_tick(SynthState *s)
{
    /* update section tracking */
    for (int i = 0; i < s->num_sections; i++) {
        if (s->current_tick >= s->sections[i].start_tick &&
            s->current_tick < s->sections[i].end_tick) {
            if (s->current_section != i) {
                s->current_section = i;
                memcpy(s->section_name, s->sections[i].name,
                       sizeof(s->section_name));
                s->section_name[sizeof(s->section_name) - 1] = '\0';

                /* fade out for outro */
                if (s->sections[i].type == SEC_OUTRO) {
                    s->fade_target = 0.0f;
                    int outro_ticks = s->sections[i].end_tick -
                                      s->sections[i].start_tick;
                    float outro_sec = (float)outro_ticks *
                                      60.0f / s->bpm / 4.0f;
                    s->fade_speed = 1.0f /
                                    (outro_sec * (float)SAMPLE_RATE);
                }
            }
            break;
        }
    }

    /* fire all events at current tick */
    while (s->event_idx < s->num_events &&
           s->events[s->event_idx].tick <= s->current_tick)
    {
        MusicEvent *ev = &s->events[s->event_idx];
        if (ev->tick == s->current_tick) {
            int ch_idx = ev->channel;
            if (ch_idx >= 0 && ch_idx < NUM_CHANNELS) {
                if (ev->midi_note > 0) {
                    channel_note_on(&s->channels[ch_idx], ev,
                                    s->drum_voices);
                } else {
                    env_release_env(&s->channels[ch_idx].env);
                }
            }
        }
        s->event_idx++;
    }

    s->current_tick++;

    int total = s->sections[s->num_sections - 1].end_tick;
    if (s->current_tick >= total)
        s->finished = true;
}

/* ── Render ─────────────────────────────────────────────────────────── */

void synth_render(SynthState *s, int16_t *buffer, int num_samples)
{
    if (s->paused || s->finished) {
        memset(buffer, 0, (size_t)num_samples * sizeof(int16_t));
        return;
    }

    float level_accum[NUM_CHANNELS];
    memset(level_accum, 0, sizeof(level_accum));

    for (int i = 0; i < num_samples; i++) {
        /* tick advance with swing */
        s->tick_counter--;
        if (s->tick_counter <= 0 && !s->finished) {
            advance_tick(s);

            int base = s->samples_per_tick;
            int swing_offset = (int)((float)base * s->swing);
            if (s->current_tick % 2 == 0)
                s->tick_counter = base + swing_offset;
            else
                s->tick_counter = base - swing_offset;
            if (s->tick_counter < base / 2)
                s->tick_counter = base / 2;
        }

        /* mix tonal channels */
        float mix = 0.0f;
        for (int c = 0; c < NUM_CHANNELS; c++) {
            Channel *ch = &s->channels[c];
            if (c == CH_DRUMS) continue;  /* drums handled separately */
            if (ch->env.stage == ENV_IDLE) continue;

            /* ── chip: adjust waveform / duty before generation ── */
            int   save_wf   = ch->waveform;
            float save_duty  = ch->duty;
            int   adj_wf    = ch->waveform;
            float adj_duty   = ch->duty;
            chip_adjust_voice(s->chip_type, c, &adj_wf, &adj_duty,
                              (float)ch->vib_phase);
            ch->waveform = adj_wf;
            ch->duty     = adj_duty;

            float sample = gen_sample(ch);

            /* restore originals so the composer's intent is preserved */
            ch->waveform = save_wf;
            ch->duty     = save_duty;

            /* ── chip: per-channel colouring ── */
            sample = chip_color_sample(&s->chip_state, sample, c);

            float env    = env_process(&ch->env);
            float out    = sample * env * ch->volume;
            mix += out;

            float abs_out = fabsf(out);
            if (abs_out > level_accum[c])
                level_accum[c] = abs_out;

            if (ch->samples_left > 0) {
                ch->samples_left--;
                if (ch->samples_left == 0)
                    env_release_env(&ch->env);
            }
        }

        /* drum mix */
        float drum_mix = 0.0f;
        for (int d = 0; d < 4; d++)
            drum_mix += drum_process(&s->drum_voices[d]);
        drum_mix *= s->channels[CH_DRUMS].volume;
        drum_mix = chip_color_sample(&s->chip_state, drum_mix, CH_DRUMS);
        mix += drum_mix;

        float abs_drum = fabsf(drum_mix);
        if (abs_drum > level_accum[CH_DRUMS])
            level_accum[CH_DRUMS] = abs_drum;

        /* echo / delay */
        float delayed = s->delay_buf[s->delay_read % DELAY_SIZE];
        float echo_in = mix + delayed * s->delay_feedback;
        s->delay_buf[s->delay_write % DELAY_SIZE] = echo_in;
        s->delay_write = (s->delay_write + 1) % DELAY_SIZE;
        s->delay_read  = (s->delay_read + 1) % DELAY_SIZE;
        mix = mix + delayed * s->delay_mix;

        /* one-pole LPF */
        s->lpf_state = s->lpf_alpha * s->lpf_state +
                       (1.0f - s->lpf_alpha) * mix;
        mix = s->lpf_state;

        /* master fade */
        if (s->master_fade < s->fade_target) {
            s->master_fade += s->fade_speed;
            if (s->master_fade > s->fade_target)
                s->master_fade = s->fade_target;
        } else if (s->master_fade > s->fade_target) {
            s->master_fade -= s->fade_speed;
            if (s->master_fade < s->fade_target)
                s->master_fade = s->fade_target;
        }
        mix *= s->master_fade;

        /* chip post-processing (filters, quantisation, etc.) */
        mix = chip_post_process(&s->chip_state, mix);

        /* soft clip */
        if (mix > 1.0f) mix = 1.0f;
        else if (mix < -1.0f) mix = -1.0f;
        else mix = mix * (1.5f - 0.5f * mix * mix);

        buffer[i] = (int16_t)(mix * 30000.0f);
    }

    /* update visualization data */
    for (int c = 0; c < NUM_CHANNELS; c++)
        s->ch_levels[c] = level_accum[c];

    s->current_note = s->channels[CH_LEAD].midi_note;

    int total = (s->num_sections > 0)
        ? s->sections[s->num_sections - 1].end_tick : 1;
    s->progress = (float)s->current_tick / (float)total;
    if (s->progress > 1.0f) s->progress = 1.0f;
}
