/*
 * theme_softice.c — SoftICE kernel debugger UI
 *
 * Faithful recreation of the classic SoftICE full-screen debugger:
 *   - Black background, high-contrast terminal palette
 *   - Register window at top with CPU flags (o d I s Z a P c)
 *   - Code/disassembly window with segment:offset addresses
 *   - Data window (hex dump) with ASCII sidebar
 *   - Waveform monitor (unique to cheapbin)
 *   - Console/command window at bottom with module event log
 *   - Green status bar with PROT32 mode indicator
 *
 * Palette (authentic):
 *   Background:  black (#000000)
 *   Text/instr:  white (#FFFFFF)
 *   Reg values:  cyan (#00FFFF)
 *   Dim text:    grey (#808080)
 *   Highlight:   light blue bar (#0000AA background)
 *   Status bar:  green on black / bright green headers
 *   Flags set:   cyan, Flags clear: dim grey
 *   Borders:     dark grey (#555555)
 */

#include "display_internal.h"
#include "theme.h"
#include "chipemu.h"
#include "style.h"
#include "re_data.h"
#include "binview.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ── Authentic SoftICE palette ─────────────────────────────────────── */

#define SI_BG()        BG(0, 0, 0)          /* pure black background    */
#define SI_BG_HL()     BG(0, 0, 170)        /* light blue highlight bar */
#define SI_BG_STATUS() BG(0, 0, 0)          /* status bar background    */

#define SI_FG_WHITE()  FG(255, 255, 255)    /* primary text / instructions */
#define SI_FG_CYAN()   FG(0, 255, 255)      /* register values, addresses  */
#define SI_FG_GREEN()  FG(0, 255, 0)        /* headers, status, PROT32     */
#define SI_FG_DIM()    FG(128, 128, 128)    /* dim / inactive text         */
#define SI_FG_GREY()   FG(170, 170, 170)    /* secondary text              */
#define SI_FG_DKGREY() FG(85, 85, 85)       /* borders, separators         */
#define SI_FG_YELLOW() FG(255, 255, 85)     /* occasional highlights       */
#define SI_FG_RED()    FG(255, 85, 85)      /* alerts                      */

/* ── Local state ───────────────────────────────────────────────────── */

#define QUOTE_DURATION 90
static int si_quote_idx   = 0;
static int si_quote_timer = QUOTE_DURATION;

/* ── Fill a row with black background ──────────────────────────────── */

static void si_clear_row(int row, int w)
{
    MOVETO(row, 1);
    SI_BG();
    for (int i = 0; i < w; i++) buf_printf(" ");
}

/* ── Horizontal divider ────────────────────────────────────────────── */

static void si_divider(int row, int w, const char *label)
{
    MOVETO(row, 1);
    SI_BG();
    SI_FG_DKGREY();
    buf_printf("├");

    if (label) {
        SI_FG_GREEN();
        buf_printf(BOLD " %s " RESET, label);
        SI_BG();
        SI_FG_DKGREY();
        int used = 3 + (int)strlen(label);
        for (int i = used; i < w - 1; i++) buf_printf("─");
    } else {
        for (int i = 1; i < w - 1; i++) buf_printf("─");
    }
    buf_printf("┤" RESET);
}

/* ── Top border ────────────────────────────────────────────────────── */

static void si_top_border(int w)
{
    MOVETO(1, 1);
    SI_BG();
    SI_FG_DKGREY();
    buf_printf("┌");
    for (int i = 1; i < w - 1; i++) buf_printf("─");
    buf_printf("┐" RESET);
}

/* ── Bottom border ─────────────────────────────────────────────────── */

static void si_bottom_border(int row, int w)
{
    MOVETO(row, 1);
    SI_BG();
    SI_FG_DKGREY();
    buf_printf("└");
    for (int i = 1; i < w - 1; i++) buf_printf("─");
    buf_printf("┘" RESET);
}

/* ══════════════════════════════════════════════════════════════════════
   REGISTER WINDOW — 2 rows, white names, cyan values, flag chars
   ══════════════════════════════════════════════════════════════════════ */

