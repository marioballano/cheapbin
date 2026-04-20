#include "display.h"
#include "display_internal.h"
#include "theme.h"
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <stdarg.h>

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

void buf_flush(void)
{
    if (s_pos > 0) {
        (void)!write(STDOUT_FILENO, s_buf, (size_t)s_pos);
        s_pos = 0;
    }
}

__attribute__((format(printf, 1, 2)))
void buf_printf(const char *fmt, ...)
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

/* ── Shared state ──────────────────────────────────────────────────── */

static char      s_filename[256];
static size_t    s_filesize;
static int       s_frame;
static BinView  *s_bv;
static ThemeType s_theme = THEME_DEFAULT;

/* ── PRNG ──────────────────────────────────────────────────────────── */

static uint32_t s_rng = 0x12345678;

/* ── Accessor functions for theme implementations ──────────────────── */

int          di_cols(void)     { return s_cols; }
int          di_rows(void)     { return s_rows; }
int          di_frame(void)    { return s_frame; }
const char  *di_filename(void) { return s_filename; }
size_t       di_filesize(void) { return s_filesize; }
BinView     *di_binview(void)  { return s_bv; }

uint32_t di_rng(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* ── Utility ───────────────────────────────────────────────────────── */

int display_width(const char *str)
{
    int w = 0;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++)
        if ((*p & 0xC0) != 0x80) w++;
    return w;
}

/* ── Common constants ──────────────────────────────────────────────── */

const char *NOTE_NAMES[12] = {
    "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

const char *CH_NAMES[6] = {
    "LEAD  ", "HARMON", "BASS  ", "ARPEG ", "PAD   ", "DRUMS "
};

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
    s_theme       = THEME_DEFAULT;

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
    buf_printf(SHOW_CURSOR ALT_BUF_OFF RESET);
    buf_flush();
}

int display_poll_key(void)
{
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return 0;
    if (c != 0x1B) return c;

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

/* ── Theme cycling ─────────────────────────────────────────────────── */

void display_cycle_theme(void)
{
    s_theme = theme_next(s_theme);
}

/* ── Main display update — dispatch to current theme ───────────────── */

void display_update(const SynthState *s)
{
    get_term_size();
    s_frame++;
    s_pos = 0;

    buf_printf(HOME CLEAR);

    switch (s_theme) {
    case THEME_SOFTICE: theme_softice_draw(s); break;
    case THEME_TD32:    theme_td32_draw(s);    break;
    default:            theme_default_draw(s);  break;
    }

    buf_flush();
}
