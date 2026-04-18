#include "display.h"
#include "re_data.h"
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

/* ── Constants ─────────────────────────────────────────────────────── */

static const char *NOTE_NAMES[] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

static const char *CH_NAMES[] = {
    "LEAD  ", "HARMON", "BASS  ", "ARPEG ", "PAD   ", "DRUMS "
};

/* ── Disasm / quote state ──────────────────────────────────────────── */

#define TICKER_LINES 8
static int s_ticker_idx;

static int  s_quote_idx;
static int  s_quote_timer;
#define QUOTE_DURATION 90  /* frames ≈ 3 s at 30 fps */

/* ── Shared state ──────────────────────────────────────────────────── */

static char          s_filename[256];
static size_t        s_filesize;
static int           s_frame;
static const uint8_t *s_file_data;
static size_t        s_file_data_size;

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

void display_init(const char *filename, size_t filesize,
                  const uint8_t *data, size_t data_size)
{
    get_term_size();
    strncpy(s_filename, filename, sizeof(s_filename) - 1);
    s_filename[sizeof(s_filename) - 1] = '\0';
    s_filesize   = filesize;
    s_file_data  = data;
    s_file_data_size = data_size;
    s_frame      = 0;
    s_pos        = 0;
    s_rng        = (uint32_t)(filesize ^ 0xDEADBEEF);
    s_ticker_idx = 0;
    s_quote_idx  = (int)(rng() % NUM_RE_QUOTES);
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
    if (read(STDIN_FILENO, &c, 1) == 1) return c;
    return 0;
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

    /* Right-side info */
    char info[64];
    int n = snprintf(info, sizeof(info), " [%s] %.0f BPM ",
                     s->section_name, (double)s->bpm);
    int pad = bw - 13 - n;
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

    /* Magic bytes — real bytes from the file + recognized format label */
    MOVETO(row + 1, 48);
    FG(60, 60, 80);
    buf_printf("MAGIC ");
    if (s_file_data && s_file_data_size >= 4) {
        const uint8_t *m = s_file_data;
        FG(100, 140, 120);
        buf_printf("%02X %02X %02X %02X",
                   m[0], m[1], m[2], m[3]);

        /* Identify common formats from magic bytes */
        const char *fmt = NULL;
        if (m[0] == 0xCF && m[1] == 0xFA && m[2] == 0xED && m[3] == 0xFE)
            fmt = "Mach-O 64";
        else if (m[0] == 0xCE && m[1] == 0xFA && m[2] == 0xED && m[3] == 0xFE)
            fmt = "Mach-O 32";
        else if (m[0] == 0xCA && m[1] == 0xFE && m[2] == 0xBA && m[3] == 0xBE)
            fmt = "Fat Mach-O";
        else if (m[0] == 0xFE && m[1] == 0xED && m[2] == 0xFA && m[3] == 0xCF)
            fmt = "Mach-O 64 BE";
        else if (m[0] == 0x7F && m[1] == 'E' && m[2] == 'L' && m[3] == 'F')
            fmt = "ELF";
        else if (m[0] == 'M' && m[1] == 'Z')
            fmt = "PE/MZ";
        else if (m[0] == 0x89 && m[1] == 'P' && m[2] == 'N' && m[3] == 'G')
            fmt = "PNG";
        else if (m[0] == 0xFF && m[1] == 0xD8 && m[2] == 0xFF)
            fmt = "JPEG";
        else if (m[0] == 'G' && m[1] == 'I' && m[2] == 'F')
            fmt = "GIF";
        else if (m[0] == 'P' && m[1] == 'K' && m[2] == 0x03 && m[3] == 0x04)
            fmt = "ZIP";
        else if (m[0] == 0x1F && m[1] == 0x8B)
            fmt = "gzip";
        else if (m[0] == 'B' && m[1] == 'Z' && m[2] == 'h')
            fmt = "bzip2";
        else if (m[0] == 0xFD && m[1] == '7' && m[2] == 'z' && m[3] == 'X')
            fmt = "XZ";
        else if (m[0] == 0x25 && m[1] == 0x50 && m[2] == 0x44 && m[3] == 0x46)
            fmt = "PDF";
        else if (m[0] == 'd' && m[1] == 'e' && m[2] == 'x' && m[3] == 0x0A)
            fmt = "DEX";
        else if (m[0] == 0x4D && m[1] == 0x53 && m[2] == 0x43 && m[3] == 0x46)
            fmt = "CAB";
        else if (m[0] == 0x52 && m[1] == 0x61 && m[2] == 0x72 && m[3] == 0x21)
            fmt = "RAR";

        if (fmt) {
            buf_printf(" ");
            FG(0, 200, 150);
            buf_printf(BOLD "[%s]" RESET, fmt);
        }
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

static void draw_scope(int row, int w, const SynthState *s)
{
    int vw = w - 8;
    if (vw < 20) vw = 20;
    if (vw > 80) vw = 80;
    int vh = 7;

    MOVETO(row, 4);
    FG(80, 80, 100);
    buf_printf("SCOPE ");
    FG(50, 50, 70);
    buf_printf("0x%04X", (s_frame * 0x40) & 0xFFFF);
    buf_printf(RESET);

    for (int r = 0; r < vh; r++) {
        MOVETO(row + 1 + r, 4);
        FG(40, 40, 60);
        buf_printf("%02X│", (unsigned)((r * 0x10 + s_frame) & 0xFF));

        float rl = 1.0f - (float)r / (float)(vh - 1);

        for (int c = 0; c < vw; c++) {
            float t  = (float)c / (float)vw;
            float ph = (float)s_frame * 0.08f;

            float f1 = 2.0f + s->ch_levels[CH_LEAD] * 8.0f;
            float f2 = 1.0f + s->ch_levels[CH_BASS] * 4.0f;
            float f3 = 3.0f + s->ch_levels[CH_ARPEGGIO] * 6.0f;

            float wave = 0.5f
                + 0.22f * sinf(t * f1 * 6.2832f + ph)
                + 0.13f * sinf(t * f2 * 6.2832f + ph * 0.7f)
                + 0.08f * sinf(t * f3 * 6.2832f + ph * 1.3f)
                + s->ch_levels[CH_DRUMS] * 0.15f
                  * sinf(t * 2.0f * 6.2832f + ph * 2.0f);

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

/* ── Hex dump of real file bytes ───────────────────────────────────── */

static void draw_hex_dump(int row, int w, const SynthState *s)
{
    (void)w;
    int nrows = 4, bpr = 8;

    MOVETO(row, 4);
    FG(80, 80, 100);
    buf_printf("HEX DUMP ");
    FG(50, 50, 70);
    int si = (s_frame / 60) % (int)NUM_SECTION_NAMES;
    buf_printf("[%s]" RESET, SECTION_NAMES[si]);

    size_t off = 0;
    if (s_file_data_size > 0) {
        off = (size_t)(s->progress * (float)(s_file_data_size > 64 ? s_file_data_size - 64 : 0));
        off &= ~(size_t)0xF;
    }

    for (int r = 0; r < nrows; r++) {
        MOVETO(row + 1 + r, 4);
        FG(60, 60, 80);
        buf_printf("%08zX  ", off + (size_t)(r * bpr));

        for (int b = 0; b < bpr; b++) {
            size_t idx = off + (size_t)(r * bpr + b);
            if (s_file_data && idx < s_file_data_size) {
                uint8_t v = s_file_data[idx];
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
            size_t idx = off + (size_t)(r * bpr + b);
            if (s_file_data && idx < s_file_data_size) {
                uint8_t v = s_file_data[idx];
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

static void draw_disasm(int row, int w)
{
    int tw = 32;
    int cx = w - tw - 2;
    if (cx < 40) return;

    MOVETO(row, cx);
    FG(70, 70, 90);
    buf_printf("┌─ DISASM ");
    for (int i = 0; i < tw - 10; i++) buf_printf("─");
    buf_printf("┐");

    if (s_frame % 4 == 0) {
        s_ticker_idx++;
        if (s_ticker_idx >= (int)NUM_FAKE_DISASM)
            s_ticker_idx = 0;
    }

    for (int ln = 0; ln < TICKER_LINES; ln++) {
        MOVETO(row + 1 + ln, cx);
        FG(40, 40, 60);
        buf_printf("│");

        int di = (s_ticker_idx + ln) % (int)NUM_FAKE_DISASM;
        int addr = 0x401000 + di * 4 + s_frame;

        FG(80, 80, 100);
        buf_printf("%04x: ", addr & 0xFFFF);

        if (ln == 0) {
            FG(0, 255, 180);
            buf_printf(BOLD "▶ ");
        } else {
            buf_printf("  ");
        }

        char instr[32];
        snprintf(instr, sizeof(instr), "%-20s", FAKE_DISASM[di]);
        if (ln == 0)      FG(0, 255, 180);
        else if (ln < 3)  FG(0, 150, 100);
        else              FG(50, 70, 60);
        buf_printf("%.20s" RESET, instr);

        FG(40, 40, 60);
        buf_printf("│");
    }

    MOVETO(row + TICKER_LINES + 1, cx);
    FG(40, 40, 60);
    buf_printf("└");
    for (int i = 0; i < tw; i++) buf_printf("─");
    buf_printf("┘" RESET);
}

/* ── Register panel (right side) ───────────────────────────────────── */

static void draw_regs(int row, int w, const SynthState *s)
{
    int cx = w - 34;
    if (cx < 40) return;

    MOVETO(row, cx);
    FG(70, 70, 90);
    buf_printf("┌─ REGS ────────────────────┐");

    for (int i = 0; i < 4; i++) {
        MOVETO(row + 1 + i, cx);
        FG(40, 40, 60);
        buf_printf("│ ");
        for (int j = 0; j < 2; j++) {
            int ri = ((s_frame / 3 + i * 2 + j) * 7) % (int)NUM_REGISTERS;
            uint32_t val = rng();
            if (j == 0) val = (uint32_t)(s->ch_levels[i % NUM_CHANNELS] * 0xFFFF);
            FG(100, 120, 140);
            buf_printf("%-4s", REGISTERS[ri]);
            FG(0, 180, 130);
            buf_printf("%04X ", val & 0xFFFF);
        }
        FG(40, 40, 60);
        buf_printf("│" RESET);
    }

    MOVETO(row + 5, cx);
    FG(40, 40, 60);
    buf_printf("└────────────────────────────┘" RESET);
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
        buf_printf("q" RESET " quit");
    } else if (s->finished) {
        FG(0, 255, 100);
        buf_printf(BOLD "✓ COMPLETE " RESET);
        FG(80, 80, 100);
        buf_printf("q" RESET " quit");
    } else {
        FG(0, 255, 100);
        buf_printf(BOLD "▶ PLAYING  " RESET);
        FG(80, 80, 100);
        buf_printf("space" RESET " pause   ");
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
       1-3     Header box
       4       Beat indicator
       5-6     File info / note / magic
       7       (blank)
       8-13    Channel meters (6 channels)
       14      (blank)
       15-22   Oscilloscope (1 label + 7 rows)
       23      (blank)
       24-28   Hex dump (1 label + 4 rows)
       29      (blank)
       30      Quote
       31      (blank)
       32      Progress bar
       33      (blank)
       34      Status line
       Right panels (col w-34): disasm at row 8, regs at row 17
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

    /* Oscilloscope */
    draw_scope(15, w, s);

    /* Hex dump */
    draw_hex_dump(23, w, s);

    /* Right-side panels */
    draw_disasm(8, w);
    draw_regs(17, w, s);

    /* Quote */
    draw_quote(29, w);

    /* Progress */
    draw_progress(31, w, s);

    /* Status */
    draw_status(33, s);

    /* Single atomic flush */
    buf_flush();
}
