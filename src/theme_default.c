#include "display_internal.h"
#include "theme.h"
#include "chipemu.h"
#define USE_RE_QUOTES
#define USE_OPCODES
#define USE_SECTION_NAMES
#include "re_data.h"
#include "binview.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ── Local state ───────────────────────────────────────────────────── */

#define TICKER_LINES_MAX BV_MAX_LINES
#define QUOTE_DURATION   90

static int s_quote_idx   = 0;
static int s_quote_timer = QUOTE_DURATION;

/* ══════════════════════════════════════════════════════════════════════
   Drawing helpers — all use buf_printf / MOVETO / FG via display_internal.h
   ══════════════════════════════════════════════════════════════════════ */

/* ── Header box ────────────────────────────────────────────────────── */

static void draw_header(int w, const SynthState *s)
{
    int frame = di_frame();
    int bw = w - 6;
    if (bw < 30) bw = 30;

    MOVETO(1, 3);
    FG(0, 255, 200);
    buf_printf(BOLD "╔");
    for (int i = 0; i < bw; i++) buf_printf("═");
    buf_printf("╗" RESET);

    MOVETO(2, 3);
    FG(0, 255, 200);
    buf_printf(BOLD "║" RESET " ");
    FG(255, 0, 128);
    buf_printf(BOLD "♪ ");
    FG(0, 255, 255);
    buf_printf("CHEAP");
    FG(255, 100, 0);
    buf_printf("BIN" RESET);

    const char *spinners = "|/-\\";
    buf_printf(" " DIM "%c" RESET, spinners[frame & 3]);

    buf_printf(" ");
    FG(180, 80, 255);
    buf_printf(BOLD "◈" RESET " ");
    FG(200, 160, 255);
    buf_printf("%s" RESET, chip_short_name(s->chip_type));

    int style_len = 0;
    if (s->style_type != STYLE_NONE) {
        const char *sn = style_short_name(s->style_type);
        buf_printf("  ");
        FG(255, 150, 50);
        buf_printf(BOLD "♦" RESET " ");
        FG(255, 200, 100);
        buf_printf("%s" RESET, sn);
        style_len = 4 + display_width(sn);
    }

    const char *kn = scale_short_name(s->scale_type);
    buf_printf("  ");
    FG(100, 220, 150);
    buf_printf(BOLD "♪" RESET " ");
    FG(180, 240, 200);
    buf_printf("%s" RESET, kn);
    int scale_len = 4 + display_width(kn);

    char info[64];
    int n = snprintf(info, sizeof(info), " [%s] %.0f BPM ",
                     s->section_name, (double)s->bpm);
    int chip_len = 3 + display_width(chip_short_name(s->chip_type));
    int pad = bw - 13 - n - chip_len - style_len - scale_len;
    for (int i = 0; i < pad; i++) buf_printf(" ");
    FG(255, 200, 0);
    buf_printf(BOLD "%s" RESET, info);
    FG(0, 255, 200);
    buf_printf(BOLD "║" RESET);

    MOVETO(3, 3);
    FG(0, 255, 200);
    buf_printf(BOLD "╚");
    for (int i = 0; i < bw; i++) buf_printf("═");
    buf_printf("╝" RESET);
}

/* ── File info line ────────────────────────────────────────────────── */

