/*
 * theme_td32.c — Turbo Debugger 32 (TD32) UI
 *
 * Faithful recreation of the Borland Turbo Debugger 32-bit interface:
 *   - Deep teal/cyan background with bright cyan text
 *   - Maroon/red menu bar with light grey text
 *   - CPU 32 (x86-32) disassembly panel with cs:XXXXXXXX addresses
 *   - 32-bit register panel (eax, ebx, ecx, edx, esi, edi, ebp, esp)
 *   - Data segment dump (ds:XXXXXXXX) with ASCII interpretation
 *   - Stack panel (ss:esp) with highlighted top-of-stack
 *   - Function key bar at bottom (F1=Help, F2=Bkpt, etc.)
 *   - Navy blue selection/highlight bar
 *
 * Palette (authentic Borland TD):
 *   Background:   dark teal  (#005555 / 0,68,68)
 *   Text:         bright cyan (#55FFFF / 85,255,255)
 *   Menu bar:     maroon      (#AA0000 / 170,0,0)
 *   Menu text:    light grey  (#AAAAAA / 170,170,170)
 *   Highlight:    navy blue   (#000080 / 0,0,128)
 *   HL text:      bright white
 *   Borders:      dark cyan   (#008080 / 0,128,128)
 *   Fkey labels:  red on black
 *   Fkey text:    cyan
 *   Addresses:    yellow      (#FFFF55 / 255,255,85)
 */

#include "display_internal.h"
#include "theme.h"
#include "chipemu.h"
#include "style.h"
#define USE_RE_QUOTES
#include "re_data.h"
#include "binview.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ── Authentic TD32 BIOS 16-color VGA palette ─────────────────────── */

/* Backgrounds */
#define TD_BG()        BG(0, 170, 170)       /* #00AAAA Borland teal     */
#define TD_BG_HL()     BG(0, 0, 170)         /* #0000AA dark blue select */
#define TD_BG_MENU()   BG(170, 170, 170)     /* #AAAAAA light grey menu  */
#define TD_BG_FKEY()   BG(170, 170, 170)     /* #AAAAAA light grey fkeys */
#define TD_BG_TITLE()  BG(0, 170, 170)       /* #00AAAA same as desktop  */
#define TD_BG_BLACK()  BG(0, 0, 0)           /* #000000 shadow/terminal  */

/* Foreground — text & values */
#define TD_FG_TEXT()   FG(255, 255, 255)     /* #FFFFFF white on cyan    */
#define TD_FG_ADDR()   FG(255, 255, 255)     /* #FFFFFF white addresses  */
#define TD_FG_WHITE()  FG(255, 255, 255)     /* #FFFFFF bright white     */
#define TD_FG_RED()    FG(255, 85, 85)       /* #FF5555 changed values   */
#define TD_FG_GREEN()  FG(85, 255, 85)       /* #55FF55 READY indicator  */
#define TD_FG_YELLOW() FG(255, 255, 85)      /* #FFFF55 hex immediates   */
#define TD_FG_CYAN()   FG(0, 255, 255)       /* #00FFFF bright cyan      */
#define TD_FG_BLACK()  FG(0, 0, 0)           /* #000000 text on grey bg  */

/* Foreground — UI chrome */
#define TD_FG_MENU()   FG(170, 0, 0)         /* #AA0000 menu item red    */
#define TD_FG_MENUH()  FG(0, 0, 0)           /* #000000 hotkey black     */
#define TD_FG_BORDER() FG(0, 255, 255)       /* #00FFFF cyan borders     */
#define TD_FG_DIM()    FG(128, 128, 128)     /* #808080 shadow/dim       */
#define TD_FG_GREY()   FG(170, 170, 170)     /* #AAAAAA light grey       */
#define TD_FG_DKGREY() FG(85, 85, 85)        /* #555555 dark grey        */

/* ── Architecture detection from register names ───────────────────── */

typedef enum { TD_ARCH_X86_32, TD_ARCH_X86_64, TD_ARCH_ARM32, TD_ARCH_ARM64 } TdArch;

