#ifndef CHEAPBIN_THEME_H
#define CHEAPBIN_THEME_H

#include "synth.h"

typedef enum {
    THEME_DEFAULT = 0,
    THEME_SOFTICE,
    THEME_TD32,
    NUM_THEMES
} ThemeType;

const char *theme_name(ThemeType t);
ThemeType   theme_next(ThemeType current);
ThemeType   theme_prev(ThemeType current);
/* Returns -1 on unknown name. Accepts: default, softice, td32 */
int         theme_parse(const char *name);

/* Each theme implements a single full-screen draw function. */
void theme_default_draw(const SynthState *s);
void theme_softice_draw(const SynthState *s);
void theme_td32_draw(const SynthState *s);

#endif
