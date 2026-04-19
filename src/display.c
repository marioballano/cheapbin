#include "display.h"
#include "chipemu.h"
#include "re_data.h"
#include "binview.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <stdarg.h>

/* ── ANSI escape codes ─────────────────────────────────────────────── */

#define ESC          "\033["
#define RESET        ESC "0m"
#define BOLD         ESC "1m"
#define DIM          ESC "2m"
#define ITALIC       ESC "3m"
#define HIDE_CURSOR  ESC "?25l"
#define SHOW_CURSOR  ESC "?25h"
#define ALT_BUF_ON   ESC "?1049h"
#define ALT_BUF_OFF  ESC "?1049l"
#define CLEAR        ESC "2J"
#define HOME         ESC "H"
#define ERASE_LINE   ESC "2K"

/* ── Terminal state ────────────────────────────────────────────────── */

static struct termios s_orig_termios;
static bool           s_raw_mode = false;
static int            s_cols;
static int            s_rows;

static void get_term_size(void)
{
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        s_cols = w.ws_col;
        s_rows = w.ws_row;
    } else {
        s_cols = 80;
        s_rows = 24;
    }
}

/* ── Output buffer — ALL rendering goes through this ───────────────── */

#define OUTBUF_SIZE (1 << 17)  /* 128 KB */
static char  s_buf[OUTBUF_SIZE];
static int   s_pos;

static void buf_flush(void)
{
    if (s_pos > 0) {
        (void)!write(STDOUT_FILENO, s_buf, (size_t)s_pos);
        s_pos = 0;
    }
}

__attribute__((format(printf, 1, 2)))
static void buf_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s_buf + s_pos, OUTBUF_SIZE - s_pos, fmt, ap);
    va_end(ap);
    if (n > 0) {
        s_pos += n;
        if (s_pos >= OUTBUF_SIZE - 2048) buf_flush();
    }
}

/* Convenience macros — everything via the buffer */
#define MOVETO(r,c) buf_printf(ESC "%d;%dH", (r), (c))
#define FG(r,g,b)   buf_printf(ESC "38;2;%d;%d;%dm", (r), (g), (b))

/* Display width of a UTF-8 string (columns, not bytes).
   Counts one column per character start byte, skipping continuation bytes. */
static int display_width(const char *str)
{
    int w = 0;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++)
        if ((*p & 0xC0) != 0x80) w++;
    return w;
}

/* ── Constants ─────────────────────────────────────────────────────── */

static const char *NOTE_NAMES[] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

static const char *CH_NAMES[] = {
    "LEAD  ", "HARMON", "BASS  ", "ARPEG ", "PAD   ", "DRUMS "
};

/* ── Disasm / quote state ──────────────────────────────────────────── */

#define TICKER_LINES_MAX BV_MAX_LINES

static int  s_quote_idx;
static int  s_quote_timer;
#define QUOTE_DURATION 90  /* frames ≈ 3 s at 30 fps */

/* ── Shared state ──────────────────────────────────────────────────── */

static char     s_filename[256];
static size_t   s_filesize;
static int      s_frame;
static BinView *s_bv;

/* ── PRNG ──────────────────────────────────────────────────────────── */

static uint32_t s_rng = 0x12345678;

static uint32_t rng(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* ── Init / cleanup / poll ─────────────────────────────────────────── */

void display_init(const char *filename, size_t filesize, BinView *bv)
{
    get_term_size();
    strncpy(s_filename, filename, sizeof(s_filename) - 1);
    s_filename[sizeof(s_filename) - 1] = '\0';
    s_filesize    = filesize;
    s_bv          = bv;
    s_frame       = 0;
    s_pos         = 0;
    s_rng         = (uint32_t)(filesize ^ 0xDEADBEEF);
    s_quote_idx   = (int)(rng() % NUM_RE_QUOTES);
    s_quote_timer = QUOTE_DURATION;

    tcgetattr(STDIN_FILENO, &s_orig_termios);
    struct termios raw = s_orig_termios;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    s_raw_mode = true;

    buf_printf(ALT_BUF_ON HIDE_CURSOR CLEAR HOME);
    buf_flush();
}

void display_cleanup(void)
{
    buf_flush();
    if (s_raw_mode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_orig_termios);
        s_raw_mode = false;
    }
    buf_printf(SHOW_CURSOR ALT_BUF_OFF);
    buf_flush();
}