static void draw_file_info(int row, const SynthState *s)
{
    const char *fname = di_filename();
    size_t      fsize = di_filesize();
    BinView    *bv    = di_binview();

    MOVETO(row, 4);
    FG(100, 100, 120);
    buf_printf("FILE ");
    FG(200, 200, 255);
    buf_printf(BOLD "%s" RESET, fname);

    MOVETO(row + 1, 4);
    FG(100, 100, 120);
    buf_printf("SIZE " RESET);
    FG(200, 200, 255);
    if (fsize >= 1048576)
        buf_printf("%.1f MB", (double)fsize / 1048576.0);
    else if (fsize >= 1024)
        buf_printf("%.1f KB", (double)fsize / 1024.0);
    else
        buf_printf("%zu bytes", fsize);
    buf_printf(RESET);

    MOVETO(row + 1, 30);
    int note = s->current_note;
    if (note > 0 && note < 128) {
        FG(100, 100, 120);
        buf_printf("NOTE ");
        FG(0, 255, 255);
        buf_printf(BOLD "%-3s%d" RESET, NOTE_NAMES[note % 12], note / 12 - 1);
    } else {
        FG(100, 100, 120);
        buf_printf("NOTE " DIM "---" RESET);
    }

    MOVETO(row + 1, 48);
    FG(60, 60, 80);
    buf_printf("MAGIC ");
    uint8_t magic[4] = {0};
    size_t mn = binview_file_bytes(bv, 0, magic, 4);
    if (mn >= 4) {
        FG(100, 140, 120);
        buf_printf("%02X %02X %02X %02X",
                   magic[0], magic[1], magic[2], magic[3]);
        const char *fmt = binview_format(bv);
        if (fmt && *fmt) {
            buf_printf(" ");
            FG(0, 200, 150);
            buf_printf(BOLD "[%s]" RESET, fmt);
        }
    }
    if (binview_has_r2(bv)) {
        buf_printf(" ");
        FG(255, 100, 0);
        buf_printf(BOLD "r2" RESET);
    }
    buf_printf(RESET);
}

/* ── Channel meters ────────────────────────────────────────────────── */

static void draw_meters(int row, int w, const SynthState *s)
{
    static const int HI[][3] = {
        {0,255,255},{100,100,255},{0,255,100},{255,0,255},{255,255,0},{255,50,50}
    };
    static const int LO[][3] = {
        {0,80,120},{30,30,120},{0,100,0},{100,0,100},{120,100,0},{120,0,0}
    };

    int frame = di_frame();
    int bw = w - 24;
    if (bw < 10) bw = 10;
    if (bw > 60) bw = 60;

    for (int c = 0; c < NUM_CHANNELS; c++) {
        MOVETO(row + c, 4);

        FG(HI[c][0], HI[c][1], HI[c][2]);
        buf_printf(BOLD "%s" RESET " ", CH_NAMES[c]);

        float lv = s->ch_levels[c];
        if (lv > 1.0f) lv = 1.0f;
        int filled = (int)(lv * (float)bw);

        for (int i = 0; i < bw; i++) {
            float t = (float)i / (float)bw;
            if (i < filled) {
                int r = LO[c][0] + (int)(t * (float)(HI[c][0] - LO[c][0]));
                int g = LO[c][1] + (int)(t * (float)(HI[c][1] - LO[c][1]));
                int b = LO[c][2] + (int)(t * (float)(HI[c][2] - LO[c][2]));
                FG(r, g, b);
                if (i == filled - 1) {
                    float frac = lv * (float)bw - (float)i;
                    if (frac > 0.75f) buf_printf("█");
                    else if (frac > 0.5f) buf_printf("▓");
                    else if (frac > 0.25f) buf_printf("▒");
                    else buf_printf("░");
                } else {
                    buf_printf("█");
                }
            } else {
                FG(30, 30, 40);
                buf_printf("░");
            }
        }

        buf_printf(RESET " ");
        if (lv > 0.05f) {
            int oi = (frame + c * 7) % (int)NUM_OPCODES;
            FG(60, 60, 80);
            buf_printf("%-6s", OPCODES[oi]);
        }
        buf_printf(RESET);
    }
}

/* ── Oscilloscope ──────────────────────────────────────────────────── */

