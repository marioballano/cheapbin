#include "display.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdbool.h>

#define ESC          "\033["
#define RESET        ESC "0m"
#define BOLD         ESC "1m"
#define DIM          ESC "2m"
#define FG_RED       ESC "91m"
#define FG_GREEN     ESC "92m"
#define FG_YELLOW    ESC "93m"
#define FG_BLUE      ESC "94m"
#define FG_MAGENTA   ESC "95m"
#define FG_CYAN      ESC "96m"
#define FG_WHITE     ESC "97m"
#define FG_GRAY      ESC "90m"
#define HIDE_CURSOR  ESC "?25l"
#define SHOW_CURSOR  ESC "?25h"
#define ALT_BUF_ON   ESC "?1049h"
#define ALT_BUF_OFF  ESC "?1049l"
#define CLEAR        ESC "2J"
#define HOME         ESC "H"

static void move_to(int row, int col)
{
    printf(ESC "%d;%dH", row, col);
}

static struct termios s_orig_termios;
static bool           s_raw_mode = false;
static int            s_term_cols;
static int            s_term_rows;

static void get_term_size(void)
{
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
        s_term_cols = w.ws_col;
        s_term_rows = w.ws_row;
    } else {
        s_term_cols = 80;
        s_term_rows = 24;
    }
}

static const char *NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

static const char *CH_NAMES[] = {
    "LEAD  ", "HARMON", "BASS  ", "ARPEG ", "PAD   ", "DRUMS "
};

static const char *CH_COLORS[] = {
    FG_CYAN, FG_BLUE, FG_GREEN, FG_MAGENTA, FG_YELLOW, FG_RED
};

static char    s_filename[256];
static size_t  s_filesize;
static int     s_frame;

void display_init(const char *filename, size_t filesize)
{
    get_term_size();
    strncpy(s_filename, filename, sizeof(s_filename) - 1);
    s_filename[sizeof(s_filename) - 1] = '\0';
    s_filesize = filesize;
    s_frame = 0;

    tcgetattr(STDIN_FILENO, &s_orig_termios);
    struct termios raw = s_orig_termios;
    raw.c_lflag &= (tcflag_t)~(ECHO | ICANON);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    s_raw_mode = true;

    printf(ALT_BUF_ON HIDE_CURSOR CLEAR HOME);
    fflush(stdout);
}

