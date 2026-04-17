#include "synth.h"
#include <math.h>
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

    s->sections        = comp->sections;
    s->num_sections    = comp->num_sections;
    s->current_section = 0;
    if (comp->num_sections > 0)
        strncpy(s->section_name, comp->sections[0].name,
                sizeof(s->section_name) - 1);

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
                strncpy(s->section_name, s->sections[i].name,
                        sizeof(s->section_name) - 1);

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

            float sample = gen_sample(ch);
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
