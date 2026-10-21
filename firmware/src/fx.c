#include "nightfall.h"

static int32_t limit(int32_t x)
{
    return x < -32768 ? -32768 : x > 32767 ? 32767 : x;
}
static int32_t mix(int32_t dry, int32_t wet, uint8_t amount)
{
    return dry + (wet - dry) * amount / 127;
}

int32_t nf_process_fx(nf_state *s, int32_t dry, int32_t send)
{
    nf_fx *f;
    int32_t x = dry, wet;
    unsigned i;

    f = &s->fx[NF_FILTER];
    if (f->enabled) {
        unsigned cutoff = 2u + f->value[0];
        int32_t previous = s->filter_z;
        s->filter_z += (x - s->filter_z) * (int32_t)cutoff / 129;
        wet = mix(s->filter_z, x - s->filter_z, f->value[2]);
        wet = limit(wet + (s->filter_z - previous) * f->value[1] / 32);
        x = mix(x, wet, f->value[3]);
    }
    f = &s->fx[NF_DRIVE];
    if (f->enabled) {
        int32_t gain = 32 + f->value[0] * 5;
        int32_t hard = limit(x * gain / 64);
        int32_t soft = hard * 24576 / (24576 + (hard < 0 ? -hard : hard));
        wet = mix(soft, hard, f->value[2]);
        s->drive_z += (wet - s->drive_z) * (16 + f->value[1]) / 143;
        x = mix(x, s->drive_z, f->value[3]);
    }
    f = &s->fx[NF_CHORUS];
    if (f->enabled) {
        uint32_t phase = s->chorus_phase & 4095u;
        uint32_t tri = phase < 2048u ? phase : 4095u - phase;
        uint32_t lag = 70u + f->value[1] * 3u + tri * f->value[2] / 2048u;
        uint32_t read = (s->chorus_pos + 2048u - lag) & 2047u;
        wet = s->chorus[read];
        s->chorus[s->chorus_pos] = (int16_t)limit(x);
        s->chorus_pos = (s->chorus_pos + 1u) & 2047u;
        s->chorus_phase += 1u + f->value[0] / 16u;
        x = mix(x, wet, f->value[3]);
    }
    f = &s->fx[NF_PHASER];
    if (f->enabled) {
        uint32_t phase = s->phaser_phase & 4095u;
        uint32_t tri = phase < 2048u ? phase : 4095u - phase;
        int32_t a = 20 + f->value[1] * 60 / 127 +
            (int32_t)tri * f->value[2] * 35 / (2048 * 127);
        wet = x;
        for (i = 0; i < 2u; i++) {
            int32_t y = s->phaser_in[i] + (s->phaser_out[i] - wet) * a / 128;
            s->phaser_in[i] = wet;
            s->phaser_out[i] = y;
            wet = y;
        }
        s->phaser_phase += 1u + f->value[0] / 12u;
        x = mix(x, wet, f->value[3]);
    }
    f = &s->fx[NF_DELAY];
    if (f->enabled) {
        uint32_t length = 1200u + f->value[0] * 80u;
        uint32_t pos = s->delay_pos % length;
        wet = s->delay[pos];
        s->delay[pos] = (int16_t)limit(send * f->value[1] / 127 +
            wet * f->value[2] / 160);
        s->delay_pos = (pos + 1u) % length;
        x = limit(x + wet * f->value[3] / 127);
    }
    f = &s->fx[NF_REVERB];
    if (f->enabled) {
        uint32_t pa = s->reverb_pos_a, pb = s->reverb_pos_b;
        int32_t a = s->reverb_a[pa], b = s->reverb_b[pb];
        int32_t feedback = 40 + f->value[0] * 70 / 127;
        s->reverb_lp_a += (a - s->reverb_lp_a) * (16 + f->value[1]) / 143;
        s->reverb_lp_b += (b - s->reverb_lp_b) * (16 + f->value[1]) / 143;
        s->reverb_a[pa] = (int16_t)limit(x / 5 + s->reverb_lp_a * feedback / 128);
        s->reverb_b[pb] = (int16_t)limit(x / 6 + s->reverb_lp_b * feedback / 128);
        s->reverb_pos_a = (pa + 1u) % 1559u;
        s->reverb_pos_b = (pb + 1u) % 1949u;
        wet = (a + b) * (32 + f->value[2]) / 159;
        x = limit(x + wet * f->value[3] / 254);
    }
    return limit(x);
}