int display_poll_key(void)
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return 0;
    if (c != 0x1B) return c;

    /* Possible CSI/SS3 escape sequence — bytes arrive back-to-back, so a
     * non-blocking read here picks them up. A lone ESC press leaves the
     * follow-ups empty and we return 0x1B unchanged. */
    unsigned char b1, b2;
    if (read(STDIN_FILENO, &b1, 1) != 1) return 0x1B;
    if (b1 != '[' && b1 != 'O') return b1;
    if (read(STDIN_FILENO, &b2, 1) != 1) return 0x1B;
    switch (b2) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    default:  return 0;
    }
}

/* ══════════════════════════════════════════════════════════════════════
   Drawing helpers — all use buf_printf / MOVETO / FG, never printf.
   ══════════════════════════════════════════════════════════════════════ */

/* ── Header box ────────────────────────────────────────────────────── */

static void draw_header(int w, const SynthState *s)
{
    int bw = w - 6;
    if (bw < 30) bw = 30;

    /* Top border */
    MOVETO(1, 3);
    FG(0, 255, 200);
    buf_printf(BOLD "╔");
    for (int i = 0; i < bw; i++) buf_printf("═");
    buf_printf("╗" RESET);

    /* Title line */
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
    buf_printf(" " DIM "%c" RESET, spinners[s_frame & 3]);

    /* Chip indicator */
    buf_printf(" ");
    FG(180, 80, 255);
    buf_printf(BOLD "◈" RESET " ");
    FG(200, 160, 255);
    buf_printf("%s" RESET, chip_short_name(s->chip_type));

    /* Style indicator */
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

    /* Right-side info */
    char info[64];
    int n = snprintf(info, sizeof(info), " [%s] %.0f BPM ",
                     s->section_name, (double)s->bpm);
    int chip_len = 3 + display_width(chip_short_name(s->chip_type));
    int pad = bw - 13 - n - chip_len - style_len;
    for (int i = 0; i < pad; i++) buf_printf(" ");
    FG(255, 200, 0);
    buf_printf(BOLD "%s" RESET, info);
    FG(0, 255, 200);
    buf_printf(BOLD "║" RESET);

    /* Bottom border */
    MOVETO(3, 3);
    FG(0, 255, 200);
    buf_printf(BOLD "╚");
    for (int i = 0; i < bw; i++) buf_printf("═");
    buf_printf("╝" RESET);
}

/* ── File info line ────────────────────────────────────────────────── */

static void draw_file_info(int row, const SynthState *s)
{
    MOVETO(row, 4);
    FG(100, 100, 120);
    buf_printf("FILE ");
    FG(200, 200, 255);
    buf_printf(BOLD "%s" RESET, s_filename);

    /* Size */
    MOVETO(row + 1, 4);
    FG(100, 100, 120);
    buf_printf("SIZE " RESET);
    FG(200, 200, 255);
    if (s_filesize >= 1048576)
        buf_printf("%.1f MB", (double)s_filesize / 1048576.0);
    else if (s_filesize >= 1024)
        buf_printf("%.1f KB", (double)s_filesize / 1024.0);
    else
        buf_printf("%zu bytes", s_filesize);
    buf_printf(RESET);

    /* Note */
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

    /* Magic bytes — first 4 file bytes plus the binview-detected format */
    MOVETO(row + 1, 48);
    FG(60, 60, 80);
    buf_printf("MAGIC ");
    uint8_t magic[4] = {0};
    size_t mn = binview_read(s_bv, 0, magic, 4);
    if (mn >= 4) {
        FG(100, 140, 120);
        buf_printf("%02X %02X %02X %02X",
                   magic[0], magic[1], magic[2], magic[3]);
        const char *fmt = binview_format(s_bv);
        if (fmt && *fmt) {
            buf_printf(" ");
            FG(0, 200, 150);
            buf_printf(BOLD "[%s]" RESET, fmt);
        }
    }
    if (binview_has_r2(s_bv)) {
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

        /* Opcode tag */
        buf_printf(RESET " ");
        if (lv > 0.05f) {
            int oi = (s_frame + c * 7) % (int)NUM_OPCODES;
            FG(60, 60, 80);
            buf_printf("%-6s", OPCODES[oi]);
        }
        buf_printf(RESET);
    }
}

