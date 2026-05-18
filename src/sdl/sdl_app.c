#include "sdl_app.h"

#include "sdl_display.h"
#include "sdl_include.h"
#include "../audio.h"
#include "../chipemu.h"
#include "../reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cheapbin_sdl_print_usage(const char *prog)
{
    fprintf(stderr,
        "cheapbin (SDL) - binary-to-chiptune music\n"
        "\n"
        "Usage: %s [options] <file>\n"
        "\n"
        "  --chip <name>    sid|nes|genesis|spectrum|clean\n"
        "  --style <name>   synthwave|dungeon|baroque|acid|doom|\n"
        "                   eurobeat|demoscene|ska|trap|progrock|none\n"
        "  --scale <name>   major|minor|dorian|... (see README)\n"
        "  -r, --no-r2      Disable radare2; use built-in fallback\n"
        "  -h, --help       Show this help\n"
        "\n"
        "Controls (in window):\n"
        "  space    pause/resume\n"
        "  h / l    seek -5s / +5s    (or arrow keys)\n"
        "  c / C    next/prev chip\n"
        "  s / S    next/prev style\n"
        "  k / K    next/prev scale\n"
        "  gamepad  A/start pause, dpad seek/scale, X/Y chip/style,\n"
        "           L/R prev chip/style, B/back quit\n"
        "  q / esc  quit\n",
        prog);
}

CheapbinSdlArgsResult cheapbin_sdl_parse_args(int argc, char *argv[],
                                              CheapbinSdlOptions *options)
{
    options->filepath = NULL;
    options->forced_chip = -1;
    options->forced_style = -1;
    options->forced_scale = -1;
    options->use_r2 = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            cheapbin_sdl_print_usage(argv[0]);
            return CHEAPBIN_SDL_ARGS_HELP;
        } else if (strcmp(argv[i], "--chip") == 0 && i + 1 < argc) {
            options->forced_chip = chip_parse(argv[++i]);
            if (options->forced_chip < 0) {
                fprintf(stderr, "error: unknown chip '%s'\n", argv[i]);
                return CHEAPBIN_SDL_ARGS_ERROR;
            }
        } else if (strcmp(argv[i], "--style") == 0 && i + 1 < argc) {
            options->forced_style = style_parse(argv[++i]);
            if (options->forced_style < 0) {
                fprintf(stderr, "error: unknown style '%s'\n", argv[i]);
                return CHEAPBIN_SDL_ARGS_ERROR;
            }
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            options->forced_scale = scale_parse(argv[++i]);
            if (options->forced_scale < 0) {
                fprintf(stderr, "error: unknown scale '%s'\n", argv[i]);
                return CHEAPBIN_SDL_ARGS_ERROR;
            }
        } else if (strcmp(argv[i], "-r") == 0 ||
                   strcmp(argv[i], "--no-r2") == 0) {
            options->use_r2 = 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            cheapbin_sdl_print_usage(argv[0]);
            return CHEAPBIN_SDL_ARGS_ERROR;
        } else {
            options->filepath = argv[i];
        }
    }

    return CHEAPBIN_SDL_ARGS_OK;
}

static const char *display_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash)) {
        slash = backslash;
    }
    return slash ? slash + 1 : path;
}

void cheapbin_sdl_app_cleanup(CheapbinSdlApp *app)
{
    if (app->display_ready) {
        sdl_display_cleanup();
        app->display_ready = 0;
    }
    if (app->audio_ready) {
        audio_stop();
        app->audio_ready = 0;
    }
    if (app->bv) {
        binview_close(app->bv);
        app->bv = NULL;
    }
    if (app->synth_ready) {
        free(app->synth.styled_events);
        app->synth.styled_events = NULL;
        app->synth_ready = 0;
    }
    if (app->data) {
        free(app->data);
        app->data = NULL;
    }
    if (app->comp_ready) {
        composition_free(&app->comp);
        app->comp_ready = 0;
    }
    if (app->sdl_ready) {
        SDL_Quit();
        app->sdl_ready = 0;
    }
}

