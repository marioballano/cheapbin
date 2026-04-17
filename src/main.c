#include "reader.h"
#include "composer.h"
#include "synth.h"
#include "audio.h"
#include "display.h"

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
        "  \033[1mUsage:\033[0m %s <file>\n"
        "\n"
        "  Reads a binary file and transforms it into a chiptune melody.\n"
        "  Any file works — executables, images, archives, you name it.\n"
        "\n"
        "  \033[90mControls:\033[0m\n"
        "    space   pause / resume\n"
        "    q       quit\n"
        "\n", prog);
}

/* ── Main ───────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (argc != 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return (argc != 2) ? 1 : 0;
    }

    const char *filepath = argv[1];

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

    /* ── Init display ── */
    char *pathcopy = strdup(filepath);
    const char *fname = basename(pathcopy);
    display_init(fname, size);
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
    free(data);
    composition_free(&comp);

    return 0;
}