static TdArch td_detect_arch(const BvReg *regs, int nregs)
{
    for (int i = 0; i < nregs; i++) {
        if (strcmp(regs[i].name, "rax") == 0) return TD_ARCH_X86_64;
        if (strcmp(regs[i].name, "eax") == 0) return TD_ARCH_X86_32;
        if (strcmp(regs[i].name, "x0") == 0)  return TD_ARCH_ARM64;
        if (strcmp(regs[i].name, "r0") == 0)  return TD_ARCH_ARM32;
    }
    return TD_ARCH_X86_32;  /* fallback */
}

static const char *td_arch_label(TdArch arch)
{
    switch (arch) {
        case TD_ARCH_X86_64: return "CPU 64 (x86-64)";
        case TD_ARCH_X86_32: return "CPU 32 (x86-32)";
        case TD_ARCH_ARM64:  return "CPU 64 (AArch64)";
        case TD_ARCH_ARM32:  return "CPU 32 (ARM)";
    }
    return "CPU";
}

static int td_find_sp(const BvReg *regs, int nregs)
{
    for (int i = 0; i < nregs; i++) {
        if (strcmp(regs[i].name, "rsp") == 0 ||
            strcmp(regs[i].name, "esp") == 0 ||
            strcmp(regs[i].name, "sp")  == 0)
            return i;
    }
    return nregs > 7 ? 7 : 0;  /* fallback */
}

/* ── Local state ───────────────────────────────────────────────────── */

#define QUOTE_DURATION 90
static int td_quote_idx   = 0;
static int td_quote_timer = QUOTE_DURATION;

/* ── Fill a row with teal background ───────────────────────────────── */

static void td_clear_row(int row, int w)
{
    MOVETO(row, 1);
    TD_BG();
    for (int i = 0; i < w; i++) buf_printf(" ");
}

/* ══════════════════════════════════════════════════════════════════════
   MENU BAR — Maroon bar with F-ile E-dit V-iew R-un B-reakpoints ...
   ══════════════════════════════════════════════════════════════════════ */

static void td_draw_menu(int row, int w)
{
    MOVETO(row, 1);
    TD_BG_MENU();
    TD_FG_MENU();

    buf_printf(" ");

    /* Menu items with hotkey highlighting (first letter) */
    static const char *items[] = {
        "File", "Edit", "View", "Run", "Breakpoints", "Data", "Options", "Window", "Help"
    };
    int n = (int)(sizeof(items) / sizeof(items[0]));

    for (int i = 0; i < n; i++) {
        buf_printf(" ");
        TD_FG_MENUH();
        buf_printf("%c", items[i][0]);
        TD_FG_MENU();
        buf_printf("%s", items[i] + 1);
        buf_printf(" ");
    }

    /* Fill rest of menu bar */
    int used = 1;
    for (int i = 0; i < n; i++) used += 2 + (int)strlen(items[i]);

    /* READY indicator on the right */
    const char *ready_str = "READY";
    int ready_col = w - (int)strlen(ready_str) - 1;
    for (int i = used; i < ready_col; i++) buf_printf(" ");
    TD_FG_GREEN();
    buf_printf("%s", ready_str);
    TD_BG_MENU();
    buf_printf(" ");

    buf_printf(RESET);
}

/* ══════════════════════════════════════════════════════════════════════
   PANEL FRAME — Double-line bordered panel with title
   ══════════════════════════════════════════════════════════════════════ */

/* Top border of a panel: ╔══ Title ══════════╗ */
static void td_panel_top(int row, int col, int pw, const char *title)
{
    MOVETO(row, col);
    TD_BG();
    TD_FG_BORDER();
    buf_printf("╔");

    if (title) {
        buf_printf("═");
        TD_BG_TITLE();
        TD_FG_WHITE();
        buf_printf(BOLD " %s " RESET, title);
        TD_BG();
        TD_FG_BORDER();
        int used = 3 + (int)strlen(title);
        for (int i = used; i < pw - 1; i++) buf_printf("═");
    } else {
        for (int i = 1; i < pw - 1; i++) buf_printf("═");
    }
    buf_printf("╗" RESET);
}

/* Bottom border: ╚══════════════════╝ */
static void td_panel_bot(int row, int col, int pw)
{
    MOVETO(row, col);
    TD_BG();
    TD_FG_BORDER();
    buf_printf("╚");
    for (int i = 1; i < pw - 1; i++) buf_printf("═");
    buf_printf("╝" RESET);
}