int cheapbin_sdl_app_init(CheapbinSdlApp *app,
                          const CheapbinSdlOptions *options)
{
    memset(app, 0, sizeof(*app));

    if (!options->filepath) {
        fprintf(stderr, "error: no input file\n");
        return -1;
    }

    if (SDL_Init(0) != 0) {
        fprintf(stderr, "error: SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    app->sdl_ready = 1;

    if (read_binary(options->filepath, &app->data, &app->size) != 0) {
        cheapbin_sdl_app_cleanup(app);
        return -1;
    }

    if (compose_with_scale(app->data, app->size,
                           options->forced_scale, &app->comp) != 0) {
        fprintf(stderr, "error: composition failed\n");
        cheapbin_sdl_app_cleanup(app);
        return -1;
    }
    app->comp_ready = 1;

    if (app->comp.num_events == 0) {
        fprintf(stderr, "error: no musical events generated\n");
        cheapbin_sdl_app_cleanup(app);
        return -1;
    }

    synth_init(&app->synth, &app->comp);
    app->synth_ready = 1;

    ChipType chip = (options->forced_chip >= 0)
                  ? (ChipType)options->forced_chip
                  : chip_select_from_data(app->data, app->size);
    synth_set_chip(&app->synth, chip);

    app->cur_style = (options->forced_style >= 0)
                   ? (StyleType)options->forced_style
                   : STYLE_NONE;
    synth_apply_style(&app->synth, app->cur_style, &app->comp);

    if (audio_init(&app->synth) != 0) {
        cheapbin_sdl_app_cleanup(app);
        return -1;
    }
    app->audio_ready = 1;

    app->bv = binview_open(options->filepath, app->data, app->size,
                           options->use_r2);
    if (sdl_display_init(display_basename(options->filepath),
                         app->size, app->bv) != 0) {
        cheapbin_sdl_app_cleanup(app);
        return -1;
    }
    app->display_ready = 1;

    if (audio_start() != 0) {
        cheapbin_sdl_app_cleanup(app);
        return -1;
    }
    return 0;
}

static void app_seek_seconds(CheapbinSdlApp *app, int direction)
{
    int step = (int)(app->synth.bpm * 4.0f / 60.0f * 5.0f);
    if (step < 1) step = 1;
    synth_seek(&app->synth, direction * step);
}

static int app_handle_key(CheapbinSdlApp *app, int key)
{
    if (key == 'q' || key == 'Q' || key == KEY_QUIT) {
        app->quit_requested = 1;
        return 1;
    }
    if (key == ' ') {
        app->paused = !app->paused;
        app->synth.paused = app->paused;
        if (app->paused) audio_pause(); else audio_resume();
    } else if (key == 'c') {
        synth_set_chip(&app->synth, chip_next(app->synth.chip_type));
    } else if (key == 'C') {
        synth_set_chip(&app->synth, chip_prev(app->synth.chip_type));
    } else if (key == 's') {
        app->cur_style = style_next(app->cur_style);
        synth_apply_style(&app->synth, app->cur_style, &app->comp);
    } else if (key == 'S') {
        app->cur_style = style_prev(app->cur_style);
        synth_apply_style(&app->synth, app->cur_style, &app->comp);
    } else if (key == 'k') {
        ScaleType nx = scale_next(app->synth.scale_type);
        synth_set_scale(&app->synth, app->data, app->size,
                        (int)nx, &app->comp);
    } else if (key == 'K') {
        ScaleType pv = scale_prev(app->synth.scale_type);
        synth_set_scale(&app->synth, app->data, app->size,
                        (int)pv, &app->comp);
    } else if (key == 'l' || key == 'L' || key == KEY_RIGHT) {
        app_seek_seconds(app, 1);
    } else if (key == 'h' || key == 'H' || key == KEY_LEFT) {
        app_seek_seconds(app, -1);
    }
    return 0;
}

int cheapbin_sdl_app_frame(CheapbinSdlApp *app)
{
    int key;
    while ((key = sdl_display_poll_key()) != 0) {
        if (app_handle_key(app, key)) {
            return 1;
        }
    }

    sdl_display_update(&app->synth);

    if (app->synth.finished && !app->paused) {
        app->finish_hold_frames++;
        return app->finish_hold_frames >= 90;
    }

    app->finish_hold_frames = 0;
    return app->quit_requested;
}