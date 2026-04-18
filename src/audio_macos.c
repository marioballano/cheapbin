#include "audio.h"
#include <AudioToolbox/AudioToolbox.h>
#include <stdio.h>
#include <string.h>

#define NUM_BUFFERS  3
#define BUFFER_SIZE  4096   /* bytes per buffer (2048 int16_t samples) */

static AudioQueueRef         s_queue;
static AudioQueueBufferRef   s_buffers[NUM_BUFFERS];
static SynthState           *s_synth;
static bool                  s_running;

/* ── Audio callback ─────────────────────────────────────────────────── */

static void audio_callback(void *user_data, AudioQueueRef queue,
                           AudioQueueBufferRef buf)
{
    SynthState *synth = (SynthState *)user_data;
    int num_samples = BUFFER_SIZE / (int)sizeof(int16_t);

    synth_render(synth, (int16_t *)buf->mAudioData, num_samples);
    buf->mAudioDataByteSize = BUFFER_SIZE;

    if (s_running) {
        AudioQueueEnqueueBuffer(queue, buf, 0, NULL);
    }
}

/* ── Public API ─────────────────────────────────────────────────────── */

int audio_init(SynthState *synth)
{
    s_synth  = synth;
    s_running = false;

    AudioStreamBasicDescription fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.mSampleRate       = (Float64)SAMPLE_RATE;
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kLinearPCMFormatFlagIsSignedInteger
                          | kLinearPCMFormatFlagIsPacked;
    fmt.mBitsPerChannel   = 16;
    fmt.mChannelsPerFrame = 1;
    fmt.mFramesPerPacket  = 1;
    fmt.mBytesPerFrame    = 2;
    fmt.mBytesPerPacket   = 2;

    OSStatus err = AudioQueueNewOutput(&fmt, audio_callback, synth,
                                       NULL, kCFRunLoopCommonModes, 0,
                                       &s_queue);
    if (err != noErr) {
        fprintf(stderr, "error: AudioQueueNewOutput failed (%d)\n", (int)err);
        return -1;
    }

    for (int i = 0; i < NUM_BUFFERS; i++) {
        err = AudioQueueAllocateBuffer(s_queue, BUFFER_SIZE, &s_buffers[i]);
        if (err != noErr) {
            fprintf(stderr, "error: AudioQueueAllocateBuffer failed (%d)\n", (int)err);
            return -1;
        }
        /* pre-fill and enqueue */
        int num_samples = BUFFER_SIZE / (int)sizeof(int16_t);
        synth_render(synth, (int16_t *)s_buffers[i]->mAudioData, num_samples);
        s_buffers[i]->mAudioDataByteSize = BUFFER_SIZE;
        AudioQueueEnqueueBuffer(s_queue, s_buffers[i], 0, NULL);
    }

    /* set volume */
    AudioQueueSetParameter(s_queue, kAudioQueueParam_Volume, 1.0f);

    return 0;
}

int audio_start(void)
{
    s_running = true;
    OSStatus err = AudioQueueStart(s_queue, NULL);
    if (err != noErr) {
        fprintf(stderr, "error: AudioQueueStart failed (%d)\n", (int)err);
        return -1;
    }
    return 0;
}

void audio_pause(void)
{
    AudioQueuePause(s_queue);
}

void audio_resume(void)
{
    AudioQueueStart(s_queue, NULL);
}

void audio_stop(void)
{
    s_running = false;
    AudioQueueStop(s_queue, true);
    AudioQueueDispose(s_queue, true);
}