/* Left+right borders for a content row */
static void td_panel_lborder(int row, int col)
{
    MOVETO(row, col);
    TD_BG();
    TD_FG_BORDER();
    buf_printf("║");
}

static void td_panel_rborder(int row, int col)
{
    MOVETO(row, col);
    TD_BG();
    TD_FG_BORDER();
    buf_printf("║" RESET);
}

/* Horizontal separator inside panel: ╟──────╢ */
static void td_panel_hsep(int row, int col, int pw)
{
    MOVETO(row, col);
    TD_BG();
    TD_FG_BORDER();
    buf_printf("╟");
    for (int i = 1; i < pw - 1; i++) buf_printf("─");
    buf_printf("╢" RESET);
}

/* ══════════════════════════════════════════════════════════════════════
   REGISTER PANEL — eax, ebx, ecx, edx, esi, edi, ebp, esp + flags
   Displayed on the right side of the screen
   ══════════════════════════════════════════════════════════════════════ */

static void td_draw_regs(int row, int col, int pw, int nrows,
                          const SynthState *s)
{
    BinView *bv = di_binview();
    BvReg regs[BV_MAX_REGS];
    int nregs = binview_regs(bv, regs, BV_MAX_REGS);

    /* Show up to 8 GP regs from whatever arch r2 detected */
    int show_regs = nregs < 8 ? nregs : 8;

    for (int r = 0; r < nrows; r++) {
        td_panel_lborder(row + r, col);
        TD_BG();

        if (r < show_regs) {
            /* Register line — use actual register name from r2 */
            int ri = r;
            char rname[BV_REG_NAME];
            size_t nl = strlen(regs[ri].name);
            if (nl >= sizeof(rname)) nl = sizeof(rname) - 1;
            for (size_t k = 0; k < nl; k++)
                rname[k] = (char)toupper((unsigned char)regs[ri].name[k]);
            rname[nl] = '\0';
            uint32_t val = regs[ri].value;

            /* Highlight eip (first line) with dark blue bar */
            if (r == 0) {
                TD_BG_HL();
                TD_FG_WHITE();
            } else {
                TD_FG_TEXT();
            }

            buf_printf(" ");
            buf_printf(BOLD "%-4s" RESET, rname);
            if (r == 0) TD_BG_HL(); else TD_BG();
            if (r == 0) TD_FG_WHITE(); else TD_FG_YELLOW();
            buf_printf("%08X", val);

            /* Pad remaining */
            int used = 1 + 4 + 8;
            if (r == 0) TD_BG_HL(); else TD_BG();
            for (int i = used; i < pw - 2; i++) buf_printf(" ");
            buf_printf(RESET);
        } else if (r == show_regs) {
            /* Separator */
            TD_FG_BORDER();
            for (int i = 1; i < pw - 1; i++) buf_printf("─");
        } else if (r == show_regs + 1) {
            /* Flags line */
            uint32_t flag_seed = 0;
            for (int i = 0; i < nregs && i < 8; i++)
                flag_seed ^= regs[i].value;
            flag_seed ^= (uint32_t)di_frame();

            static const char *FLAG_NAMES[] = {
                "c", "z", "s", "o", "p", "a", "d", "i"
            };
            TD_FG_TEXT();
            buf_printf(" ");
            for (int f = 0; f < 8; f++) {
                int set = (flag_seed >> (f * 4)) & 1;
                if (set) {
                    TD_FG_WHITE();
                    buf_printf(BOLD "%s" RESET, FLAG_NAMES[f]);
                } else {
                    TD_FG_DKGREY();
                    buf_printf("%s", FLAG_NAMES[f]);
                }
                TD_BG();
                buf_printf(" ");
            }
            int used = 1 + 8 * 2;
            for (int i = used; i < pw - 2; i++) buf_printf(" ");
        } else if (r == show_regs + 2) {
            /* Separator */
            TD_FG_BORDER();
            for (int i = 1; i < pw - 1; i++) buf_printf("─");
        } else if (r == show_regs + 3) {
            /* Current note display */
            int note = s->current_note;
            TD_FG_DIM();
            buf_printf(" ");
            if (note > 0 && note < 128) {
                TD_FG_GREEN();
                buf_printf(BOLD "♪" RESET);
                TD_BG();
                buf_printf(" ");
                TD_FG_TEXT();
                buf_printf("%-3s%d", NOTE_NAMES[note % 12], note / 12 - 1);
            } else {
                buf_printf("  ---");
            }
            int used = 6;
            for (int i = used; i < pw - 2; i++) buf_printf(" ");
        } else if (r == show_regs + 4) {
            /* BPM */
            TD_FG_DIM();
            buf_printf(" bpm ");
            TD_FG_TEXT();
            buf_printf("%.0f", (double)s->bpm);
            int used = 5 + 3;
            for (int i = used; i < pw - 2; i++) buf_printf(" ");
        } else if (r == show_regs + 5) {
            /* Section */
            TD_FG_DIM();
            buf_printf(" sec ");
            TD_FG_TEXT();
            buf_printf("%.8s", s->section_name);
            int used = 5 + 8;
            for (int i = used; i < pw - 2; i++) buf_printf(" ");
        } else {
            /* Empty filler */
            for (int i = 1; i < pw - 1; i++) buf_printf(" ");
        }

        td_panel_rborder(row + r, col + pw - 1);
    }
}

