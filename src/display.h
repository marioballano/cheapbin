#ifndef CHEAPBIN_DISPLAY_H
#define CHEAPBIN_DISPLAY_H

#include "synth.h"
#include "binview.h"
#include <stdbool.h>

/* Initialize the terminal UI (alt screen, raw mode, hide cursor).
   The BinView supplies file format info, hex bytes, disasm and regs;
   it must outlive the display. */
void display_init(const char *filename, size_t filesize, BinView *bv);

/* Redraw the UI with current synth state. */
void display_update(const SynthState *s);

/* Restore terminal to normal state. */
void display_cleanup(void);

/* Synthetic key codes for non-ASCII keys (outside 0..255). */
#define KEY_UP    0x100
#define KEY_DOWN  0x101
#define KEY_LEFT  0x102
#define KEY_RIGHT 0x103

/* Check for keypress (non-blocking).
   Returns: ASCII byte, one of the KEY_* codes, or 0 if no key. */
int display_poll_key(void);

/* Cycle to the next visual theme. */
void display_cycle_theme(void);

#endif