static void si_draw_regs(int row, int w, const SynthState *s)
{
    BinView *bv = di_binview();
    BvReg regs[BV_MAX_REGS];
    int nregs = binview_regs(bv, regs, BV_MAX_REGS);

    /* ── Row 1: GP registers ── */
    MOVETO(row, 1);
    SI_BG();
    SI_FG_DKGREY();
    buf_printf("│ ");

    for (int i = 0; i < nregs && i < 8; i++) {
        char up[BV_REG_NAME];
        size_t nl = strlen(regs[i].name);
        if (nl >= sizeof(up)) nl = sizeof(up) - 1;
        for (size_t k = 0; k < nl; k++)
            up[k] = (char)toupper((unsigned char)regs[i].name[k]);
        up[nl] = '\0';

        SI_FG_WHITE();
        buf_printf(BOLD "%-4s" RESET, up);
        SI_BG();
        SI_FG_CYAN();
        buf_printf("%08X " RESET, regs[i].value);
        SI_BG();
    }

    /* CPU-style flags on the right (derived from register values) */
    /* Use accumulator-derived bits for flag-like display */
    uint32_t flag_seed = 0;
    for (int i = 0; i < nregs && i < 8; i++)
        flag_seed ^= regs[i].value;
    flag_seed ^= (uint32_t)di_frame();

    static const char FLAG_CHARS[] = "odIszaPc";
    int flags_col = w - 20;
    if (flags_col > 50) {
        MOVETO(row, flags_col);
        for (int f = 0; f < 8; f++) {
            int set = (flag_seed >> (f * 3)) & 1;
            if (set) {
                SI_FG_CYAN();
                buf_printf(BOLD "%c" RESET, FLAG_CHARS[f]);
            } else {
                SI_FG_DIM();
                buf_printf("%c", (char)tolower((unsigned char)FLAG_CHARS[f]));
            }
            SI_BG();
            buf_printf(" ");
        }
    }

    MOVETO(row, w);
    SI_FG_DKGREY();
    buf_printf("│" RESET);

    /* ── Row 2: segment regs + remaining regs + note ── */
    MOVETO(row + 1, 1);
    SI_BG();
    SI_FG_DKGREY();
    buf_printf("│ ");

    for (int i = 8; i < nregs && i < 16; i++) {
        char up[BV_REG_NAME];
        size_t nl = strlen(regs[i].name);
        if (nl >= sizeof(up)) nl = sizeof(up) - 1;
        for (size_t k = 0; k < nl; k++)
            up[k] = (char)toupper((unsigned char)regs[i].name[k]);
        up[nl] = '\0';

        SI_FG_WHITE();
        buf_printf(BOLD "%-4s" RESET, up);
        SI_BG();
        SI_FG_CYAN();
        buf_printf("%08X " RESET, regs[i].value);
        SI_BG();
    }

    /* Current note on the right */
    int note_col = w - 20;
    if (note_col > 50) {
        MOVETO(row + 1, note_col);
        int note = s->current_note;
        if (note > 0 && note < 128) {
            SI_FG_WHITE();
            buf_printf("NOTE=");
            SI_FG_CYAN();
            buf_printf("%-3s%d" RESET, NOTE_NAMES[note % 12], note / 12 - 1);
        } else {
            SI_FG_DIM();
            buf_printf("NOTE=---");
        }
        SI_BG();
    }

    MOVETO(row + 1, w);
    SI_FG_DKGREY();
    buf_printf("│" RESET);
}

/* ══════════════════════════════════════════════════════════════════════
   CODE WINDOW — disassembly with segment:offset, HL bar on EIP
   ══════════════════════════════════════════════════════════════════════ */