/* ══════════════════════════════════════════════════════════════════════
   CODE PANEL — Disassembly with cs:XXXXXXXX addresses
   ══════════════════════════════════════════════════════════════════════ */

static void td_draw_code(int row, int col, int pw, int nrows,
                          const SynthState *s)
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
        td_panel_lborder(row + ln, col);

        /* Active instruction gets dark blue highlight bar */
        if (ln == 0) {
            TD_BG_HL();
        } else {
            TD_BG();
        }

        uint64_t addr = ln < n ? addrs[ln] : 0;
        const char *text = ln < n ? lines[ln] : "";

        /* Instruction pointer arrow */
        if (ln == 0) {
            TD_FG_WHITE();
            buf_printf(BOLD "►" RESET);
            TD_BG_HL();
        } else {
            buf_printf(" ");
        }

        /* Address */
        if (ln == 0) {
            TD_FG_WHITE();
        } else {
            TD_FG_YELLOW();
        }
        buf_printf("%08X ", (unsigned)(addr & 0xFFFFFFFF));

        /* Instruction text */
        if (ln == 0) {
            TD_FG_WHITE();
            buf_printf(BOLD);
        } else {
            TD_FG_TEXT();
        }

        int avail = pw - 12;  /* ║ + ► + XXXXXXXX + space + ║ */
        if (avail < 10) avail = 10;
        buf_printf("%-*.*s" RESET, avail, avail, text);

        if (ln == 0) {
            /* reset bg before right border */
        }
        td_panel_rborder(row + ln, col + pw - 1);
    }
}

/* ══════════════════════════════════════════════════════════════════════
   DATA PANEL — Hex dump ds:XXXXXXXX with ASCII
   ══════════════════════════════════════════════════════════════════════ */

static void td_draw_data(int row, int col, int pw, int nrows,
                          const SynthState *s)
{
    BinView *bv    = di_binview();
    size_t   fsize = di_filesize();

    enum { MAX_BPR = 16 };

    /* Figure out bytes per row */
    int avail = pw - 13;  /* borders + addr + spaces */
    int bpr = avail / 4;
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
        td_panel_lborder(row + r, col);
        TD_BG();

        /* Address */
        TD_FG_YELLOW();
        buf_printf("%08X ",
                   (unsigned)((base + (uint64_t)(r * bpr)) & 0xFFFFFFFF));

        /* Hex bytes */
        for (int b = 0; b < bpr; b++) {
            size_t idx = (size_t)(r * bpr + b);
            if (idx < got) {
                uint8_t v = hbuf[idx];
                if (v == 0)                       TD_FG_DKGREY();
                else if (v >= 0x20 && v < 0x7F)   TD_FG_YELLOW();
                else                               TD_FG_DIM();
                buf_printf("%02X ", v);
            } else {
                TD_FG_DKGREY();
                buf_printf("   ");
            }

            if (b == bpr / 2 - 1) {
                TD_FG_DIM();
                buf_printf("- ");
            }
        }

        /* ASCII */
        TD_FG_DIM();
        buf_printf(" ");
        for (int b = 0; b < bpr; b++) {
            size_t idx = (size_t)(r * bpr + b);
            if (idx < got) {
                uint8_t v = hbuf[idx];
                if (v >= 0x20 && v < 0x7F) {
                    TD_FG_TEXT();
                    buf_printf("%c", (char)v);
                } else {
                    TD_FG_DKGREY();
                    buf_printf(".");
                }
            } else {
                buf_printf(" ");
            }
        }

        td_panel_rborder(row + r, col + pw - 1);
    }
}