void display_update(const SynthState *s)
{
    get_term_size();
    int w = s_term_cols;
    if (w < 50) w = 50;
    s_frame++;

    printf(HOME);

    /* Header box */
    move_to(1, 1);
    printf(BOLD FG_CYAN "  +");
    for (int i = 0; i < w - 6; i++) printf("-");
    printf("+" RESET);

    move_to(2, 1);
    printf(BOLD FG_CYAN "  |" FG_MAGENTA " " "\xe2\x99\xaa" " CHEAPBIN" RESET);
    /* section name + BPM on the right */
    {
        char info[64];
        int n = snprintf(info, sizeof(info), "[ %s ] %.0f BPM ",
                         s->section_name, (double)s->bpm);
        int pad = w - 6 - 12 - n;
        if (pad < 0) pad = 0;
        for (int i = 0; i < pad; i++) printf(" ");
        printf(BOLD FG_YELLOW "%s" FG_CYAN "|" RESET, info);
    }

    move_to(3, 1);
    printf(BOLD FG_CYAN "  +");
    for (int i = 0; i < w - 6; i++) printf("-");
    printf("+" RESET);

    /* File info */
    move_to(5, 3);
    printf(FG_GRAY "FILE " RESET BOLD "%s" RESET, s_filename);
    move_to(6, 3);
    {
        char size_str[64];
        if (s_filesize >= 1048576)
            snprintf(size_str, sizeof(size_str), "%.1f MB", (double)s_filesize / 1048576.0);
        else if (s_filesize >= 1024)
            snprintf(size_str, sizeof(size_str), "%.1f KB", (double)s_filesize / 1024.0);
        else
            snprintf(size_str, sizeof(size_str), "%zu bytes", s_filesize);
        printf(FG_GRAY "SIZE " RESET "%s", size_str);
    }

    /* Now playing */
    move_to(8, 3);
    {
        int note = s->current_note;
        if (note > 0 && note < 128) {
            const char *name = NOTE_NAMES[note % 12];
            int octave = (note / 12) - 1;
            printf(FG_GRAY "NOTE " RESET BOLD FG_CYAN "%-3s%d" RESET
                   "  " FG_GRAY "SECTION " RESET BOLD "%s" RESET "        ",
                   name, octave, s->section_name);
        } else {
            printf(FG_GRAY "NOTE " RESET DIM "---" RESET
                   "  " FG_GRAY "SECTION " RESET BOLD "%s" RESET "        ",
                   s->section_name);
        }
    }

    /* Channel level meters */
    int bar_width = w - 18;
    if (bar_width < 10) bar_width = 10;
    if (bar_width > 60) bar_width = 60;

    for (int c = 0; c < NUM_CHANNELS; c++) {
        move_to(10 + c, 3);
        printf("%s%s" RESET " ", CH_COLORS[c], CH_NAMES[c]);

        float level = s->ch_levels[c];
        if (level > 1.0f) level = 1.0f;
        int filled = (int)(level * (float)bar_width);

        printf("%s", CH_COLORS[c]);
        for (int i = 0; i < bar_width; i++) {
            if (i < filled) {
                /* gradient blocks */
                if (i < filled - 1) {
                    printf("\xe2\x96\x88");  /* full block */
                } else {
                    float frac = level * (float)bar_width - (float)i;
                    int idx = (int)(frac * 4.0f);
                    if (idx <= 0) printf("\xe2\x96\x91");       /* light shade */
                    else if (idx == 1) printf("\xe2\x96\x92");  /* medium shade */
                    else if (idx == 2) printf("\xe2\x96\x93");  /* dark shade */
                    else printf("\xe2\x96\x88");                 /* full block */
                }
            } else {
                printf(DIM "\xe2\x96\x91" RESET "%s", CH_COLORS[c]);
            }
        }
        printf(RESET);
    }

    /* Waveform */
    int vis_y = 17;
    int vis_h = 6;
    int vis_w = bar_width;

    move_to(vis_y, 3);
    printf(FG_GRAY "WAVEFORM" RESET);

    for (int row = 0; row < vis_h; row++) {
        move_to(vis_y + 1 + row, 3);
        float row_level = 1.0f - (float)row / (float)(vis_h - 1);

        for (int col = 0; col < vis_w; col++) {
            float t = (float)col / (float)vis_w;
            /* combine multiple channel levels for interesting waveform */
            float f1 = 2.0f + s->ch_levels[CH_LEAD] * 6.0f;
            float f2 = 1.0f + s->ch_levels[CH_BASS] * 3.0f;
            float phase_off = (float)s_frame * 0.1f;
            float wave = 0.5f + 0.25f * sinf(t * f1 * 6.2832f + phase_off)
                              + 0.15f * sinf(t * f2 * 6.2832f + phase_off * 0.7f);

            float dist = fabsf(row_level - wave);

            if (dist < 0.08f)
                printf(FG_CYAN "\xe2\x96\x88" RESET);
            else if (dist < 0.16f)
                printf(FG_CYAN "\xe2\x96\x93" RESET);
            else if (dist < 0.24f)
                printf(DIM FG_CYAN "\xe2\x96\x91" RESET);
            else
                printf(" ");
        }
    }

    /* Progress bar */
    int prog_y = vis_y + vis_h + 2;
    move_to(prog_y, 3);
    {
        int pbar_w = w - 14;
        if (pbar_w < 10) pbar_w = 10;
        int pct = (int)(s->progress * 100.0f);
        if (pct > 100) pct = 100;
        int filled = (int)(s->progress * (float)pbar_w);

        printf(FG_GRAY "[" RESET);
        for (int i = 0; i < pbar_w; i++) {
            if (i < filled)
                printf(FG_CYAN "\xe2\x94\x81" RESET);
            else
                printf(DIM "\xe2\x94\x80" RESET);
        }
        printf(FG_GRAY "] " RESET BOLD "%3d%%" RESET, pct);
    }

    /* Status */
    move_to(prog_y + 2, 3);
    if (s->paused)
        printf(BOLD FG_YELLOW "|| PAUSED " RESET FG_GRAY " space" RESET " resume  " FG_GRAY "q" RESET " quit");
    else if (s->finished)
        printf(BOLD FG_GREEN "\xe2\x9c\x93 FINISHED " RESET FG_GRAY "q" RESET " quit          ");
    else
        printf(BOLD FG_GREEN "\xe2\x96\xb6 PLAYING " RESET FG_GRAY " space" RESET " pause   " FG_GRAY "q" RESET " quit");

    printf(ESC "J");
    fflush(stdout);
}

void display_cleanup(void)
{
    if (s_raw_mode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_orig_termios);
        s_raw_mode = false;
    }
    printf(SHOW_CURSOR ALT_BUF_OFF);
    fflush(stdout);
}

int display_poll_key(void)
{
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) == 1)
        return (int)c;
    return 0;
}