static void si_draw_code(int row, int w, int nrows, const SynthState *s)
{
    BinView *bv    = di_binview();
    int      frame = di_frame();

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

    char     lines[BV_MAX_LINES][BV_LINE_LEN];
    uint64_t addrs[BV_MAX_LINES];
    uint64_t base = binview_pc(bv);
    if (base == 0) base = binview_text_addr(bv);
    int n = binview_disasm(bv, base, nrows, addrs, lines);

    for (int ln = 0; ln < nrows; ln++) {
        MOVETO(row + ln, 1);

        /* Active instruction gets light-blue highlight bar */
        if (ln == 0) {
            SI_BG_HL();
        } else {
            SI_BG();
        }

        SI_FG_DKGREY();
        buf_printf("│");

        uint64_t addr = ln < n ? addrs[ln] : 0;
        const char *text = ln < n ? lines[ln] : "";

        /* Segment:offset address (SoftICE style: 0008:XXXXXXXX) */
        if (ln == 0) {
            FG(255, 255, 255);
        } else {
            SI_FG_CYAN();
        }
        buf_printf(" %04X:%08X  ", 0x0008, (unsigned)(addr & 0xFFFFFFFF));

        /* Instruction text */
        if (ln == 0) {
            SI_FG_WHITE();
            buf_printf(BOLD);
        } else if (ln < 4) {
            SI_FG_WHITE();
        } else {
            SI_FG_GREY();
        }

        int avail = w - 19;
        if (avail < 10) avail = 10;
        buf_printf("%-*.*s" RESET, avail, avail, text);

        /* Right border — must reset BG before drawing it on non-HL rows */
        if (ln == 0) {
            /* still on blue HL bg, draw border then reset */
            SI_FG_DKGREY();
            MOVETO(row + ln, w);
            buf_printf("│" RESET);
        } else {
            SI_FG_DKGREY();
            MOVETO(row + ln, w);
            buf_printf("│" RESET);
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════
   DATA WINDOW — hex dump with SoftICE formatting
   ══════════════════════════════════════════════════════════════════════ */

static void si_draw_data(int row, int w, int nrows, const SynthState *s)
{
    BinView *bv    = di_binview();
    size_t   fsize = di_filesize();

    enum { MAX_BPR = 16 };

    /* Calculate bytes per row to fit */
    int avail = w - 20;   /* segment:offset prefix (15) + ASCII + borders */
    int bpr = (avail - 2) / 4;
    if (bpr < 4)       bpr = 4;
    if (bpr > MAX_BPR) bpr = MAX_BPR;
    int total = nrows * bpr;

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

    uint8_t hbuf[24 * MAX_BPR];
    size_t got = binview_read(bv, base, hbuf, (size_t)total);

    for (int r = 0; r < nrows; r++) {
        MOVETO(row + r, 1);
        SI_BG();
        SI_FG_DKGREY();
        buf_printf("│");

        /* Segment:offset address */
        SI_FG_CYAN();
        buf_printf(" 0030:%08X  ",
                   (unsigned)((base + (uint64_t)(r * bpr)) & 0xFFFFFFFF));

        /* Hex bytes */
        for (int b = 0; b < bpr; b++) {
            size_t idx = (size_t)(r * bpr + b);
            if (idx < got) {
                uint8_t v = hbuf[idx];
                if (v == 0)                       SI_FG_DIM();
                else if (v == 0xFF)               SI_FG_RED();
                else if (v >= 0x20 && v < 0x7F)   SI_FG_WHITE();
                else                               SI_FG_GREY();
                buf_printf("%02X ", v);
            } else {
                SI_FG_DIM();
                buf_printf("   ");
            }

            /* SoftICE midpoint separator */
            if (b == bpr / 2 - 1) {
                SI_FG_DIM();
                buf_printf("- ");
            }
        }

        /* ASCII column */
        SI_FG_DIM();
        buf_printf(" ");
        for (int b = 0; b < bpr; b++) {
            size_t idx = (size_t)(r * bpr + b);
            if (idx < got) {
                uint8_t v = hbuf[idx];
                if (v >= 0x20 && v < 0x7F) {
                    SI_FG_GREY();
                    buf_printf("%c", (char)v);
                } else {
                    SI_FG_DIM();
                    buf_printf(".");
                }
            } else {
                buf_printf(" ");
            }
        }

        MOVETO(row + r, w);
        SI_FG_DKGREY();
        buf_printf("│" RESET);
    }
}

/* ══════════════════════════════════════════════════════════════════════
   WAVE WINDOW — oscilloscope (green on black, classic CRT look)
   ══════════════════════════════════════════════════════════════════════ */

static void si_draw_wave(int row, int w, int nrows, const SynthState *s)
{
    int frame = di_frame();
    int vw = w - 4;
    if (vw < 20) vw = 20;
    int vh = nrows;
    if (vh < 2) vh = 2;

    /* Half-block rendering: each char cell = 2 vertical pixels */
    int vh2 = vh * 2;  /* virtual rows at half-char resolution */
    float ph = s->paused ? 0.0f : (float)frame * 0.08f;

    for (int r = 0; r < vh; r++) {
        MOVETO(row + r, 1);
        SI_BG();
        SI_FG_DKGREY();
        buf_printf("\u2502 ");

        /* Two virtual y-positions for this character row */
        float y_top = 1.0f - (float)(r * 2)     / (float)(vh2 - 1);
        float y_bot = 1.0f - (float)(r * 2 + 1)  / (float)(vh2 - 1);

        for (int c = 0; c < vw; c++) {
            float t = (float)c / (float)vw;

            float wave;
            if (s->paused) {
                wave = 0.5f;
            } else {
                float f1 = 6.0f + s->ch_levels[CH_LEAD] * 14.0f;
                float f2 = 3.0f + s->ch_levels[CH_BASS] * 8.0f;
                float f3 = 9.0f + s->ch_levels[CH_ARPEGGIO] * 12.0f;
                wave = 0.5f
                    + 0.20f * sinf(t * f1 * 6.2832f + ph)
                    + 0.12f * sinf(t * f2 * 6.2832f + ph * 0.7f)
                    + 0.07f * sinf(t * f3 * 6.2832f + ph * 1.3f)
                    + s->ch_levels[CH_DRUMS] * 0.12f
                      * sinf(t * 5.0f * 6.2832f + ph * 2.0f);
            }

            float dt = fabsf(y_top - wave);
            float db = fabsf(y_bot - wave);
            float thr = 1.2f / (float)vh2;  /* adaptive thickness */
            int hit_top = dt < thr;
            int hit_bot = db < thr;

            /* Center line (dim) at y=0.5 */
            int center_top = fabsf(y_top - 0.5f) < 0.5f / (float)vh2;
            int center_bot = fabsf(y_bot - 0.5f) < 0.5f / (float)vh2;

            if (hit_top && hit_bot) {
                SI_FG_GREEN();
                buf_printf("\u2588");  /* █ */
            } else if (hit_top) {
                SI_FG_GREEN();
                buf_printf("\u2580");  /* ▀ */
            } else if (hit_bot) {
                SI_FG_GREEN();
                buf_printf("\u2584");  /* ▄ */
            } else if (center_top && !center_bot) {
                FG(0, 50, 0);
                buf_printf("\u2580");
            } else if (!center_top && center_bot) {
                FG(0, 50, 0);
                buf_printf("\u2584");
            } else if (center_top && center_bot) {
                FG(0, 50, 0);
                buf_printf("\u2500");
            } else {
                /* Glow near the wave */
                float dmin = dt < db ? dt : db;
                if (dmin < thr * 2.5f) {
                    FG(0, 60, 0);
                    buf_printf("\u2591");  /* ░ */
                } else {
                    buf_printf(" ");
                }
            }
        }

        SI_FG_DKGREY();
        buf_printf("\u2502" RESET);
    }
}

/* ══════════════════════════════════════════════════════════════════════
   CHANNEL METERS — 2 rows, 3 channels each, green→yellow→red bars
   ══════════════════════════════════════════════════════════════════════ */

static void si_draw_meters(int row, int w, const SynthState *s)
{
    int mw = (w - 8) / 3 - 10;
    if (mw < 8) mw = 8;
    if (mw > 25) mw = 25;

    for (int line = 0; line < 2; line++) {
        MOVETO(row + line, 1);
        SI_BG();
        SI_FG_DKGREY();
        buf_printf("│ ");

        for (int ch = 0; ch < 3; ch++) {
            int c = line * 3 + ch;
            float lv = s->ch_levels[c];
            if (lv > 1.0f) lv = 1.0f;
            int filled = (int)(lv * (float)mw);

            SI_FG_WHITE();
            buf_printf(BOLD "%s " RESET, CH_NAMES[c]);
            SI_BG();

            for (int i = 0; i < mw; i++) {
                if (i < filled) {
                    float t = (float)i / (float)mw;
                    if (t > 0.8f)      SI_FG_RED();
                    else if (t > 0.6f) SI_FG_YELLOW();
                    else               SI_FG_GREEN();
                    buf_printf("█");
                } else {
                    FG(40, 40, 40);
                    buf_printf("░");
                }
            }
            buf_printf("  ");
        }

        MOVETO(row + line, w);
        SI_FG_DKGREY();
        buf_printf("│" RESET);
    }
}

/* ══════════════════════════════════════════════════════════════════════
   CONSOLE WINDOW — module-load event log + command prompt
   ══════════════════════════════════════════════════════════════════════ */

static void si_draw_console(int row, int w, int nrows, const SynthState *s)
{
    int frame = di_frame();

    /* Module-load style event log lines.
     * Simulated using file info, chip, style, section, etc. */
    const char *fname = di_filename();
    size_t      fsize = di_filesize();
    BinView    *bv    = di_binview();
    const char *fmt   = binview_format(bv);

    /* Build a set of "event log" lines that scroll and rotate */
    char event_lines[12][128];
    int  num_events = 0;

    /* Static-ish events based on file info */
    if (fmt && *fmt) {
        snprintf(event_lines[num_events++], 128,
                 "MOD=%s loaded (%s)", fname, fmt);
    } else {
        snprintf(event_lines[num_events++], 128,
                 "MOD=%s loaded", fname);
    }

    if (fsize >= 1048576)
        snprintf(event_lines[num_events++], 128,
                 "  size=%.1fMB entry=%08llX",
                 (double)fsize / 1048576.0,
                 (unsigned long long)binview_entry(bv));
    else
        snprintf(event_lines[num_events++], 128,
                 "  size=%.1fKB entry=%08llX",
                 (double)fsize / 1024.0,
                 (unsigned long long)binview_entry(bv));

    snprintf(event_lines[num_events++], 128,
             "CHIP=%s attached", chip_short_name(s->chip_type));

    if (s->style_type != STYLE_NONE)
        snprintf(event_lines[num_events++], 128,
                 "STYLE=%s applied", style_short_name(s->style_type));

    snprintf(event_lines[num_events++], 128,
             "AUDIO=44100Hz 16bit mono  BPM=%.0f", (double)s->bpm);

    snprintf(event_lines[num_events++], 128,
             "SEC=[%s]  TICK=%d/%d",
             s->section_name, s->current_tick,
             s->num_events > 0 ? s->events[s->num_events - 1].tick : 0);

    /* Rotating RE quote as a "system message" */
    si_quote_timer--;
    if (si_quote_timer <= 0) {
        si_quote_idx = (int)(di_rng() % NUM_RE_QUOTES);
        si_quote_timer = QUOTE_DURATION;
    }
    const char *q = RE_QUOTES[si_quote_idx % NUM_RE_QUOTES];
    snprintf(event_lines[num_events++], 128,
             "MSG=\"%.100s\"", q);

    if (s->paused)
        snprintf(event_lines[num_events++], 128, "STATUS=BREAK (paused)");
    else if (s->finished)
        snprintf(event_lines[num_events++], 128, "STATUS=COMPLETE");
    else
        snprintf(event_lines[num_events++], 128,
                 "STATUS=RUNNING  progress=%d%%",
                 (int)(s->progress * 100.0f));

    /* Draw event log lines (scroll slowly) */
    int log_rows = nrows - 1;   /* last row is the command prompt */
    if (log_rows < 1) log_rows = 1;

    int scroll = (frame / 60) % (num_events > log_rows ? num_events - log_rows + 1 : 1);

    for (int r = 0; r < log_rows; r++) {
        MOVETO(row + r, 1);
        SI_BG();
        SI_FG_DKGREY();
        buf_printf("│ ");

        int eidx = scroll + r;
        if (eidx < num_events) {
            SI_FG_DIM();
            buf_printf("%s", event_lines[eidx]);
        }

        MOVETO(row + r, w);
        SI_FG_DKGREY();
        buf_printf("│" RESET);
    }

    /* Command prompt at the very bottom of the console */
    MOVETO(row + log_rows, 1);
    SI_BG();
    SI_FG_DKGREY();
    buf_printf("│");
    SI_FG_WHITE();
    buf_printf(BOLD ":" RESET);
    SI_BG();

    /* Show a "command" — key hints */
    SI_FG_GREY();
    buf_printf(" space");
    SI_FG_DIM();
    buf_printf("=pause ");
    SI_FG_GREY();
    buf_printf("c");
    SI_FG_DIM();
    buf_printf("=chip ");
    SI_FG_GREY();
    buf_printf("s");
    SI_FG_DIM();
    buf_printf("=style ");
    SI_FG_GREY();
    buf_printf("t");
    SI_FG_DIM();
    buf_printf("=ui ");
    SI_FG_GREY();
    buf_printf("h/l");
    SI_FG_DIM();
    buf_printf("=seek ");
    SI_FG_GREY();
    buf_printf("q");
    SI_FG_DIM();
    buf_printf("=quit");

    MOVETO(row + log_rows, w);
    SI_FG_DKGREY();
    buf_printf("│" RESET);
}

/* ══════════════════════════════════════════════════════════════════════
   GREEN STATUS BAR — PROT32 mode + file info + progress
   ══════════════════════════════════════════════════════════════════════ */

static void si_draw_status_bar(int row, int w, const SynthState *s)
{
    MOVETO(row, 1);
    SI_BG();
    SI_FG_DKGREY();
    buf_printf("│");

    SI_FG_GREEN();
    buf_printf(BOLD " PROT32" RESET);
    SI_BG();

    /* Chip / driver name */
    SI_FG_GREEN();
    buf_printf("  %s", chip_short_name(s->chip_type));

    /* File / "module" */
    SI_FG_DIM();
    buf_printf("  │  ");
    SI_FG_GREEN();
    buf_printf("%s", di_filename());

    /* Status */
    SI_FG_DIM();
    buf_printf("  │  ");
    if (s->paused) {
        SI_FG_YELLOW();
        buf_printf("(BREAK)");
    } else if (s->finished) {
        SI_FG_GREEN();
        buf_printf("(COMPLETE)");
    } else {
        SI_FG_GREEN();
        buf_printf("(DISPATCH)");
    }

    /* Progress bar on the right */
    int pct = (int)(s->progress * 100.0f);
    if (pct > 100) pct = 100;

    int pw = w - 70;
    if (pw < 8) pw = 8;
    if (pw > 35) pw = 35;
    int filled = (int)(s->progress * (float)pw);

    /* Position progress bar near right edge */
    MOVETO(row, w - pw - 8);
    SI_FG_GREEN();
    for (int i = 0; i < pw; i++) {
        if (i < filled) buf_printf("━");
        else { SI_FG_DIM(); buf_printf("─"); }
    }
    SI_FG_GREEN();
    buf_printf(" %3d%%", pct);

    MOVETO(row, w);
    SI_FG_DKGREY();
    buf_printf("│" RESET);
}

/* ══════════════════════════════════════════════════════════════════════
   MAIN DRAW — layout orchestration
   ══════════════════════════════════════════════════════════════════════ */

void theme_softice_draw(const SynthState *s)
{
    int w = di_cols();
    int h = di_rows();

    /* Fill entire screen with black */
    for (int r = 1; r <= h; r++)
        si_clear_row(r, w);

    /*
     * Layout (authentic SoftICE arrangement):
     *
     *   Row 1         Top border
     *   Row 2-3       REGS  (register window, 2 rows)
     *   Row 4         Green status bar (PROT32 + module + status)
     *   Row 5         Divider: CODE
     *   Row 6..C      CODE  (disassembly, ~35% of flex)
     *   Row C+1       Divider: DATA
     *   Row C+2..D    DATA  (hex dump, ~20% of flex)
     *   Row D+1       Divider: WAVE
     *   Row D+2..W    WAVE  (oscilloscope, ~20% of flex)
     *   Row W+1       Divider: METERS
     *   Row W+2..M    METERS (2 rows)
     *   Row M+1       Divider (console)
     *   Row M+2..N    CONSOLE (event log + command prompt, ~25% of flex)
     *   Row N+1       Bottom border
     */

    /* Fixed overhead: top(1) + regs(2) + statusbar(1) + 5 dividers + meters(2) + bottom(1) = 12 */
    int fixed_rows = 12;
    int flex = h - fixed_rows;
    if (flex < 16) flex = 16;

    /* Proportional split: CODE 35%, DATA 20%, WAVE 20%, CONSOLE 25% */
    int code_rows    = flex * 35 / 100;
    int data_rows    = flex * 20 / 100;
    int wave_rows    = flex * 20 / 100;
    int console_rows = flex - code_rows - data_rows - wave_rows;

    if (code_rows < 4)    code_rows = 4;
    if (data_rows < 3)    data_rows = 3;
    if (wave_rows < 3)    wave_rows = 3;
    if (console_rows < 3) console_rows = 3;

    int cur = 1;

    /* Top border */
    si_top_border(w);
    cur = 2;

    /* REGS */
    si_draw_regs(cur, w, s);
    cur += 2;

    /* Green status bar */
    si_draw_status_bar(cur, w, s);
    cur++;

    /* CODE divider + panel */
    si_divider(cur, w, "CODE");
    cur++;
    si_draw_code(cur, w, code_rows, s);
    cur += code_rows;

    /* DATA divider + panel */
    si_divider(cur, w, "DATA");
    cur++;
    si_draw_data(cur, w, data_rows, s);
    cur += data_rows;

    /* WAVE divider + panel */
    si_divider(cur, w, "WAVE");
    cur++;
    si_draw_wave(cur, w, wave_rows, s);
    cur += wave_rows;

    /* METERS divider + panel */
    si_divider(cur, w, "METERS");
    cur++;
    si_draw_meters(cur, w, s);
    cur += 2;

    /* CONSOLE divider + panel */
    si_divider(cur, w, "CONSOLE");
    cur++;
    si_draw_console(cur, w, console_rows, s);
    cur += console_rows;

    /* Bottom border */
    si_bottom_border(cur, w);

    buf_printf(RESET);
}
