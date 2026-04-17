#include "analyzer.h"
#include <math.h>
#include <string.h>

void analyze_section(const uint8_t *data, size_t len, SectionStats *out)
{
    if (len == 0) { memset(out, 0, sizeof(*out)); return; }

    int freq[256];
    memset(freq, 0, sizeof(freq));
    double sum = 0.0;
    int zeros = 0;

    for (size_t i = 0; i < len; i++) {
        freq[data[i]]++;
        sum += data[i];
        if (data[i] == 0) zeros++;
    }

    double n = (double)len;
    out->mean       = sum / n;
    out->zero_ratio = (double)zeros / n;

    double var_sum = 0.0;
    for (size_t i = 0; i < len; i++) {
        double diff = (double)data[i] - out->mean;
        var_sum += diff * diff;
    }
    out->variance = var_sum / n;

    double entropy = 0.0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            double p = (double)freq[i] / n;
            entropy -= p * log2(p);
        }
    }
    out->entropy = entropy;
}

/* ── Pre-built rhythm pattern banks ─────────────────────────────────── */

static const uint16_t KICK_PATS[10] = {
    0x8888, 0x8808, 0xA0A0, 0x8828, 0x8080,
    0x88A0, 0xA8A8, 0x8890, 0x8480, 0xA880,
};
static const uint16_t SNARE_PATS[10] = {
    0x0808, 0x0828, 0x2828, 0x0A08, 0x0848,
    0x0818, 0x2808, 0x080A, 0x0908, 0x2820,
};
static const uint16_t HAT_PATS[10] = {
    0xAAAA, 0xFFFF, 0xEEEE, 0x5555, 0xBBBB,
    0xDDDD, 0xCCCC, 0xEAEA, 0xF0F0, 0x9999,
};
static const uint16_t MELODY_PATS[10] = {
    0x9999, 0x8888, 0xAAAA, 0xE8E8, 0xB4B4,
    0x9292, 0xD8D8, 0x8C8C, 0xE4E4, 0xA4A4,
};
static const uint16_t ARP_PATS[10] = {
    0xFFFF, 0xEEEE, 0xAAAA, 0xBBBB, 0xDDDD,
    0x7777, 0xCCCC, 0xF0F0, 0x5555, 0xFAFA,
};

static uint8_t safe_byte(const uint8_t *data, size_t size, size_t idx)
{
    return (idx < size) ? data[idx] : 0;
}

void analyze_global(const uint8_t *data, size_t size, GlobalAnalysis *out)
{
    memset(out, 0, sizeof(*out));

    if (size == 0) {
        out->base_tempo = 120.0f;
        out->duty_base = 0.5f;
        out->echo_feedback = 0.25f;
        out->vibrato_depth = 0.15f;
        out->kick_pattern = 0x8888;
        out->snare_pattern = 0x0808;
        out->hat_pattern = 0xAAAA;
        out->melody_rhythm = 0x9999;
        out->arp_rhythm = 0xFFFF;
        return;
    }

    /* Sample bytes from strategic file positions */
    uint8_t b0  = safe_byte(data, size, 0);
    uint8_t b1  = safe_byte(data, size, 1);
    uint8_t b2  = safe_byte(data, size, 2);
    uint8_t b3  = safe_byte(data, size, 3);
    uint8_t b4  = safe_byte(data, size, 4);
    uint8_t bq1 = safe_byte(data, size, size / 4);
    uint8_t bq2 = safe_byte(data, size, size / 2);
    uint8_t bq3 = safe_byte(data, size, 3 * size / 4);
    uint8_t bend = safe_byte(data, size, size > 1 ? size - 1 : 0);

    /* Musical parameters derived directly from bytes */
    out->scale_index        = b0 % 10;
    out->progression_index  = (b1 + b2) % 8;
    out->root_note          = b3 % 12;
    out->base_tempo         = 100.0f + (float)b4 * (80.0f / 255.0f);
    out->style              = bq1 % 4;
    out->swing              = (float)(bq2 % 36) / 100.0f;
    out->duty_base          = 0.125f + (float)(bq3 % 128) / 255.0f;
    out->vibrato_depth      = 0.05f + (float)(bend % 36) / 100.0f;
    out->echo_feedback      = 0.15f + (float)((bq1 + bq3) % 31) / 100.0f;

    /* Drum patterns from byte pairs at 1/6, 2/6, 3/6 positions */
    {
        size_t dp = size / 6;
        if (dp == 0) dp = 1;
        out->kick_pattern  = KICK_PATS[(safe_byte(data, size, dp) + safe_byte(data, size, dp+1)) % 10];
        out->snare_pattern = SNARE_PATS[(safe_byte(data, size, dp*2) + safe_byte(data, size, dp*2+1)) % 10];
        out->hat_pattern   = HAT_PATS[(safe_byte(data, size, dp*3) + safe_byte(data, size, dp*3+1)) % 10];
    }

    /* Melody & arp rhythm from 4/5 position */
    {
        size_t rp = size / 5;
        if (rp == 0) rp = 1;
        out->melody_rhythm = MELODY_PATS[safe_byte(data, size, rp * 4) % 10];
        out->arp_rhythm    = ARP_PATS[safe_byte(data, size, rp * 4 + 1) % 10];
    }

    /* Extract 4 melodic motifs from evenly-spaced file positions */
    for (int m = 0; m < 4; m++) {
        size_t base = (size * (size_t)(m + 1)) / 5;
        for (int n = 0; n < 8; n++) {
            uint8_t b = safe_byte(data, size, base + (size_t)n);
            out->motifs[m][n] = b % 16;
        }
    }
}