static void draw_scope(int row, int w, int nrows, const SynthState *s)
{
    int frame = di_frame();
    int right_limit = w >= 74 ? w - 36 : w - 2;
    int vw = right_limit - 6;
    if (vw < 20) vw = 20;
    int vh = nrows;
    if (vh < 2) vh = 2;

    MOVETO(row, 4);
    FG(80, 80, 100);
    buf_printf("SCOPE ");
    FG(50, 50, 70);
    buf_printf("0x%04X", (frame * 0x40) & 0xFFFF);
    buf_printf(RESET);

    for (int r = 0; r < vh; r++) {
        MOVETO(row + 1 + r, 4);
        FG(40, 40, 60);
        unsigned tick = s->paused ? 0 : (unsigned)frame;
        buf_printf("%02X│", (r * 0x10 + tick) & 0xFF);

        float rl = 1.0f - (float)r / (float)(vh - 1);

        for (int c = 0; c < vw; c++) {
            float t  = (float)c / (float)vw;
            float ph = s->paused ? 0.0f : (float)frame * 0.08f;

            float wave;
            if (s->paused) {
                wave = 0.5f;
            } else {
                float f1 = 2.0f + s->ch_levels[CH_LEAD] * 8.0f;
                float f2 = 1.0f + s->ch_levels[CH_BASS] * 4.0f;
                float f3 = 3.0f + s->ch_levels[CH_ARPEGGIO] * 6.0f;
                wave = 0.5f
                    + 0.22f * sinf(t * f1 * 6.2832f + ph)
                    + 0.13f * sinf(t * f2 * 6.2832f + ph * 0.7f)
                    + 0.08f * sinf(t * f3 * 6.2832f + ph * 1.3f)
                    + s->ch_levels[CH_DRUMS] * 0.15f
                      * sinf(t * 2.0f * 6.2832f + ph * 2.0f);
            }

            float d = fabsf(rl - wave);

            if (d < 0.06f) {
                int g = 200 + (int)(55.0f * sinf(t * 3.14f + ph));
                if (g > 255) g = 255;
                FG(0, g, (int)(100.0f * (1.0f - t)));
                buf_printf("█");
            } else if (d < 0.12f) {
                FG(0, 180, 80);
                buf_printf("▓");
            } else if (d < 0.20f) {
                FG(0, 100, 50);
                buf_printf("░");
            } else if ((c + r + frame / 4) % 12 == 0) {
                FG(20, 25, 20);
                buf_printf("·");
            } else {
                buf_printf(" ");
            }
        }
        buf_printf(RESET);
    }
}

/* ── Hex dump ──────────────────────────────────────────────────────── */

static void draw_hex_dump(int row, int w, int nrows, const SynthState *s)
{
    BinView *bv    = di_binview();
    size_t   fsize = di_filesize();
    int      frame = di_frame();

    enum { MAX_BPR = 64, MAX_NROWS = 24 };
    if (nrows < 1) nrows = 1;
    if (nrows > MAX_NROWS) nrows = MAX_NROWS;

    int avail = (w >= 74 ? w - 34 : w) - 4 - 1;
    int bpr = (avail - 12) / 4;
    if (bpr < 4)        bpr = 4;
    if (bpr > MAX_BPR)  bpr = MAX_BPR;
    const int total = nrows * bpr;

    MOVETO(row, 4);
    FG(80, 80, 100);
    buf_printf("HEX DUMP ");
    FG(50, 50, 70);
    int si = (frame / 60) % (int)NUM_SECTION_NAMES;
    buf_printf("[%s]" RESET, SECTION_NAMES[si]);

    uint64_t base;
    if (binview_has_r2(bv)) {
        uint64_t text = binview_text_addr(bv);
        size_t   tsz  = binview_text_size(bv);
        if (tsz < (size_t)total) tsz = (size_t)total;
        uint64_t span = (uint64_t)(tsz - (size_t)total);
        base = text + (uint64_t)(s->progress * (float)span);
    } else {
        size_t fsz = fsize > (size_t)total ? fsize - (size_t)total : 0;
        base = (uint64_t)(s->progress * (float)fsz);
    }
    base -= base % (uint64_t)bpr;

    uint8_t hbuf[MAX_NROWS * MAX_BPR];
    size_t  got = binview_read(bv, base, hbuf, (size_t)total);

    for (int r = 0; r < nrows; r++) {
        MOVETO(row + 1 + r, 4);
        FG(60, 60, 80);
        buf_printf("%08llX  ", (unsigned long long)(base + (uint64_t)(r * bpr)));

        for (int b = 0; b < bpr; b++) {
            size_t idx = (size_t)(r * bpr + b);
            if (idx < got) {
                uint8_t v = hbuf[idx];
                if (v == 0)          FG(40, 40, 50);
                else if (v == 0xFF)  FG(255, 100, 100);
                else if (v >= 0x20 && v < 0x7F) FG(0, 200, 150);
                else                 FG(100, 150, 200);
                buf_printf("%02X ", v);
            } else {
                FG(30, 30, 40);
                buf_printf("·· ");
            }
        }

        FG(50, 50, 70);
        buf_printf("│");
        for (int b = 0; b < bpr; b++) {
            size_t idx = (size_t)(r * bpr + b);
            if (idx < got) {
                uint8_t v = hbuf[idx];
                if (v >= 0x20 && v < 0x7F) {
                    FG(0, 180, 130);
                    buf_printf("%c", (char)v);
                } else {
                    FG(40, 40, 50);
                    buf_printf(".");
                }
            } else {
                buf_printf(" ");
            }
        }
        FG(50, 50, 70);
        buf_printf("│" RESET);
    }
}

