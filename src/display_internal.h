#ifndef CHEAPBIN_DISPLAY_INTERNAL_H
#define CHEAPBIN_DISPLAY_INTERNAL_H

/*
 * Shared drawing utilities for theme implementations.
 * All themes include this header to access the output buffer,
 * terminal state, and common helpers.
 */

#include "synth.h"
#include "binview.h"

/* ── ANSI escape codes ─────────────────────────────────────────────── */

#define ESC          "\033["
#define RESET        ESC "0m"
#define BOLD         ESC "1m"
#define DIM          ESC "2m"
#define ITALIC       ESC "3m"
#define UNDERLINE    ESC "4m"
#define HIDE_CURSOR  ESC "?25l"
#define SHOW_CURSOR  ESC "?25h"
#define ALT_BUF_ON   ESC "?1049h"
#define ALT_BUF_OFF  ESC "?1049l"
#define CLEAR        ESC "2J"
#define HOME         ESC "H"
#define ERASE_LINE   ESC "2K"

/* ── Output buffer ─────────────────────────────────────────────────── */

__attribute__((format(printf, 1, 2)))
void buf_printf(const char *fmt, ...);
void buf_flush(void);

/* ── Convenience macros (all go through buf_printf) ────────────────── */

#define MOVETO(r,c)  buf_printf(ESC "%d;%dH", (r), (c))
#define FG(r,g,b)    buf_printf(ESC "38;2;%d;%d;%dm", (r), (g), (b))
#define BG(r,g,b)    buf_printf(ESC "48;2;%d;%d;%dm", (r), (g), (b))
#define BG_RESET     buf_printf(ESC "49m")

/* ── Shared state accessors ────────────────────────────────────────── */

int          di_cols(void);
int          di_rows(void);
int          di_frame(void);
const char  *di_filename(void);
size_t       di_filesize(void);
BinView     *di_binview(void);
uint32_t     di_rng(void);

/* ── Utility ───────────────────────────────────────────────────────── */

/* Display width of a UTF-8 string (columns, not bytes). */
int display_width(const char *str);

/* ── Common data ───────────────────────────────────────────────────── */

extern const char *NOTE_NAMES[12];
extern const char *CH_NAMES[6];

#endif
