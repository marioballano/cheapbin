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

/* Check for keypress (non-blocking).
   Returns: 'q' for quit, ' ' for pause toggle, 0 if no key. */
int display_poll_key(void);

#endif