/* ── Disassembly ticker (right panel) ──────────────────────────────── */

static void draw_disasm(int row, int w, int nrows, const SynthState *s)
{
    BinView *bv    = di_binview();
    int      frame = di_frame();
    int tw = 32;
    int cx = w - tw - 2;
    if (cx < 40) return;
    if (nrows < 1) nrows = 1;
    if (nrows > TICKER_LINES_MAX) nrows = TICKER_LINES_MAX;

    if ((frame & 3) == 0) {
        uint64_t music_pc = 0;
        if (binview_has_r2(bv)) {
            uint64_t text = binview_text_addr(bv);
            size_t   tsz  = binview_text_size(bv);
            uint64_t span = tsz > 16 ? (uint64_t)(tsz - 16) : 0;
            music_pc = text + (uint64_t)(s->progress * (float)span);
        }
        binview_step(bv, music_pc);
    }

    MOVETO(row, cx);
    FG(70, 70, 90);
    buf_printf("┌─ DISASM ");
    for (int i = 0; i < tw - 13; i++) buf_printf("─");
    buf_printf("┐");

    char     lines[TICKER_LINES_MAX][BV_LINE_LEN];
    uint64_t addrs[TICKER_LINES_MAX];
    uint64_t base = binview_pc(bv);
    if (base == 0) base = binview_text_addr(bv);
    int n = binview_disasm(bv, base, nrows, addrs, lines);

    for (int ln = 0; ln < nrows; ln++) {
        MOVETO(row + 1 + ln, cx);
        FG(40, 40, 60);
        buf_printf("│");

        const char *text = ln < n ? lines[ln] : "";
        uint64_t    addr = ln < n ? addrs[ln] : 0;

        FG(80, 80, 100);
        buf_printf("%04x: ", (unsigned)(addr & 0xFFFF));

        if (ln == 0) {
            FG(0, 255, 180);
            buf_printf(BOLD "▶ ");
        } else {
            buf_printf("  ");
        }

        if (ln == 0)      FG(0, 255, 180);
        else if (ln < 3)  FG(0, 150, 100);
        else              FG(50, 70, 60);
        buf_printf("%-20.20s" RESET, text);

        FG(40, 40, 60);
        buf_printf("│");
    }

    MOVETO(row + nrows + 1, cx);
    FG(40, 40, 60);
    buf_printf("└");
    for (int i = 0; i < tw - 4; i++) buf_printf("─");
    buf_printf("┘" RESET);
}

/* ── Register panel ────────────────────────────────────────────────── */

#define REG_ROWS 8
#define REG_COLS 2