/* ══════════════════════════════════════════════════════════════════════
   STACK PANEL — SP-based with values, red highlight on top
   ══════════════════════════════════════════════════════════════════════ */

static void td_draw_stack(int row, int col, int pw, int nrows,
                           const SynthState *s)
{
    BinView *bv = di_binview();
    BvReg regs[BV_MAX_REGS];
    int nregs = binview_regs(bv, regs, BV_MAX_REGS);

    /* Find the stack pointer register dynamically */
    int sp_idx = td_find_sp(regs, nregs);
    uint32_t sp_val = (sp_idx < nregs) ? regs[sp_idx].value : 0xFFFEu;

    for (int r = 0; r < nrows; r++) {
        td_panel_lborder(row + r, col);
        TD_BG();

        uint32_t sp_addr = sp_val + (uint32_t)(r * 4);
        /* Synthesize a stack value from the binary data */
        uint32_t val = 0;
        uint8_t vbytes[4];
        size_t got = binview_read(bv, (uint64_t)sp_addr, vbytes, 4);
        if (got >= 4) {
            val = (uint32_t)vbytes[0]
                | ((uint32_t)vbytes[1] << 8)
                | ((uint32_t)vbytes[2] << 16)
                | ((uint32_t)vbytes[3] << 24);
        } else {
            /* Generate a pseudo-value */
            val = sp_addr ^ (uint32_t)di_frame() ^ 0xDEAD0000u;
        }

        /* Address */
        TD_FG_WHITE();
        buf_printf("%08X ", sp_addr);

        /* Value — top of stack gets red highlight */
        if (r == 0) {
            TD_FG_RED();
            buf_printf(BOLD "%08X" RESET, val);
        } else {
            TD_FG_YELLOW();
            buf_printf("%08X", val);
        }
        TD_BG();

        /* Pad */
        int used = 9 + 8;
        for (int i = used; i < pw - 2; i++) buf_printf(" ");

        td_panel_rborder(row + r, col + pw - 1);
    }
}

/* ══════════════════════════════════════════════════════════════════════
   WAVE PANEL — Oscilloscope waveform (cyan on teal)
   ══════════════════════════════════════════════════════════════════════ */

static void td_draw_wave(int row, int col, int pw, int nrows,
                          const SynthState *s)
{
    int frame = di_frame();
    int vw = pw - 2;  /* inside borders */
    if (vw < 10) vw = 10;

    /* Half-block rendering: each char cell = 2 vertical pixels */
    int vh2 = nrows * 2;  /* virtual rows at half-char resolution */
    float ph = s->paused ? 0.0f : (float)frame * 0.08f;

    for (int r = 0; r < nrows; r++) {
        td_panel_lborder(row + r, col);
        TD_BG();

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
            float thr = 1.2f / (float)vh2;
            int hit_top = dt < thr;
            int hit_bot = db < thr;

            /* Center line (dim) at y=0.5 */
            int center_top = fabsf(y_top - 0.5f) < 0.5f / (float)vh2;
            int center_bot = fabsf(y_bot - 0.5f) < 0.5f / (float)vh2;

            if (hit_top && hit_bot) {
                TD_FG_CYAN();
                buf_printf("\u2588");  /* █ */
            } else if (hit_top) {
                TD_FG_CYAN();
                buf_printf("\u2580");  /* ▀ */
            } else if (hit_bot) {
                TD_FG_CYAN();
                buf_printf("\u2584");  /* ▄ */
            } else if (center_top && !center_bot) {
                FG(0, 60, 60);
                buf_printf("\u2580");
            } else if (!center_top && center_bot) {
                FG(0, 60, 60);
                buf_printf("\u2584");
            } else if (center_top && center_bot) {
                FG(0, 60, 60);
                buf_printf("\u2500");
            } else {
                float dmin = dt < db ? dt : db;
                if (dmin < thr * 2.5f) {
                    FG(0, 80, 80);
                    buf_printf("\u2591");  /* ░ */
                } else {
                    buf_printf(" ");
                }
            }
        }

        td_panel_rborder(row + r, col + pw - 1);
    }
}

