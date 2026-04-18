#ifndef CHEAPBIN_DISPLAY_H
#define CHEAPBIN_DISPLAY_H

#include "synth.h"
#include <stdbool.h>

/* Initialize the terminal UI (alt screen, raw mode, hide cursor).
   Pass the raw file data and size for hex dump visualization. */
void display_init(const char *filename, size_t filesize,
                  const uint8_t *data, size_t data_size);

/* Redraw the UI with current synth state. */
void display_update(const SynthState *s);

/* Restore terminal to normal state. */
void display_cleanup(void);

/* Check for keypress (non-blocking).
   Returns: 'q' for quit, ' ' for pause toggle, 0 if no key. */
int display_poll_key(void);

#endif