static void draw_regs(int row, int w)
{
    BinView *bv = di_binview();
    int tw = 32;
    int cx = w - tw - 2;
    if (cx < 40) return;

    MOVETO(row, cx);
    FG(70, 70, 90);
    buf_printf("┌─ REGS ");
    for (int i = 0; i < tw - 11; i++) buf_printf("─");
    buf_printf("┐");

    BvReg regs[BV_MAX_REGS];
    int   nregs = binview_regs(bv, regs, BV_MAX_REGS);

    for (int r = 0; r < REG_ROWS; r++) {
        MOVETO(row + 1 + r, cx);
        FG(40, 40, 60);
        buf_printf("│ ");
        for (int c = 0; c < REG_COLS; c++) {
            int idx = r * REG_COLS + c;
            char up[BV_REG_NAME];
            up[0] = '\0';
            if (idx < nregs) {
                size_t nl = strlen(regs[idx].name);
                if (nl >= sizeof(up)) nl = sizeof(up) - 1;
                for (size_t k = 0; k < nl; k++)
                    up[k] = (char)toupper((unsigned char)regs[idx].name[k]);
                up[nl] = '\0';
            }
            uint32_t val = idx < nregs ? regs[idx].value : 0;
            FG(100, 120, 140);
            buf_printf("%-6s", up);
            FG(0, 180, 130);
            buf_printf("%04X", val & 0xFFFF);
            FG(40, 40, 60);
            buf_printf(c == 0 ? "    " : " ");
        }
        buf_printf("  │" RESET);
    }

    MOVETO(row + REG_ROWS + 1, cx);
    FG(40, 40, 60);
    buf_printf("└");
    for (int i = 0; i < tw - 4; i++) buf_printf("─");
    buf_printf("┘" RESET);
}

/* ── Rotating RE quote ─────────────────────────────────────────────── */

static void draw_quote(int row, int w)
{
    int frame = di_frame();
    (void)frame;

    s_quote_timer--;
    if (s_quote_timer <= 0) {
        s_quote_idx = (int)(di_rng() % NUM_RE_QUOTES);
        s_quote_timer = QUOTE_DURATION;
    }

    const char *q = RE_QUOTES[s_quote_idx % NUM_RE_QUOTES];
    int ml = w - 10;
    if (ml < 10) return;

    MOVETO(row, 1);
    buf_printf(ERASE_LINE);
    MOVETO(row, 4);

    float a = 1.0f;
    if (s_quote_timer > QUOTE_DURATION - 15)
        a = (float)(QUOTE_DURATION - s_quote_timer) / 15.0f;
    else if (s_quote_timer < 15)
        a = (float)s_quote_timer / 15.0f;

    int br = (int)(120.0f + 80.0f * a);
    if (br > 255) br = 255;

    FG(br / 2, br, br);
    buf_printf(ITALIC "  \"");

    int len = (int)strlen(q);
    if (len > ml - 6)
        buf_printf("%.*s...", ml - 9, q);
    else
        buf_printf("%s", q);
    buf_printf("\"" RESET);
}

/* ── Progress bar ──────────────────────────────────────────────────── */

static void draw_progress(int row, int w, const SynthState *s)
{
    MOVETO(row, 4);

    int pw = w - 16;
    if (pw < 10) pw = 10;
    if (pw > 80) pw = 80;

    int pct = (int)(s->progress * 100.0f);
    if (pct > 100) pct = 100;
    int filled = (int)(s->progress * (float)pw);

    FG(60, 60, 80);
    buf_printf("[");

    for (int i = 0; i < pw; i++) {
        if (i < filled) {
            float t = (float)i / (float)pw;
            int r = (int)(180.0f * (1.0f - t));
            int g = (int)(50.0f + 205.0f * t);
            int b = (int)(200.0f + 55.0f * t);
            if (b > 255) b = 255;
            FG(r, g, b);
            buf_printf("━");
        } else if (i == filled) {
            FG(255, 255, 255);
            buf_printf("╸");
        } else {
            FG(30, 30, 40);
            buf_printf("─");
        }
    }

    FG(60, 60, 80);
    buf_printf("] " RESET BOLD);
    FG(200, 200, 220);
    buf_printf("%3d%%" RESET, pct);
}

/* ── Status line ───────────────────────────────────────────────────── */

