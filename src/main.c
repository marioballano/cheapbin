#include "reader.h"
#include "composer.h"
#include "synth.h"
#include "chipemu.h"
#include "style.h"
#include "audio.h"
#include "display.h"
#include "binview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <libgen.h>

/* ── Globals for signal handler cleanup ─────────────────────────────── */

static volatile sig_atomic_t g_quit = 0;

static void signal_handler(int sig)
{
    (void)sig;
    g_quit = 1;
}

/* ── Usage ──────────────────────────────────────────────────────────── */

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "\n"
        "  \033[1;96m♪ CHEAPBIN\033[0m — binary-to-chiptune music\n"
        "\n"
        "  \033[1mUsage:\033[0m %s [options] <file>\n"
        "\n"
        "  Reads a binary file and transforms it into a chiptune melody.\n"
        "  Any file works — executables, images, archives, you name it.\n"
        "\n"
        "  \033[1mOptions:\033[0m\n"
        "    --chip <name>    Force a sound chip emulation:\n"
        "                       sid, nes, genesis, spectrum, clean\n"
        "    --style <name>   Force a music style transformation:\n"
        "                       synthwave, dungeon, baroque, acid,\n"
        "                       doom, eurobeat, demoscene, ska,\n"
        "                       trap, progrock, none\n"
        "    -r, --no-r2      Disable radare2 backend; use built-in fake\n"
        "                       disasm/hex/regs instead\n"
        "    -h, --help       Show this help\n"
        "\n"
        "  \033[90mControls:\033[0m\n"
        "    space   pause / resume\n"
        "    h / ←   seek backward 5s\n"
        "    l / →   seek forward 5s\n"
        "    c       cycle sound chip\n"
        "    s       cycle music style\n"
        "    q       quit\n"
        "\n", prog);
}

/* ── Main ───────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *filepath = NULL;
    int forced_chip  = -1;   /* -1 = auto-select from file content */
    int forced_style = -1;   /* -1 = no style transformation */
    int use_r2       = 1;    /* 0 = force fallback disasm/hex/regs */

    /* ── Parse arguments ── */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--chip") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --chip requires an argument\n");
                return 1;
            }
            forced_chip = chip_parse(argv[++i]);
            if (forced_chip < 0) {
                fprintf(stderr, "error: unknown chip '%s'\n", argv[i]);
                fprintf(stderr, "  valid: sid, nes, genesis, spectrum, clean\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--style") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --style requires an argument\n");
                return 1;
            }
            forced_style = style_parse(argv[++i]);
            if (forced_style < 0) {
                fprintf(stderr, "error: unknown style '%s'\n", argv[i]);
                fprintf(stderr, "  valid: synthwave, dungeon, baroque, acid, doom,\n"
                                "         eurobeat, demoscene, ska, trap, progrock, none\n");
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

    /* ── Read binary ── */
    uint8_t *data = NULL;
    size_t   size = 0;
    if (read_binary(filepath, &data, &size) != 0)
        return 1;

    /* ── Compose ── */
    Composition comp;
    if (compose(data, size, &comp) != 0) {
        fprintf(stderr, "error: composition failed\n");
        free(data);
        return 1;
    }

    if (comp.num_events == 0) {
        fprintf(stderr, "error: no musical events generated\n");
        free(data);
        composition_free(&comp);
        return 1;
    }

    /* ── Init synth ── */
    SynthState synth;
    synth_init(&synth, &comp);

    /* ── Select sound chip ── */
    ChipType chip = (forced_chip >= 0)
                  ? (ChipType)forced_chip
                  : chip_select_from_data(data, size);
    synth_set_chip(&synth, chip);

    /* ── Apply music style ── */
    StyleType current_style = (forced_style >= 0)
                            ? (StyleType)forced_style
                            : STYLE_NONE;
    synth_apply_style(&synth, current_style, &comp);

    /* ── Init audio ── */
    if (audio_init(&synth) != 0) {
        free(data);
        composition_free(&comp);
        return 1;
    }

    /* ── Install signal handler ── */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* ── Init binview (optional r2 backend) ── */
    BinView *bv = binview_open(filepath, data, size, use_r2);

    /* ── Init display ── */
    char *pathcopy = strdup(filepath);
    const char *fname = basename(pathcopy);
    display_init(fname, size, bv);
    free(pathcopy);

    /* ── Start playback ── */
    if (audio_start() != 0) {
        display_cleanup();
        audio_stop();
        free(data);
        composition_free(&comp);
        return 1;
    }

    /* ── Main loop ── */
    int paused = 0;
    while (!g_quit) {
        /* poll keyboard */
        int key = display_poll_key();
        if (key == 'q' || key == 'Q') {
            break;
        } else if (key == ' ') {
            paused = !paused;
            synth.paused = paused;
            if (paused)
                audio_pause();
            else
                audio_resume();
        } else if (key == 'c' || key == 'C') {
            synth_set_chip(&synth, chip_next(synth.chip_type));
        } else if (key == 's' || key == 'S') {
            current_style = style_next(current_style);
            synth_apply_style(&synth, current_style, &comp);
        } else if (key == 'l' || key == 'L' || key == KEY_RIGHT) {
            /* 5 seconds at current BPM — composition uses 16th-note ticks. */
            int step = (int)(synth.bpm * 4.0f / 60.0f * 5.0f);
            if (step < 1) step = 1;
            synth_seek(&synth, step);
        } else if (key == 'h' || key == 'H' || key == KEY_LEFT) {
            int step = (int)(synth.bpm * 4.0f / 60.0f * 5.0f);
            if (step < 1) step = 1;
            synth_seek(&synth, -step);
        }

        /* update display */
        display_update(&synth);

        /* check if song is done */
        if (synth.finished && !paused) {
            /* let the last buffers drain, then show final state */
            display_update(&synth);
            /* wait a moment for user to see "FINISHED" */
            for (int i = 0; i < 150 && !g_quit; i++) {
                usleep(20000);
                key = display_poll_key();
                if (key == 'q' || key == 'Q') { g_quit = 1; break; }
                display_update(&synth);
            }
            break;
        }

        /* ~30 FPS */
        usleep(33000);
    }

    /* ── Cleanup ── */
    display_cleanup();
    audio_stop();
    binview_close(bv);
    free(synth.styled_events);
    free(data);
    composition_free(&comp);

    return 0;
}
