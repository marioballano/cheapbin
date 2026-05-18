#ifndef CHEAPBIN_SDL_APP_H
#define CHEAPBIN_SDL_APP_H

#include "../binview.h"
#include "../composer.h"
#include "../style.h"
#include "../synth.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *filepath;
    int forced_chip;
    int forced_style;
    int forced_scale;
    int use_r2;
} CheapbinSdlOptions;

typedef enum {
    CHEAPBIN_SDL_ARGS_ERROR = -1,
    CHEAPBIN_SDL_ARGS_OK    = 0,
    CHEAPBIN_SDL_ARGS_HELP  = 1,
} CheapbinSdlArgsResult;

typedef struct {
    uint8_t      *data;
    size_t        size;
    Composition   comp;
    SynthState    synth;
    BinView      *bv;
    StyleType     cur_style;
    int           paused;
    int           finish_hold_frames;
    int           quit_requested;
    int           sdl_ready;
    int           comp_ready;
    int           synth_ready;
    int           audio_ready;
    int           display_ready;
} CheapbinSdlApp;

void cheapbin_sdl_print_usage(const char *prog);
CheapbinSdlArgsResult cheapbin_sdl_parse_args(int argc, char *argv[],
                                              CheapbinSdlOptions *options);

int  cheapbin_sdl_app_init(CheapbinSdlApp *app,
                           const CheapbinSdlOptions *options);
int  cheapbin_sdl_app_frame(CheapbinSdlApp *app);
void cheapbin_sdl_app_cleanup(CheapbinSdlApp *app);

#endif