static void draw_status(int row, const SynthState *s)
{
    MOVETO(row, 4);
    if (s->paused) {
        FG(255, 200, 0);
        buf_printf(BOLD "⏸ PAUSED  " RESET);
    } else if (s->finished) {
        FG(0, 255, 100);
        buf_printf(BOLD "✓ COMPLETE " RESET);
    } else {
        FG(0, 255, 100);
        buf_printf(BOLD "▶ PLAYING  " RESET);
    }
    FG(80, 80, 100);
    buf_printf("space" RESET " pause   ");
    FG(80, 80, 100);
    buf_printf("h/\xe2\x86\x90  l/\xe2\x86\x92" RESET " seek   ");
    FG(80, 80, 100);
    buf_printf("c" RESET " chip  ");
    FG(80, 80, 100);
    buf_printf("s" RESET " style  ");
    FG(80, 80, 100);
    buf_printf("k" RESET " scale  ");
    FG(80, 80, 100);
    buf_printf("t" RESET " theme  ");
    FG(80, 80, 100);
    buf_printf("q" RESET " quit");
    buf_printf(RESET);
}

/* ── Background texture ────────────────────────────────────────────── */

static void draw_bg_texture(int w, int h)
{
    int frame = di_frame();

    for (int r = 4; r < h - 1; r += 3) {
        MOVETO(r, 1);
        FG(18, 22, 18);
        int v = (r * 37 + frame / 30) & 0xF;
        buf_printf("%X", v);
    }
    for (int r = 4; r < h - 1; r += 4) {
        if (w > 50) {
            MOVETO(r, w);
            FG(18, 22, 18);
            int v = (r * 13 + frame / 30) & 0xF;
            buf_printf("%X", v);
        }
    }
    buf_printf(RESET);
}

/* ── Beat indicator ────────────────────────────────────────────────── */

static void draw_beat_indicator(int row, const SynthState *s)
{
    MOVETO(row, 4);
    float kick = s->ch_levels[CH_DRUMS];
    if (kick > 0.3f) {
        int br = (int)(255.0f * kick);
        if (br > 255) br = 255;
        FG(br, br / 4, 0);
        buf_printf("● ");
    } else {
        FG(30, 30, 40);
        buf_printf("○ ");
    }

    float snare = s->ch_levels[CH_LEAD];
    if (snare > 0.3f) {
        int br = (int)(255.0f * snare);
        if (br > 255) br = 255;
        FG(0, br / 3, br);
        buf_printf("●");
    } else {
        FG(30, 30, 40);
        buf_printf("○");
    }
    buf_printf(RESET);
}

/* ══════════════════════════════════════════════════════════════════════
   Default theme — full-screen draw
   ══════════════════════════════════════════════════════════════════════ */

void theme_default_draw(const SynthState *s)
{
    int w = di_cols();
    int h = di_rows();

    draw_bg_texture(w, h);
    draw_header(w, s);
    draw_beat_indicator(4, s);
    draw_file_info(5, s);
    draw_meters(8, w, s);

    int quote_row    = h >= 33 ? h - 4 : 29;
    if (quote_row < 29) quote_row = 29;
    int progress_row = quote_row + 2;
    int status_row   = progress_row + 2;

    int scope_label_row = 15;
    int middle_rows     = quote_row - scope_label_row;
    if (middle_rows < 11) middle_rows = 11;

    int hex_cap = h / 4;
    if (hex_cap < 4) hex_cap = 4;

    int hex_rows   = hex_cap;
    int scope_rows = middle_rows - 3 - hex_rows;
    if (scope_rows < 7) {
        scope_rows = 7;
        hex_rows = middle_rows - 3 - scope_rows;
        if (hex_rows < 4) hex_rows = 4;
    }

    int hex_label_row = scope_label_row + 1 + scope_rows + 1;

    draw_scope(scope_label_row, w, scope_rows, s);
    draw_hex_dump(hex_label_row, w, hex_rows, s);

    int regs_panel_rows = REG_ROWS + 2;
    int regs_row        = quote_row - 1 - regs_panel_rows;
    if (regs_row < 18) regs_row = 18;

    int disasm_nrows = regs_row - 10;
    if (disasm_nrows < 8)                disasm_nrows = 8;
    if (disasm_nrows > TICKER_LINES_MAX) disasm_nrows = TICKER_LINES_MAX;

    draw_disasm(8, w, disasm_nrows, s);
    draw_regs(regs_row, w);

    draw_quote(quote_row, w);
    draw_progress(progress_row, w, s);
    draw_status(status_row, s);
}