/* ══════════════════════════════════════════════════════════════════════
   CHANNEL METERS — 2 rows, bar graph style
   ══════════════════════════════════════════════════════════════════════ */

static void td_draw_meters(int row, int col, int pw, const SynthState *s)
{
    int mw = (pw - 6) / 3 - 8;
    if (mw < 6) mw = 6;
    if (mw > 20) mw = 20;

    for (int line = 0; line < 2; line++) {
        td_panel_lborder(row + line, col);
        TD_BG();

        for (int ch = 0; ch < 3; ch++) {
            int c = line * 3 + ch;
            float lv = s->ch_levels[c];
            if (lv > 1.0f) lv = 1.0f;
            int filled = (int)(lv * (float)mw);

            TD_FG_WHITE();
            buf_printf(BOLD "%s " RESET, CH_NAMES[c]);
            TD_BG();

            for (int i = 0; i < mw; i++) {
                if (i < filled) {
                    float t = (float)i / (float)mw;
                    if (t > 0.8f)      TD_FG_RED();
                    else if (t > 0.6f) TD_FG_YELLOW();
                    else               TD_FG_GREEN();
                    buf_printf("█");
                } else {
                    FG(0, 100, 100);
                    buf_printf("░");
                }
            }
            buf_printf(" ");
        }

        td_panel_rborder(row + line, col + pw - 1);
    }
}

/* ══════════════════════════════════════════════════════════════════════
   STATUS LINE — file info, chip, progress, play state
   ══════════════════════════════════════════════════════════════════════ */

static void td_draw_status(int row, int w, const SynthState *s)
{
    MOVETO(row, 1);
    TD_BG();
    TD_FG_BORDER();
    buf_printf("║");
    TD_BG();

    /* Chip name */
    TD_FG_TEXT();
    buf_printf(" %s", chip_short_name(s->chip_type));

    /* Style */
    if (s->style_type != STYLE_NONE) {
        TD_FG_DIM();
        buf_printf("·");
        TD_FG_TEXT();
        buf_printf("%s", style_short_name(s->style_type));
    }

    /* File */
    TD_FG_DIM();
    buf_printf(" │ ");
    TD_FG_TEXT();
    buf_printf("%s", di_filename());

    /* Size */
    size_t fsize = di_filesize();
    TD_FG_DIM();
    buf_printf(" (");
    TD_FG_TEXT();
    if (fsize >= 1048576)
        buf_printf("%.1fMB", (double)fsize / 1048576.0);
    else if (fsize >= 1024)
        buf_printf("%.1fKB", (double)fsize / 1024.0);
    else
        buf_printf("%zuB", fsize);
    TD_FG_DIM();
    buf_printf(")");

    /* Format */
    BinView *bvst = di_binview();
    const char *fmt = binview_format(bvst);
    if (fmt && *fmt) {
        TD_FG_DIM();
        buf_printf(" [");
        TD_FG_CYAN();
        buf_printf("%s", fmt);
        TD_FG_DIM();
        buf_printf("]");
    }

    /* Status */
    TD_FG_DIM();
    buf_printf(" │ ");
    if (s->paused) {
        TD_FG_YELLOW();
        buf_printf(BOLD "BREAK" RESET);
    } else if (s->finished) {
        TD_FG_GREEN();
        buf_printf(BOLD "DONE" RESET);
    } else {
        TD_FG_GREEN();
        buf_printf(BOLD "▶ RUN" RESET);
    }
    TD_BG();

    /* Progress */
    int pct = (int)(s->progress * 100.0f);
    if (pct > 100) pct = 100;

    int pw = w - 65;
    if (pw < 6) pw = 6;
    if (pw > 30) pw = 30;
    int filled = (int)(s->progress * (float)pw);

    MOVETO(row, w - pw - 8);
    TD_BG();
    TD_FG_GREEN();
    for (int i = 0; i < pw; i++) {
        if (i < filled) buf_printf("━");
        else { TD_FG_DIM(); buf_printf("─"); }
    }
    TD_FG_TEXT();
    buf_printf(" %3d%%", pct);

    MOVETO(row, w);
    TD_FG_BORDER();
    buf_printf("║" RESET);
}

