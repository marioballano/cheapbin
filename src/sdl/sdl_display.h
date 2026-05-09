#ifndef CHEAPBIN_SDL_DISPLAY_H
#define CHEAPBIN_SDL_DISPLAY_H

#include "../synth.h"
#include "../binview.h"
#include <stdbool.h>

/* Window geometry — matches the GKD bubble panel. */
#define SDL_WIN_W   640
#define SDL_WIN_H   480

/* 8x8 bitmap font drawn at 2x → 16x16 px per cell → 40 cols × 30 rows.
   That's roughly one character per ~5mm on a 3" 640x480 screen — about
   the smallest size that's still comfortably readable. */
#define SDL_FONT_W  8
#define SDL_FONT_H  8
#define SDL_SCALE   2
#define SDL_CELL_W  (SDL_FONT_W * SDL_SCALE)
#define SDL_CELL_H  (SDL_FONT_H * SDL_SCALE)
#define SDL_COLS    (SDL_WIN_W / SDL_CELL_W)   /* 40 */
#define SDL_ROWS    (SDL_WIN_H / SDL_CELL_H)   /* 30 */

/* Same synthetic key codes as the curses backend. */
#define KEY_UP    0x100
#define KEY_DOWN  0x101
#define KEY_LEFT  0x102
#define KEY_RIGHT 0x103
#define KEY_QUIT  0x200   /* SDL_QUIT (window close) */

int  sdl_display_init(const char *filename, size_t filesize, BinView *bv);
void sdl_display_update(const SynthState *s);
void sdl_display_cleanup(void);

/* Non-blocking key poll. Returns 0 if no event pending. */
int  sdl_display_poll_key(void);

#endif
