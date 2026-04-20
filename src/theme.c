#include "theme.h"

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