/* ══════════════════════════════════════════════════════════════════════
   LOG PANEL — Rotating quote / event message
   ══════════════════════════════════════════════════════════════════════ */

static void td_draw_log(int row, int col, int pw, const SynthState *s)
{
    td_panel_lborder(row, col);
    TD_BG();

    /* Rotating RE quote */
    td_quote_timer--;
    if (td_quote_timer <= 0) {
        td_quote_idx = (int)(di_rng() % NUM_RE_QUOTES);
        td_quote_timer = QUOTE_DURATION;
    }

    const char *q = RE_QUOTES[td_quote_idx % NUM_RE_QUOTES];
    int avail = pw - 4;
    TD_FG_DIM();
    buf_printf(" ");

    if ((int)strlen(q) > avail)
        buf_printf("%.*s...", avail - 3, q);
    else
        buf_printf("%s", q);

    /* Pad */
    int used = 1 + ((int)strlen(q) > avail ? avail : (int)strlen(q));
    for (int i = used; i < pw - 2; i++) buf_printf(" ");

    td_panel_rborder(row, col + pw - 1);
}

/* ══════════════════════════════════════════════════════════════════════
   FUNCTION KEY BAR — F1=Help F2=Bkpt F3=Mod F4=Here F5=Zoom ...
   ══════════════════════════════════════════════════════════════════════ */

static void td_draw_fkeys(int row, int w)
{
    MOVETO(row, 1);
    TD_BG_FKEY();

    static const struct { const char *key; const char *label; } fkeys[] = {
        {"F1", "Help"},    {"F2", "Bkpt"},   {"F3", "Mod"},
        {"F4", "Here"},    {"F5", "Zoom"},    {"F6", "Next"},
        {"F7", "Trace"},   {"F8", "Step"},    {"F9", "Run"},
        {"F10","Menu"},
    };
    int nf = (int)(sizeof(fkeys) / sizeof(fkeys[0]));

    int slot = w / nf;
    if (slot < 8) slot = 8;

    for (int i = 0; i < nf; i++) {
        /* Position evenly */
        MOVETO(row, 1 + i * slot);
        TD_BG_FKEY();

        TD_FG_DKGREY();
        buf_printf("%s", fkeys[i].key);
        buf_printf(" ");
        TD_FG_RED();
        buf_printf("%s", fkeys[i].label);

        /* Pad within slot */
        int used = (int)strlen(fkeys[i].key) + 1 + (int)strlen(fkeys[i].label);
        for (int j = used; j < slot; j++) buf_printf(" ");
    }

    /* Fill rest */
    int used = nf * slot;
    for (int i = used; i < w; i++) buf_printf(" ");

    buf_printf(RESET);
}

/* ══════════════════════════════════════════════════════════════════════
   MAIN DRAW — TD32 full-screen layout
   ══════════════════════════════════════════════════════════════════════ */

