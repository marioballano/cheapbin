#include "sdl_app.h"

#include <emscripten/emscripten.h>

#ifndef CHEAPBIN_WEB_DEFAULT_FILE
#  define CHEAPBIN_WEB_DEFAULT_FILE "cheapbin.png"
#endif

static CheapbinSdlApp g_app;

static void web_main_loop(void)
{
    if (cheapbin_sdl_app_frame(&g_app)) {
        emscripten_cancel_main_loop();
        cheapbin_sdl_app_cleanup(&g_app);
    }
}

int main(int argc, char *argv[])
{
    CheapbinSdlOptions options;
    CheapbinSdlArgsResult args = cheapbin_sdl_parse_args(argc, argv, &options);
    if (args == CHEAPBIN_SDL_ARGS_HELP) {
        return 0;
    }
    if (args == CHEAPBIN_SDL_ARGS_ERROR) {
        return 1;
    }

    if (!options.filepath) {
        options.filepath = CHEAPBIN_WEB_DEFAULT_FILE;
    }
    options.use_r2 = 0;

    if (cheapbin_sdl_app_init(&g_app, &options) != 0) {
        return 1;
    }

    emscripten_set_main_loop(web_main_loop, 30, 1);
    return 0;
}