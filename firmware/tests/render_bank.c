#include "nightfall.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { SECONDS = 12, FRAMES_PER_BLOCK = 128 };
static nf_state state;
static int16_t pcm[FRAMES_PER_BLOCK * 2u];

static void byte(unsigned value)
{
    (void)putchar((int)(value & 255u));
}
static void word(unsigned value)
{
    byte(value); byte(value >> 8);
}
static void dword(uint32_t value)
{
    word(value); word(value >> 16);
}
static void header(uint32_t frames)
{
    uint32_t bytes = frames * 4u;
    (void)fwrite("RIFF", 1u, 4u, stdout);
    dword(bytes + 36u);
    (void)fwrite("WAVEfmt ", 1u, 8u, stdout);
    dword(16u); word(1u); word(2u);
    dword(NF_SAMPLE_RATE); dword(NF_SAMPLE_RATE * 4u);
    word(4u); word(16u);
    (void)fwrite("data", 1u, 4u, stdout);
    dword(bytes);
}

int main(int argc, char **argv)
{
    const uint32_t frames = NF_SAMPLE_RATE * SECONDS;
    uint64_t milliseconds = 0u;
    uint32_t done = 0u;
    unsigned preset;
    if (argc != 2 || argv[1][0] < '0' || argv[1][0] > '2' || argv[1][1])
        return 2;
    preset = (unsigned)(argv[1][0] - '0');
    nf_init(&state);
    if (nf_load_preset(&state, preset)) return 2;
    nf_toggle_play(&state);
    header(frames);
    while (done < frames) {
        uint32_t i, n = frames - done;
        if (n > FRAMES_PER_BLOCK) n = FRAMES_PER_BLOCK;
        nf_render_audio(&state, pcm, n);
        for (i = 0u; i < n * 2u; i++) word((uint16_t)pcm[i]);
        done += n;
        milliseconds += (uint64_t)n * 1000u;
        if (milliseconds >= NF_SAMPLE_RATE) {
            uint32_t ticks = (uint32_t)(milliseconds / NF_SAMPLE_RATE);
            nf_tick_ms(&state, ticks);
            milliseconds %= NF_SAMPLE_RATE;
        }
    }
    return ferror(stdout) ? 1 : 0;
}
