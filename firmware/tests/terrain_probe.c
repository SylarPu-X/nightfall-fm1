#include "nightfall.h"
#include <stdio.h>

static nf_state state;
static int16_t audio[2048];

int main(void)
{
    unsigned i, j;
    nf_init(&state);
    nf_note_on(&state, 1u, 48u, 112u, NF_KEY);
    nf_note_on(&state, 2u, 60u, 92u, NF_MIDI);
    nf_note_on(&state, 3u, 72u, 125u, NF_SEQ);
    for (i = 0u; i < 8u; i++) {
        nf_render_audio(&state, audio, 1024u);
        nf_update_terrain(&state);
        for (j = 0u; j < NF_TERRAIN_BANDS; j++)
            printf("%u%c", state.terrain[state.terrain_head][j],
                   j + 1u == NF_TERRAIN_BANDS ? '\n' : ' ');
    }
    return 0;
}
