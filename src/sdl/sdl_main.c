/* Native SDL front-end entry point. The shared app lifecycle lives in
 * sdl_app.c; this file only owns POSIX signals and the blocking native loop. */

#include "sdl_app.h"
#include "sdl_include.h"

#include <signal.h>
#include <string.h>

static volatile sig_atomic_t g_signal_quit;

static void signal_handler(int sig)
{
    (void)sig;
    g_signal_quit = 1;
}

static void install_signal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
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
        cheapbin_sdl_print_usage(argv[0]);
        return 1;
    }

    CheapbinSdlApp app;
    if (cheapbin_sdl_app_init(&app, &options) != 0) {
        return 1;
    }

    install_signal_handlers();
    while (!g_signal_quit) {
        if (cheapbin_sdl_app_frame(&app)) {
            break;
        }
        SDL_Delay(33);
    }

    cheapbin_sdl_app_cleanup(&app);
    return 0;
}
