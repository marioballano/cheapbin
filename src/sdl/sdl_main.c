/*
 * SDL/ALSA front-end for cheapbin — meant for the GKD bubble (640x480).
 *
 * Pulls in all the existing synth/composer/binview machinery and replaces
 * only the I/O surface: SDL2 video instead of the termios+ANSI display,
 * and either SDL2 audio (macOS test build) or ALSA (Linux ARM64 target).
 */

#include "../reader.h"
#include "../composer.h"
#include "../synth.h"
#include "../chipemu.h"
#include "../style.h"
#include "../audio.h"
#include "../binview.h"
#include "sdl_display.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <libgen.h>

#ifdef __APPLE__
#  include <SDL2/SDL.h>
#else
#  include <SDL.h>
#endif

static volatile sig_atomic_t g_quit = 0;

static void signal_handler(int sig)
{
    (void)sig;
    g_quit = 1;
}

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "cheapbin (SDL) — binary-to-chiptune music\n"
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
        "  q / esc  quit\n",
        prog);
}

int main(int argc, char *argv[])
{
    const char *filepath   = NULL;
    int forced_chip  = -1;
    int forced_style = -1;
    int forced_scale = -1;
    int use_r2       = 1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--chip") == 0 && i + 1 < argc) {
            forced_chip = chip_parse(argv[++i]);
            if (forced_chip < 0) {
                fprintf(stderr, "error: unknown chip '%s'\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "--style") == 0 && i + 1 < argc) {
            forced_style = style_parse(argv[++i]);
            if (forced_style < 0) {
                fprintf(stderr, "error: unknown style '%s'\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            forced_scale = scale_parse(argv[++i]);
            if (forced_scale < 0) {
                fprintf(stderr, "error: unknown scale '%s'\n", argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-r") == 0 ||
                   strcmp(argv[i], "--no-r2") == 0) {
            use_r2 = 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        } else {
            filepath = argv[i];
        }
    }

    if (!filepath) {
        print_usage(argv[0]);
        return 1;
    }

    if (SDL_Init(0) != 0) {
        fprintf(stderr, "error: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    uint8_t *data = NULL;
    size_t   size = 0;
    if (read_binary(filepath, &data, &size) != 0) {
        SDL_Quit();
        return 1;
    }

    Composition comp;
    if (compose_with_scale(data, size, forced_scale, &comp) != 0) {
        fprintf(stderr, "error: composition failed\n");
        free(data);
        SDL_Quit();
        return 1;
    }
    if (comp.num_events == 0) {
        fprintf(stderr, "error: no musical events generated\n");
        free(data);
        composition_free(&comp);
        SDL_Quit();
        return 1;
    }

    SynthState synth;
    synth_init(&synth, &comp);

    ChipType chip = (forced_chip >= 0)
                  ? (ChipType)forced_chip
                  : chip_select_from_data(data, size);
    synth_set_chip(&synth, chip);

    StyleType cur_style = (forced_style >= 0)
                        ? (StyleType)forced_style
                        : STYLE_NONE;
    synth_apply_style(&synth, cur_style, &comp);

    if (audio_init(&synth) != 0) {
        free(data);
        composition_free(&comp);
        SDL_Quit();
        return 1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    BinView *bv = binview_open(filepath, data, size, use_r2);

    char *pathcopy = strdup(filepath);
    const char *fname = basename(pathcopy);
    if (sdl_display_init(fname, size, bv) != 0) {
        free(pathcopy);
        audio_stop();
        binview_close(bv);
        free(data);
        composition_free(&comp);
        SDL_Quit();
        return 1;
    }
    free(pathcopy);

    if (audio_start() != 0) {
        sdl_display_cleanup();
        audio_stop();
        binview_close(bv);
        free(data);
        composition_free(&comp);
        SDL_Quit();
        return 1;
    }

    int paused = 0;
    while (!g_quit) {
        int key;
        while ((key = sdl_display_poll_key()) != 0) {
            if (key == 'q' || key == 'Q' || key == KEY_QUIT) {
                g_quit = 1;
                break;
            } else if (key == ' ') {
                paused = !paused;
                synth.paused = paused;
                if (paused) audio_pause(); else audio_resume();
            } else if (key == 'c') {
                synth_set_chip(&synth, chip_next(synth.chip_type));
            } else if (key == 'C') {
                synth_set_chip(&synth, chip_prev(synth.chip_type));
            } else if (key == 's') {
                cur_style = style_next(cur_style);
                synth_apply_style(&synth, cur_style, &comp);
            } else if (key == 'S') {
                cur_style = style_prev(cur_style);
                synth_apply_style(&synth, cur_style, &comp);
            } else if (key == 'k') {
                ScaleType nx = scale_next(synth.scale_type);
                synth_set_scale(&synth, data, size, (int)nx, &comp);
            } else if (key == 'K') {
                ScaleType pv = scale_prev(synth.scale_type);
                synth_set_scale(&synth, data, size, (int)pv, &comp);
            } else if (key == 'l' || key == 'L' || key == KEY_RIGHT) {
                int step = (int)(synth.bpm * 4.0f / 60.0f * 5.0f);
                if (step < 1) step = 1;
                synth_seek(&synth, step);
            } else if (key == 'h' || key == 'H' || key == KEY_LEFT) {
                int step = (int)(synth.bpm * 4.0f / 60.0f * 5.0f);
                if (step < 1) step = 1;
                synth_seek(&synth, -step);
            }
        }
        if (g_quit) break;

        sdl_display_update(&synth);

        if (synth.finished && !paused) {
            sdl_display_update(&synth);
            for (int i = 0; i < 90 && !g_quit; i++) {
                SDL_Delay(33);
                int k = sdl_display_poll_key();
                if (k == 'q' || k == 'Q' || k == KEY_QUIT) { g_quit = 1; break; }
                sdl_display_update(&synth);
            }
            break;
        }

        SDL_Delay(33);  /* ~30 FPS */
    }

    sdl_display_cleanup();
    audio_stop();
    binview_close(bv);
    free(synth.styled_events);
    free(data);
    composition_free(&comp);
    SDL_Quit();
    return 0;
}