/* ── Oscilloscope ──────────────────────────────────────────────────── */

static void draw_scope(int row, int w, int nrows, const SynthState *s)
{
    /* Scope rows overlap with the right panel (disasm/regs at cols w-34..w-1
     * when drawn). Content starts at col 7 (after "%02X│" prefix); stop two
     * cols before the right panel, or the terminal edge when it's hidden. */
    int right_limit = w >= 74 ? w - 36 : w - 2;
    int vw = right_limit - 6;
    if (vw < 20) vw = 20;
    int vh = nrows;
    if (vh < 2) vh = 2;

    MOVETO(row, 4);
    FG(80, 80, 100);
    buf_printf("SCOPE ");
    FG(50, 50, 70);
    buf_printf("0x%04X", (s_frame * 0x40) & 0xFFFF);
    buf_printf(RESET);

    for (int r = 0; r < vh; r++) {
        MOVETO(row + 1 + r, 4);
        FG(40, 40, 60);
        unsigned tick = s->paused ? 0 : (unsigned)s_frame;
        buf_printf("%02X│", (r * 0x10 + tick) & 0xFF);

        float rl = 1.0f - (float)r / (float)(vh - 1);

        for (int c = 0; c < vw; c++) {
            float t  = (float)c / (float)vw;
            float ph = s->paused ? 0.0f : (float)s_frame * 0.08f;

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
            } else if ((c + r + s_frame / 4) % 12 == 0) {
                FG(20, 25, 20);
                buf_printf("·");
            } else {
                buf_printf(" ");
            }
        }
        buf_printf(RESET);
    }
}

/* ── Hex dump from the entrypoint exec section (or file when no r2) ── */

