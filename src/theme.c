#include "theme.h"
#include <string.h>

static const char *THEME_NAMES[] = {
    [THEME_DEFAULT] = "Default",
    [THEME_SOFTICE] = "SoftICE",
    [THEME_TD32]    = "TD32",
};

const char *theme_name(ThemeType t)
{
    if (t >= 0 && t < NUM_THEMES) return THEME_NAMES[t];
    return "Unknown";
}

ThemeType theme_next(ThemeType current)
{
    int n = (int)current + 1;
    if (n >= NUM_THEMES) n = 0;
    return (ThemeType)n;
}

ThemeType theme_prev(ThemeType current)
{
    int n = (int)current - 1;
    if (n < 0) n = NUM_THEMES - 1;
    return (ThemeType)n;
}

int theme_parse(const char *name)
{
    if (!name) return -1;
    if (strcmp(name, "default") == 0) return THEME_DEFAULT;
    if (strcmp(name, "softice") == 0) return THEME_SOFTICE;
    if (strcmp(name, "td32")    == 0) return THEME_TD32;
    return -1;
}