void theme_td32_draw(const SynthState *s)
{
    int w = di_cols();
    int h = di_rows();

    /* Fill entire screen with teal background */
    for (int r = 1; r <= h; r++)
        td_clear_row(r, w);

    /*
     * TD32 Layout:
     *
     *   Row 1           Menu bar (maroon)
     *   Row 2           CPU 32 panel top ══════════════════════╗ Registers top ═╗
     *   Row 3..C+2      CODE (left, ~40%)                     │ REGS (right)   │
     *   Row C+3         Code/Data separator ──────             │                │
     *   Row C+4..D+3    DATA (left, ~20%)                     │                │
     *   Row D+4         Data/Wave separator ──────             │                │
     *   Row D+5..W+4    WAVE (left, ~15%)                     │ (regs+flags+   │
     *   Row W+5         Wave/Stack separator ─────             │  note+section) │
     *   Row W+6..S+5    STACK (left bottom)                   │                │
     *   Row S+6         Left panels bottom ═══════╝           │                │
     *   Row S+7 (=R+?)  Reg panel bottom                      ╝
     *   Row S+8         Meters top ═══════════════════════════════════════╗
     *   Row S+9..S+10   METERS (2 rows)
     *   Row S+11        Status/Log
     *   Row S+12        Meters bottom ════════════════════════════════════╝
     *   Row h-1         Status line
     *   Row h           F-key bar (black)
     *
     * Simplified: we use two columns: left panels (75%) and right reg panel (25%).
     * Bottom has meters+status spanning full width, then fkey bar.
     */

    /* Column split */
    int reg_pw = 20;  /* register panel width */
    if (w > 120) reg_pw = 22;
    if (w < 80) reg_pw = 18;
    int left_pw = w - reg_pw;  /* left panels width */

    /* Fixed rows: menu(1) + fkeys(1) + panel_tops(1) + panel_bots(1) +
     * 3 internal seps + meters_frame(2+2) + status(1) + log(1) = ~13 */
    int fixed = 13;
    int flex = h - fixed;
    if (flex < 12) flex = 12;

    /* Proportional: CODE 40%, DATA 22%, WAVE 18%, STACK 20% */
    int code_rows  = flex * 40 / 100;
    int data_rows  = flex * 22 / 100;
    int wave_rows  = flex * 18 / 100;
    int stack_rows = flex - code_rows - data_rows - wave_rows;

    if (code_rows < 4)  code_rows = 4;
    if (data_rows < 3)  data_rows = 3;
    if (wave_rows < 2)  wave_rows = 2;
    if (stack_rows < 2) stack_rows = 2;

    /* Total rows for left panels (between top and bottom borders) */
    int left_content = code_rows + 1 + data_rows + 1 + wave_rows + 1 + stack_rows;
    /* Register panel content rows (same height as left content) */
    int reg_content = left_content;

    int cur = 1;

    /* ── Row 1: Menu bar ── */
    td_draw_menu(cur, w);
    cur++;

    /* ── Row 2: Panel top borders ── */
    /* Detect architecture from register names for dynamic title */
    BvReg _arch_regs[BV_MAX_REGS];
    int _arch_nregs = binview_regs(di_binview(), _arch_regs, BV_MAX_REGS);
    TdArch arch = td_detect_arch(_arch_regs, _arch_nregs);
    td_panel_top(cur, 1, left_pw, td_arch_label(arch));
    td_panel_top(cur, left_pw, reg_pw + 1, "Registers");
    cur++;

    /* ── Left: CODE panel ── */
    td_draw_code(cur, 1, left_pw, code_rows, s);

    /* ── Right: Registers (fills the full height of left panels) ── */
    td_draw_regs(cur, left_pw, reg_pw + 1, reg_content, s);

    cur += code_rows;

    /* ── Left: separator CODE→DATA ── */
    td_panel_hsep(cur, 1, left_pw);
    cur++;

    /* ── Left: DATA panel ── */
    td_draw_data(cur, 1, left_pw, data_rows, s);
    cur += data_rows;

    /* ── Left: separator DATA→WAVE ── */
    td_panel_hsep(cur, 1, left_pw);
    cur++;

    /* ── Left: WAVE panel ── */
    td_draw_wave(cur, 1, left_pw, wave_rows, s);
    cur += wave_rows;

    /* ── Left: separator WAVE→STACK ── */
    td_panel_hsep(cur, 1, left_pw);
    cur++;

    /* ── Left: STACK panel ── */
    td_draw_stack(cur, 1, left_pw, stack_rows, s);
    cur += stack_rows;

    /* ── Bottom borders for both panels ── */
    td_panel_bot(cur, 1, left_pw);
    td_panel_bot(cur, left_pw, reg_pw + 1);
    cur++;

    /* ── Full-width meters panel ── */
    td_panel_top(cur, 1, w, "Channels");
    cur++;
    td_draw_meters(cur, 1, w, s);
    cur += 2;

    /* ── Status / info line ── */
    td_draw_status(cur, w, s);
    cur++;

    /* ── Log line (rotating quote) ── */
    td_draw_log(cur, 1, w, s);
    cur++;

    td_panel_bot(cur, 1, w);
    cur++;

    /* ── Fkey bar at very bottom ── */
    if (cur <= h) {
        td_draw_fkeys(h, w);
    }

    buf_printf(RESET);
}