static void draw_hex_dump(int row, int w, int nrows, const SynthState *s)
{
    enum { MAX_BPR = 64, MAX_NROWS = 24 };
    if (nrows < 1) nrows = 1;
    if (nrows > MAX_NROWS) nrows = MAX_NROWS;
    /* Row width = "%08llX  " (10) + "%02X " × bpr (3*bpr) + "│" + bpr + "│"
     *           = 12 + 4*bpr. Span the whole width from col 4 up to the
     * disasm/regs column (col w-34 when that panel is visible) — pick the
     * largest integer bpr that fits so the block fills the row. */
    int avail = (w >= 74 ? w - 34 : w) - 4 - 1;
    int bpr = (avail - 12) / 4;
    if (bpr < 4)        bpr = 4;
    if (bpr > MAX_BPR)  bpr = MAX_BPR;
    const int total = nrows * bpr;

    MOVETO(row, 4);
    FG(80, 80, 100);
    buf_printf("HEX DUMP ");
    FG(50, 50, 70);
    int si = (s_frame / 60) % (int)NUM_SECTION_NAMES;
    buf_printf("[%s]" RESET, SECTION_NAMES[si]);

    /* When r2 is active, scroll across the exec section anchored at entry.
     * Otherwise fall back to walking the raw file by playback progress. */
    uint64_t base;
    if (binview_has_r2(s_bv)) {
        uint64_t text = binview_text_addr(s_bv);
        size_t   tsz  = binview_text_size(s_bv);
        if (tsz < (size_t)total) tsz = (size_t)total;
        uint64_t span = (uint64_t)(tsz - (size_t)total);
        base = text + (uint64_t)(s->progress * (float)span);
    } else {
        size_t fsz = s_filesize > (size_t)total ? s_filesize - (size_t)total : 0;
        base = (uint64_t)(s->progress * (float)fsz);
    }
    base -= base % (uint64_t)bpr;   /* align to row boundary (bpr may be non-power-of-2) */

    uint8_t buf[MAX_NROWS * MAX_BPR];
    size_t  got = binview_read(s_bv, base, buf, (size_t)total);

    for (int r = 0; r < nrows; r++) {
        MOVETO(row + 1 + r, 4);
        FG(60, 60, 80);
        buf_printf("%08llX  ", (unsigned long long)(base + (uint64_t)(r * bpr)));

        for (int b = 0; b < bpr; b++) {
            size_t idx = (size_t)(r * bpr + b);
            if (idx < got) {
                uint8_t v = buf[idx];
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
                uint8_t v = buf[idx];
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
    int tw = 32;
    int cx = w - tw - 2;
    if (cx < 40) return;
    if (nrows < 1) nrows = 1;
    if (nrows > TICKER_LINES_MAX) nrows = TICKER_LINES_MAX;

    /* Step ESIL once every 4 frames for a calm scroll. Anchor PC to the
     * same address the hex dump is showing so emulation tracks playback
     * progress instead of walking off into a dead end. */
    if ((s_frame & 3) == 0) {
        uint64_t music_pc = 0;
        if (binview_has_r2(s_bv)) {
            uint64_t text = binview_text_addr(s_bv);
            size_t   tsz  = binview_text_size(s_bv);
            uint64_t span = tsz > 16 ? (uint64_t)(tsz - 16) : 0;
            music_pc = text + (uint64_t)(s->progress * (float)span);
        }
        binview_step(s_bv, music_pc);
    }

    MOVETO(row, cx);
    FG(70, 70, 90);
    buf_printf("┌─ DISASM ");
    for (int i = 0; i < tw - 13; i++) buf_printf("─");
    buf_printf("┐");

    char     lines[TICKER_LINES_MAX][BV_LINE_LEN];
    uint64_t addrs[TICKER_LINES_MAX];
    uint64_t base = binview_pc(s_bv);
    if (base == 0) base = binview_text_addr(s_bv);
    int n = binview_disasm(s_bv, base, nrows, addrs, lines);

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

/* ── Register panel (right side, sized to match the disasm box) ──── */

#define REG_ROWS 8
#define REG_COLS 2

static void draw_regs(int row, int w)
{
    int tw = 32;
    int cx = w - tw - 2;
    if (cx < 40) return;

    MOVETO(row, cx);
    FG(70, 70, 90);
    buf_printf("┌─ REGS ");
    for (int i = 0; i < tw - 11; i++) buf_printf("─");
    buf_printf("┐");

    BvReg regs[BV_MAX_REGS];
    int   nregs = binview_regs(s_bv, regs, BV_MAX_REGS);

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
    s_quote_timer--;
    if (s_quote_timer <= 0) {
        s_quote_idx = (int)(rng() % NUM_RE_QUOTES);
        s_quote_timer = QUOTE_DURATION;
    }

    const char *q = RE_QUOTES[s_quote_idx % NUM_RE_QUOTES];
    int ml = w - 10;
    if (ml < 10) return;

    MOVETO(row, 1);
    buf_printf(ERASE_LINE);
    MOVETO(row, 4);

    /* Fade in/out */
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
        FG(80, 80, 100);
        buf_printf("space" RESET " resume  ");
        FG(80, 80, 100);
        buf_printf("c" RESET " chip   ");
        FG(80, 80, 100);
        buf_printf("s" RESET " style  ");
        FG(80, 80, 100);
        buf_printf("q" RESET " quit");
    } else if (s->finished) {
        FG(0, 255, 100);
        buf_printf(BOLD "✓ COMPLETE " RESET);
        FG(80, 80, 100);
        buf_printf("c" RESET " chip   ");
        FG(80, 80, 100);
        buf_printf("s" RESET " style  ");
        FG(80, 80, 100);
        buf_printf("q" RESET " quit");
    } else {
        FG(0, 255, 100);
        buf_printf(BOLD "▶ PLAYING  " RESET);
        FG(80, 80, 100);
        buf_printf("space" RESET " pause   ");
        FG(80, 80, 100);
        buf_printf("c" RESET " chip   ");
        FG(80, 80, 100);
        buf_printf("s" RESET " style  ");
        FG(80, 80, 100);
        buf_printf("q" RESET " quit");
    }
    buf_printf(RESET);
}

/* ── Subtle background texture (no animation, just static dots) ───── */

static void draw_bg_texture(int w, int h)
{
    /* Very faint scattered hex fragments — only in empty margin areas.
       These sit in columns 1-2 (left edge) and don't move. */
    for (int r = 4; r < h - 1; r += 3) {
        MOVETO(r, 1);
        FG(18, 22, 18);
        /* Rotating hex nibble based on row, changes slowly */
        int v = (r * 37 + s_frame / 30) & 0xF;
        buf_printf("%X", v);
    }
    /* Right edge subtle marks */
    for (int r = 4; r < h - 1; r += 4) {
        if (w > 50) {
            MOVETO(r, w);
            FG(18, 22, 18);
            int v = (r * 13 + s_frame / 30) & 0xF;
            buf_printf("%X", v);
        }
    }
    buf_printf(RESET);
}

/* ── Beat indicator (subtle, non-destructive) ─────────────────────── */

static void draw_beat_indicator(int row, const SynthState *s)
{
    /* Small beat dots next to the header */
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
   Main display update — clear → draw all layers → flush once
   ══════════════════════════════════════════════════════════════════════ */

void display_update(const SynthState *s)
{
    get_term_size();
    int w = s_cols;
    int h = s_rows;
    s_frame++;
    s_pos = 0;

    /* ---- Single clear + home ---- */
    buf_printf(HOME CLEAR);

    /* ---- Layout (row numbers, all 1-based) ----
       1-3                   Header box
       4                     Beat indicator
       5-6                   File info / note / magic
       7                     (blank)
       8-13                  Channel meters (6 channels)
       14                    (blank)
       15                    Oscilloscope label
       16 .. 15+SN           Oscilloscope rows (SN, grows with height)
       16+SN                 (blank)
       17+SN                 Hex dump label
       18+SN .. 17+SN+HN     Hex dump rows (HN, capped ~25% of screen)
       quote_row             Quote
       progress_row          Progress bar
       status_row            Status line
       Right panels (col w-34): disasm at row 8, regs at row 18
    */

    /* Background texture */
    draw_bg_texture(w, h);

    /* Header */
    draw_header(w, s);

    /* Beat indicator */
    draw_beat_indicator(4, s);

    /* File info */
    draw_file_info(5, s);

    /* Channel meters */
    draw_meters(8, w, s);

    /* Bottom widgets stick to the terminal's bottom edge when the window
     * is taller than the original 33-row layout. */
    int quote_row    = h >= 33 ? h - 4 : 29;
    if (quote_row < 29) quote_row = 29;
    int progress_row = quote_row + 2;
    int status_row   = progress_row + 2;

    /* Split the middle region between scope and hex dump. The hex dump
     * is useful but secondary — cap it near ~25 % of the terminal so the
     * scope can stretch on tall windows where the waveform looks better
     * with more vertical resolution. */
    int scope_label_row = 15;
    int middle_rows     = quote_row - scope_label_row;   /* scope+blank+hex+blank */
    if (middle_rows < 11) middle_rows = 11;               /* fallback: SN=7 + HN=4 */

    int hex_cap = h / 4;                                  /* ~25 % of screen */
    if (hex_cap < 4) hex_cap = 4;

    int hex_rows   = hex_cap;
    int scope_rows = middle_rows - 3 - hex_rows;          /* minus label+blank+label */
    if (scope_rows < 7) {
        scope_rows = 7;
        hex_rows = middle_rows - 3 - scope_rows;
        if (hex_rows < 4) hex_rows = 4;
    }

    int hex_label_row = scope_label_row + 1 + scope_rows + 1;

    /* Oscilloscope */
    draw_scope(scope_label_row, w, scope_rows, s);

    /* Hex dump */
    draw_hex_dump(hex_label_row, w, hex_rows, s);

    /* Right-side panels — regs is a fixed-size grid and stays anchored at
     * the bottom (its bottom border sits on the row just above the quote
     * line). Disasm stretches upward to fill the remaining vertical space,
     * so taller terminals show more instructions. */
    int regs_panel_rows = REG_ROWS + 2;                    /* borders included */
    int regs_row        = quote_row - 1 - regs_panel_rows; /* top border row */
    if (regs_row < 18) regs_row = 18;                      /* original position */

    /* Disasm starts at row 8 and ends directly above regs (no gap, matching
     * the original tight stack). nrows = regs_row - 10. */
    int disasm_nrows = regs_row - 10;
    if (disasm_nrows < 8)                disasm_nrows = 8;
    if (disasm_nrows > TICKER_LINES_MAX) disasm_nrows = TICKER_LINES_MAX;

    draw_disasm(8, w, disasm_nrows, s);
    draw_regs(regs_row, w);

    /* Quote */
    draw_quote(quote_row, w);

    /* Progress */
    draw_progress(progress_row, w, s);

    /* Status */
    draw_status(status_row, s);

    /* Single atomic flush */
    buf_flush();
}
