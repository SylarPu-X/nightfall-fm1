#include "nightfall.h"
#include <stdio.h>
#include <string.h>

static nf_state state;
static uint16_t pixels[NF_WIDTH * NF_HEIGHT];
static int16_t audio[1024 * 2];

int main(int argc, char **argv)
{
    unsigned x, y;
    nf_init(&state);
    (void)nf_load_preset(&state, argc > 2 && argv[2][0] >= '0' &&
        argv[2][0] <= '2' ? (unsigned)(argv[2][0] - '0') : 0u);
    state.playing = 1u;
    if (argc > 1 && !strcmp(argv[1], "fx")) {
        state.page = NF_FX_PAGE; state.selected_fx = NF_DELAY;
    } else if (argc > 1 && !strcmp(argv[1], "edit")) state.page = NF_EDIT_PAGE;
    else if (argc > 1 && !strcmp(argv[1], "scope")) state.home_view = 1u;
    nf_note_on(&state, 1, 48, 112, NF_KEY);
    nf_note_on(&state, 2, 60, 92, NF_MIDI);
    nf_note_on(&state, 3, 72, 125, NF_SEQ);
    for (x = 0; x < 8u; x++) {
        nf_render_audio(&state, audio, 1024u);
        nf_update_terrain(&state);
    }
    nf_tick_ms(&state, 160u);
    nf_draw_city(&state, pixels);
    printf("P6\n240 240\n255\n");
    for (y = 0; y < NF_HEIGHT; y++) for (x = 0; x < NF_WIDTH; x++) {
        uint16_t c = pixels[y * NF_WIDTH + x];
        putchar((int)((c >> 11) * 255u / 31u));
        putchar((int)(((c >> 5) & 63u) * 255u / 63u));
        putchar((int)((c & 31u) * 255u / 31u));
    }
    return 0;
}
