#include "nightfall.h"

/* RGB565 in logical channel order; the target swaps bytes only for LCD SPI. */
enum { BG = 0x0081, PANEL = 0x1122, GRID = 0x22c6, CITY_GRID = 0x1be7, DIM = 0x55cc,
       AMBER = 0xe52b, BRIGHT = 0x4fec, CYAN = 0x3753 };

static void pixel(uint16_t *p, int x, int y, uint16_t color)
{
    if ((unsigned)x < NF_WIDTH && (unsigned)y < NF_HEIGHT)
        p[(unsigned)y * NF_WIDTH + (unsigned)x] = color;
}
static void rect(uint16_t *p, int x, int y, int w, int h, uint16_t color)
{
    int xx, yy;
    for (yy = y; yy < y + h; yy++) for (xx = x; xx < x + w; xx++) pixel(p, xx, yy, color);
}
static void line(uint16_t *p, int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    unsigned guard = 0;
    while (guard++ < 480u) {
        int e2;
        pixel(p, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}
static uint16_t blend(uint16_t a, uint16_t b, unsigned q)
{
    unsigned ar = (a >> 11) & 31u, ag = (a >> 5) & 63u, ab = a & 31u;
    unsigned br = (b >> 11) & 31u, bg = (b >> 5) & 63u, bb = b & 31u;
    if (q > 256u) q = 256u;
    return (uint16_t)((((ar * (256u - q) + br * q) >> 8) << 11) |
        (((ag * (256u - q) + bg * q) >> 8) << 5) |
        ((ab * (256u - q) + bb * q) >> 8));
}

/* Five-column capital alphabet, digits and a few panel punctuation marks. */
static const uint8_t glyphs[42][5] = {
    {0x7e,0x11,0x11,0x11,0x7e}, {0x7f,0x49,0x49,0x49,0x36},
    {0x3e,0x41,0x41,0x41,0x22}, {0x7f,0x41,0x41,0x22,0x1c},
    {0x7f,0x49,0x49,0x49,0x41}, {0x7f,0x09,0x09,0x09,0x01},
    {0x3e,0x41,0x49,0x49,0x7a}, {0x7f,0x08,0x08,0x08,0x7f},
    {0x41,0x41,0x7f,0x41,0x41}, {0x20,0x40,0x41,0x3f,0x01},
    {0x7f,0x08,0x14,0x22,0x41}, {0x7f,0x40,0x40,0x40,0x40},
    {0x7f,0x02,0x0c,0x02,0x7f}, {0x7f,0x04,0x08,0x10,0x7f},
    {0x3e,0x41,0x41,0x41,0x3e}, {0x7f,0x09,0x09,0x09,0x06},
    {0x3e,0x41,0x51,0x21,0x5e}, {0x7f,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7f,0x01,0x01},
    {0x3f,0x40,0x40,0x40,0x3f}, {0x1f,0x20,0x40,0x20,0x1f},
    {0x7f,0x20,0x18,0x20,0x7f}, {0x63,0x14,0x08,0x14,0x63},
    {0x03,0x04,0x78,0x04,0x03}, {0x61,0x51,0x49,0x45,0x43},
    {0x3e,0x51,0x49,0x45,0x3e}, {0x00,0x42,0x7f,0x40,0x00},
    {0x62,0x51,0x49,0x49,0x46}, {0x22,0x41,0x49,0x49,0x36},
    {0x18,0x14,0x12,0x7f,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3e,0x49,0x49,0x49,0x32}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x26,0x49,0x49,0x49,0x3e},
    {0x00,0x00,0x24,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02},
    {0x00,0x00,0x00,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08},
    {0x14,0x7f,0x14,0x7f,0x14},
    {0x41,0x22,0x14,0x08,0x00}
};
static const uint8_t *glyph(char c)
{
    if (c >= 'A' && c <= 'Z') return glyphs[(unsigned)(c - 'A')];
    if (c >= '0' && c <= '9') return glyphs[26u + (unsigned)(c - '0')];
    if (c == ':') return glyphs[36];
    if (c == '/') return glyphs[37];
    if (c == '-') return glyphs[39];
    if (c == '#') return glyphs[40];
    if (c == '>') return glyphs[41];
    return glyphs[38];
}
static void text(uint16_t *p, int x, int y, const char *s, uint16_t color)
{
    for (; *s; s++, x += 6) {
        const uint8_t *g = glyph(*s);
        unsigned a, b;
        for (a = 0; a < 5u; a++)
            for (b = 0; b < 7u; b++)
                if (g[a] & (1u << b)) pixel(p, x + (int)a, y + (int)b, color);
    }
}
static void text2(uint16_t *p, int x, int y, const char *s, uint16_t color)
{
    for (; *s; s++, x += 12) {
        const uint8_t *g = glyph(*s);
        unsigned a, b;
        for (a = 0; a < 5u; a++)
            for (b = 0; b < 7u; b++)
                if (g[a] & (1u << b)) rect(p, x + (int)a * 2, y + (int)b * 2, 2, 2, color);
    }
}

static int clampi(int x, int lo, int hi)
{
    return x < lo ? lo : x > hi ? hi : x;
}
static int pitch_x(uint8_t note)
{
    return 21 + (clampi(note, 36, 96) - 36) * 198 / 60;
}
static void project(int x, int z, int height, int *px, int *py)
{
    int f = 25600 / (100 + z * 160 / 256);
    *px = 120 + (x - 120) * f / 256 + z * 6 / 256;
    *py = 182 - z * 62 / 256 - height * 90 * f / 65536;
}
static void world_line(uint16_t *p, int x0, int z0, int h0,
                       int x1, int z1, int h1, uint16_t color)
{
    int ax, ay, bx, by;
    project(x0, z0, h0, &ax, &ay); project(x1, z1, h1, &bx, &by);
    line(p, ax, ay, bx, by, color);
}
static void corner(uint16_t *p, int x, int y, int dx, int dy)
{
    line(p, x, y, x + dx * 12, y, DIM);
    line(p, x, y, x, y + dy * 11, DIM);
}
static void number(char *out, unsigned value)
{
    char tmp[6]; unsigned n = 0, j;
    do { tmp[n++] = (char)('0' + value % 10u); value /= 10u; } while (value && n < sizeof tmp);
    for (j = 0; j < n; j++) out[j] = tmp[n - 1u - j];
    out[n] = 0;
}
static void midi_name(uint8_t note, char out[5])
{
    static const char *const names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    const char *name = names[note % 12u];
    unsigned i = 0u, octave = note / 12u;
    while (*name) out[i++] = *name++;
    if (octave == 0u) { out[i++] = '-'; out[i++] = '1'; }
    else if (octave <= 10u) {
        unsigned value = octave - 1u;
        if (value >= 10u) out[i++] = '1';
        out[i++] = (char)('0' + value % 10u);
    }
    out[i] = 0;
}

static const char *page_name(uint8_t page)
{
    static const char *const names[] = {
        "HOME", "FX", "SAVE", "ENV", "LFO", "EDIT", "GLO", "SEQ", "ARP", "SCALE"
    };
    return page < sizeof names / sizeof names[0] ? names[page] : "HOME";
}
static const char *scene_name(uint8_t scene)
{
    static const char *const names[] = {"A", "B", "INTRO", "OUTRO"};
    return scene < sizeof names / sizeof names[0] ? names[scene] : "A";
}
static const char *chord_name(const nf_state *s)
{
    static const char *const names[3][4] = {
        {"AM9", "FMAJ7", "CMAJ7", "G6"},
        {"DM9", "BBMAJ7", "FMAJ7", "C6"},
        {"EM9", "CMAJ7", "GMAJ7", "D6"}
    };
    return names[s->preset < 3u ? s->preset : 0u][s->bar % 4u];
}

static void fx_screen(const nf_state *s, uint16_t *pixels)
{
    static const char *const name[NF_FX_COUNT] = {
        "FILTER", "DRIVE", "CHORUS", "PHASER", "DELAY", "REVERB"
    };
    static const char *const labels[NF_FX_COUNT][4] = {
        {"CUTOFF", "RESO", "MOTION", "MIX"},
        {"DRIVE", "TONE", "BODY", "MIX"},
        {"RATE", "DEPTH", "WIDTH", "MIX"},
        {"RATE", "DEPTH", "TONE", "MIX"},
        {"TIME", "FDBK", "TONE", "MIX"},
        {"SIZE", "DAMP", "PRE", "MIX"}
    };
    const unsigned selected = s->selected_fx < NF_FX_COUNT ? s->selected_fx : 0u;
    const nf_fx *f = &s->fx[selected];
    char n[6];
    unsigned i;
    n[0] = '0'; n[1] = (char)('1' + selected); n[2] = 0;
    text(pixels, 8, 70, n, BRIGHT);
    text(pixels, 28, 70, "/", DIM);
    text(pixels, 42, 70, name[selected], BRIGHT);
    text(pixels, f->enabled ? 210 : 190, 70, f->enabled ? "ON" : "BYPASS", f->enabled ? AMBER : DIM);
    rect(pixels, 5, 80, 230, 57, BG);
    for (i = 0u; i < 8u; i++) {
        int x = 15 + (int)i * 30;
        line(pixels, x, 84, x, 135, GRID);
    }
    for (i = 0u; i < 4u; i++) {
        int y = 84 + (int)i * 15;
        line(pixels, 15, y, 225, y, GRID);
    }
    if (selected == NF_FILTER) {
        int x = 24 + f->value[0] * 190 / 127;
        line(pixels, 15, 98, x - 7, 98, AMBER);
        line(pixels, x - 7, 98, x, 95 - f->value[1] / 15, BRIGHT);
        line(pixels, x, 95 - f->value[1] / 15, x + 12, 129, AMBER);
        line(pixels, x + 12, 129, 225, 129, DIM);
        line(pixels, x, 84, x, 135, BRIGHT);
    } else if (selected == NF_DRIVE || selected == NF_CHORUS) {
        int layer;
        for (layer = selected == NF_CHORUS ? -1 : 0; layer <= (selected == NF_CHORUS ? 1 : 0); layer++) {
            int x, prev_y = 108;
            for (x = 15; x <= 225; x += 3) {
                int phase = ((x - 15) * 4 + (int)(s->now_ms / 25u) + layer * 13) % 84;
                int tri = phase < 42 ? phase : 84 - phase;
                int y = 108 + (tri - 21) * (selected == NF_DRIVE ?
                    8 + f->value[0] / 7 : 12) / 21 + layer * (int)f->value[2] / 32;
                y = clampi(y, 86, 132);
                if (x > 15) line(pixels, x - 3, prev_y, x, y,
                                 layer == 0 ? BRIGHT : DIM);
                prev_y = y;
            }
        }
    } else if (selected == NF_PHASER) {
        int x;
        line(pixels, 15, 96, 225, 96, AMBER);
        for (x = 35; x < 220; x += 46) {
            int dx = (int)f->value[1] / 12;
            line(pixels, x - dx, 96, x, 128, BRIGHT);
            line(pixels, x, 128, x + dx, 96, BRIGHT);
        }
    } else if (selected == NF_DELAY) {
        int x, spacing = 18 + f->value[0] / 20;
        for (x = 18, i = 0u; x < 230 && i < 9u; x += spacing, i++) {
            int amp = 38 * (int)(9u - i) / 9;
            line(pixels, x - 3, 131, x, 131 - amp, i ? AMBER : BRIGHT);
            line(pixels, x, 131 - amp, x + 3, 131, i ? AMBER : BRIGHT);
        }
    } else {
        for (i = 0u; i < 5u; i++) {
            int w = 18 + (int)i * (8 + f->value[0] / 40);
            int h = 7 + (int)i * 4;
            line(pixels, 120 - w, 110 - h, 120, 104 - h, i ? AMBER : BRIGHT);
            line(pixels, 120, 104 - h, 120 + w, 110 - h, AMBER);
            line(pixels, 120 + w, 110 - h, 120 + w, 110 + h, DIM);
            line(pixels, 120 + w, 110 + h, 120 - w, 110 + h, DIM);
            line(pixels, 120 - w, 110 + h, 120 - w, 110 - h, DIM);
        }
    }
    for (i = 0u; i < 4u; i++) {
        int x = 8 + (int)i * 57;
        text(pixels, x, 150, labels[selected][i], DIM);
        number(n, f->value[i]);
        text(pixels, x, 163, n, BRIGHT);
        rect(pixels, x, 173, 49, 2, GRID);
        rect(pixels, x, 173, 49 * f->value[i] / 127, 2, AMBER);
    }
    text(pixels, 8, 185, "SELECT:FX / SEL:ON-OFF", DIM);
}

static void scratch_screen(const nf_state *s, uint16_t *pixels)
{
    unsigned i;
    text(pixels, 8, 70, "ARCHIVE / RAM ONLY", BRIGHT);
    text(pixels, 204, 70, s->save_op ? "LOAD" : "SAVE", AMBER);
    rect(pixels, 5, 81, 230, 105, BG);
    for (i = 0; i < NF_PROJECT_SLOTS; i++) {
        int y = 84 + (int)i * 26;
        uint16_t ink = i == s->selected_project ? BRIGHT : AMBER;
        char n[6];
        if (i == s->selected_project) rect(pixels, 8, y - 2, 224, 23, PANEL);
        n[0] = '0'; n[1] = (char)('1' + i); n[2] = 0;
        text(pixels, 14, y + 5, n, ink);
        line(pixels, 36, y, 36, y + 18, GRID);
        text(pixels, 44, y + 5, s->project[i].valid ? "SCRATCH / SET" : "EMPTY", ink);
        text(pixels, 210, y + 5, s->project[i].valid ? "SET" : "---", ink);
    }
    text(pixels, 8, 174, "NO FLASH SAVE YET", BRIGHT);
    text(pixels, 8, 185, "SELECT:SLOT K1:MODE SEL:GO", DIM);
}

static void parameter_row_advanced(uint16_t *pixels, const char *const labels[4],
                                   const uint8_t values[4], const uint8_t maxes[4],
                                   const char *const display[4])
{
    unsigned i;
    char number_buf[6];
    for (i = 0u; i < 4u; i++) {
        int x = 8 + (int)i * 57;
        text(pixels, x, 150, labels[i], DIM);
        number(number_buf, values[i]);
        text(pixels, x, 163, display && display[i] ? display[i] : number_buf, BRIGHT);
        rect(pixels, x, 173, 49, 2, GRID);
        rect(pixels, x, 173, 49 * values[i] / (maxes[i] ? maxes[i] : 1), 2, AMBER);
    }
}

static void parameter_row(uint16_t *pixels, const char *const labels[4],
                          const uint8_t values[4], unsigned max)
{
    const uint8_t maxes[4] = {(uint8_t)max, (uint8_t)max,
                              (uint8_t)max, (uint8_t)max};
    parameter_row_advanced(pixels, labels, values, maxes, NULL);
}

static void graph_grid(uint16_t *pixels)
{
    unsigned i;
    for (i = 0u; i < 8u; i++) {
        int x = 15 + (int)i * 30;
        line(pixels, x, 85, x, 136, GRID);
    }
    for (i = 0u; i < 4u; i++) {
        int y = 87 + (int)i * 15;
        line(pixels, 15, y, 225, y, GRID);
    }
}

static void glo_screen(const nf_state *s, uint16_t *pixels)
{
    static const char *const labels[8] = {
        "1 DRUM", "2 BASS", "3 CHORD", "4 LEAD",
        "5 A RAIN", "6 B CHASE", "7 FILL", "8 AUTO"
    };
    unsigned i;
    text(pixels, 8, 70, "NOW", DIM);
    text(pixels, 33, 70, scene_name(s->scene), BRIGHT);
    text(pixels, 123, 70, "NEXT", DIM);
    text(pixels, 154, 70, s->queued_scene < 2u ?
         scene_name(s->queued_scene) : "--", AMBER);
    text(pixels, 8, 83, s->auto_arrange ? "AUTO ARRANGE / 16 BAR" : "MANUAL / A-B", DIM);
    for (i = 0u; i < 8u; i++) {
        int x = 8 + (int)(i % 2u) * 115;
        int y = 93 + (int)(i / 2u) * 24;
        uint8_t active = i < 4u ? !s->track[i].muted :
            i == 4u ? s->scene == 0u :
            i == 5u ? s->scene == 1u :
            i == 6u ? s->fill_queued || s->fill_active :
            i == 7u ? s->auto_arrange : 0u;
        rect(pixels, x, y, 108, 21, active ? PANEL : BG);
        text(pixels, x + 5, y + 7, labels[i], active ? BRIGHT : DIM);
        if (i < 4u) {
            char n[6];
            number(n, s->track[i].level * 100u / 127u);
            text(pixels, x + 83, y + 7, active ? n : "OFF", active ? AMBER : DIM);
            rect(pixels, x + 5, y + 17, 98 * s->track[i].level / 127, 1,
                 active ? AMBER : GRID);
        } else if (i >= 6u) text(pixels, x + 82, y + 7,
                                active ? "ON" : "OFF", active ? AMBER : DIM);
    }
    text(pixels, 8, 190, "WHITE KEYS 1-8 / K1-4:LEVEL", DIM);
}

static void seq_screen(const nf_state *s, uint16_t *pixels)
{
    const nf_step *focus = &s->track[s->selected_track].steps[s->seq_focus];
    unsigned i, bank = s->seq_focus / 16u;
    char n[6];
    char pitch[5];
    text(pixels, 8, 70, "FOCUS", BRIGHT);
    number(n, s->seq_focus + 1u); text(pixels, 53, 70, n, BRIGHT);
    text(pixels, 153, 70, "PLAY", DIM);
    if (s->playing) { number(n, s->step + 1u); text(pixels, 186, 70, n, AMBER); }
    else text(pixels, 186, 70, "--", DIM);
    for (i = 0u; i < 16u; i++) {
        int x = 9 + (int)(i % 4u) * 57;
        int y = 83 + (int)(i / 4u) * 23;
        unsigned step = bank * 16u + i;
        uint8_t on = s->track[s->selected_track].steps[step].velocity != 0u;
        rect(pixels, x, y, 51, 21, on ? PANEL : BG);
        if (step == s->seq_focus) {
            line(pixels, x - 1, y - 1, x + 51, y - 1, BRIGHT);
            line(pixels, x - 1, y + 21, x + 51, y + 21, BRIGHT);
        }
        number(n, step + 1u);
        text(pixels, x + 18, y + 7, n, on ? BRIGHT : DIM);
        if (s->playing && s->step == step)
            rect(pixels, x + 16, y + 18, 19, 2, BRIGHT);
    }
    if (focus->note || focus->velocity) midi_name(focus->note, pitch);
    else { pitch[0] = '-'; pitch[1] = '-'; pitch[2] = 0; }
    text(pixels, 8, 180, "K1", DIM); text(pixels, 26, 180, pitch, BRIGHT);
    text(pixels, 61, 180, "K2", DIM); number(n, focus->velocity);
    text(pixels, 79, 180, n, BRIGHT);
    text(pixels, 114, 180, "K3", DIM); number(n, focus->length);
    text(pixels, 132, 180, n, BRIGHT);
    text(pixels, 159, 180, "K4", DIM); number(n, focus->chance);
    text(pixels, 177, 180, n, BRIGHT);
    text(pixels, 207, 180, focus->velocity ? "ON" : "OFF",
         focus->velocity ? AMBER : DIM);
    text(pixels, 8, 190, "OCT:BANK  SEL:STEP  KEYS:TOGGLE", DIM);
}

static void env_screen(const nf_state *s, uint16_t *pixels)
{
    static const char *const labels[4] = {"A ATK", "D DEC", "S LVL", "R REL"};
    const uint8_t *v = s->envelope_control[s->selected_track];
    int x1 = 28 + v[0] / 3, x2 = 55 + (v[0] + v[1]) / 3;
    int y2 = 134 - v[2] * 43 / 100;
    unsigned i;
    int32_t level = 0;
    text(pixels, 8, 70, "ENVELOPE / LIVE VOICE", BRIGHT);
    rect(pixels, 5, 82, 230, 57, BG);
    graph_grid(pixels);
    line(pixels, 15, 134, x1, 91, AMBER);
    line(pixels, x1, 91, x2, y2, AMBER);
    line(pixels, x2, y2, 190, y2, AMBER);
    line(pixels, 190, y2, 225, 134, AMBER);
    for (i = 0u; i < NF_VOICES; i++)
        if (s->voice[i].active && s->voice[i].track == s->selected_track &&
            s->voice[i].envelope > level) level = s->voice[i].envelope;
    if (level) {
        int y = 134 - level * 43 / 32767;
        line(pixels, 15, y, 225, y, BRIGHT);
        rect(pixels, 188, y - 2, 4, 4, BRIGHT);
    }
    parameter_row(pixels, labels, v, 100u);
    text(pixels, 8, 185, level ? "CURSOR:LIVE / CURVE:SHAPE" :
         "PLAY A NOTE TO MONITOR", DIM);
}

static void lfo_screen(const nf_state *s, uint16_t *pixels)
{
    static const char *const labels[4] = {"RATE", "DEPTH", "DEST", "SHAPE"};
    static const uint8_t maxes[4] = {7u, 100u, 2u, 3u};
    static const char *const rates[8] = {
        "4BAR", "2BAR", "1BAR", "1/2", "1/4", "1/8", "1/16", "1/32"
    };
    static const char *const dest[3] = {"TONE", "AMP", "PITCH"};
    static const char *const shapes[4] = {"SINE", "TRI", "SQR", "SAW"};
    const uint8_t *v = s->lfo_control;
    const char *display[4] = {
        rates[v[0] < 8u ? v[0] : 0u], NULL,
        dest[v[2] < 3u ? v[2] : 0u], shapes[v[3] < 4u ? v[3] : 0u]
    };
    int x, prior = 111;
    text(pixels, 8, 70, "LFO / CONTROL SHAPE", BRIGHT);
    rect(pixels, 5, 82, 230, 57, BG);
    graph_grid(pixels);
    line(pixels, 15, 111, 225, 111, DIM);
    for (x = 15; x <= 225; x += 3) {
        int phase = ((x - 15) * (int)(v[0] + 2u) +
                     (int)(s->now_ms / 15u)) % 84;
        int tri = phase < 42 ? phase : 84 - phase;
        int shape = v[3] < 4u ? v[3] : 0u;
        int wave = shape == 2 ? (phase < 42 ? 21 : -21) :
                   shape == 3 ? phase / 2 - 21 : tri - 21;
        if (shape == 0) {
            int magnitude = wave < 0 ? -wave : wave;
            wave = wave * (42 - magnitude) / 21;
        }
        int y = 111 - wave * (int)v[1] / 100;
        if (x > 15) line(pixels, x - 3, prior, x, y, AMBER);
        prior = y;
    }
    parameter_row_advanced(pixels, labels, v, maxes, display);
    text(pixels, 8, 185, "BPM SYNC / K3:TARGET", DIM);
}

static void edit_screen(const nf_state *s, uint16_t *pixels)
{
    static const char *const labels[4] = {"WAVE", "SPREAD", "OCT", "LEVEL"};
    static const uint8_t maxes[4] = {4u, 100u, 4u, 100u};
    static const char *const waves[5] = {"SINE", "TRI", "SAW", "SQR", "METAL"};
    static const char *const octaves[5] = {"-2", "-1", "0", "+1", "+2"};
    const uint8_t *v = s->voice_control[s->selected_track];
    const char *display[4] = {
        waves[v[0] < 5u ? v[0] : 0u], NULL,
        octaves[v[2] < 5u ? v[2] : 2u], NULL
    };
    int x, prior = 108;
    text(pixels, 8, 70, nf_patch_name(s->preset, s->selected_track), BRIGHT);
    text(pixels, 176, 70, "EDIT", DIM);
    rect(pixels, 5, 82, 230, 57, BG);
    graph_grid(pixels);
    for (x = 15; x <= 225; x += 2) {
        int phase = ((x - 15) * 4 + (int)(s->now_ms / 30u)) % 84;
        int tri = phase < 42 ? phase : 84 - phase;
        int y = v[0] == 4u ? 108 + ((tri - 21) *
            (phase < 42 ? 18 : -18) / 21) :
            v[0] == 3u ? (phase < 42 ? 91 : 125) :
            v[0] == 2u ? 92 + phase * 34 / 84 :
            108 + (tri - 21) * 17 / 21;
        if (x > 15) line(pixels, x - 2, prior, x, y, AMBER);
        prior = y;
    }
    parameter_row_advanced(pixels, labels, v, maxes, display);
    text(pixels, 8, 185, "VOICE SHAPE / SELECT TRACK", DIM);
}

static void arp_screen(const nf_state *s, uint16_t *pixels)
{
    static const char *const labels[4] = {"ORDER", "DIV", "GATE", "OCT"};
    static const uint8_t maxes[4] = {3u, 4u, 100u, 2u};
    static const char *const orders[4] = {"UP", "DOWN", "UP/DN", "RANDOM"};
    static const char *const divisions[5] = {"1/4", "1/8", "1/16", "1/32", "1/64"};
    static const char *const octaves[3] = {"0", "+1", "+2"};
    const uint8_t *v = s->arp_control;
    const char *display[4] = {
        orders[v[0] < 4u ? v[0] : 0u], divisions[v[1] < 5u ? v[1] : 0u],
        NULL, octaves[v[3] < 3u ? v[3] : 0u]
    };
    unsigned i;
    int prior_x = 0, prior_y = 0;
    text(pixels, 8, 70, s->arp_enabled ? "ARP READY" : "ARP OFF", BRIGHT);
    rect(pixels, 5, 82, 230, 57, BG);
    graph_grid(pixels);
    for (i = 0u; i < 8u; i++) {
        unsigned degree = v[0] == 1u ? 3u - i % 4u :
                          v[0] == 2u ? (i % 6u < 4u ? i % 6u : 6u - i % 6u) :
                          v[0] == 3u ? (i * 5u + 1u) % 4u : i % 4u;
        int x = 21 + (int)i * 28, y = 126 - (int)degree * 11;
        uint16_t ink = s->arp_enabled ? AMBER : DIM;
        if (prior_x) line(pixels, prior_x, prior_y, x, y, ink);
        rect(pixels, x - 2, y - 2, 5, 5,
             s->arp_position == i && s->arp_enabled ? BRIGHT : ink);
        line(pixels, x - 2, 134, x - 2 + 22 * (int)v[2] / 100, 134,
             s->arp_position == i && s->arp_enabled ? BRIGHT : DIM);
        prior_x = x; prior_y = y;
    }
    parameter_row_advanced(pixels, labels, v, maxes, display);
    text(pixels, 8, 185, "ORDER PREVIEW / SEL ON-OFF", DIM);
}

static void scale_screen(const nf_state *s, uint16_t *pixels)
{
    static const uint8_t minor[12] = {1,0,1,1,0,1,0,1,1,0,1,0};
    unsigned i;
    text(pixels, 8, 70, s->scale == 0u ? "CHROMATIC" :
         s->scale == 1u ? "SCALE LOCK" : "ONE KEY CHORD", BRIGHT);
    rect(pixels, 5, 82, 230, 90, BG);
    for (i = 0u; i < 12u; i++) {
        unsigned member = s->scale == 0u || minor[(i + 12u -
            (unsigned)(s->preset == 0u ? 9u : s->preset == 1u ? 2u : 4u)) % 12u];
        rect(pixels, 12 + (int)i * 18, member ? 105 : 118, 12,
             member ? 30 : 17, member ? AMBER : GRID);
    }
    text(pixels, 8, 174, "SEL:CYCLE SCALE MODE", DIM);
    text(pixels, 8, 185, s->scale == 2u ? "KEYS:MINOR TRIAD" :
         s->scale == 1u ? "KEYS SNAP TO SCALE" : "FREE CHROMATIC KEYBOARD", DIM);
}

static void home_city(const nf_state *s, uint16_t pixels[NF_WIDTH * NF_HEIGHT])
{
    unsigned i, pass, any = 0, held = 0, midi_held = 0;
    uint32_t scans[3] = {0, 0, 0};
    text(pixels, 8, 64, "LIVE CITY / PRE MASTER", BRIGHT);
    text(pixels, 176, 64, "SEL:SCALE", DIM);
    rect(pixels, 5, 73, 230, 111, BG);
    for (i = 0; i <= 6u; i++) {
        int z = (int)i * 256 / 6;
        world_line(pixels, 15, z, 0, 225, z, 0, CITY_GRID);
    }
    for (i = 0; i <= 5u; i++) {
        int x = 21 + (int)i * 198 / 5;
        world_line(pixels, x, 0, 0, x, 256, 0, CITY_GRID);
        world_line(pixels, x, 0, 0, x, 0, 12, CITY_GRID);
    }
    corner(pixels, 7, 74, 1, 1); corner(pixels, 232, 74, -1, 1);
    corner(pixels, 7, 183, 1, -1); corner(pixels, 232, 183, -1, -1);
    for (i = 0; i < NF_EVENTS; i++) {
        const nf_event *e = &s->event[i];
        uint32_t age;
        unsigned band;
        if (!e->serial || (!e->live && s->now_ms - e->ended_ms >= 3000u)) continue;
        any = 1u; if (e->live) held = 1u;
        if (e->live && (e->source == NF_MIDI || e->source == NF_WEB)) midi_held = 1u;
        age = s->now_ms - e->onset_ms;
        if (age >= 840u || (e->live && s->track[e->track].muted &&
                            e->source == NF_SEQ)) continue;
        band = age / 280u;
        if (band < 3u && (!scans[band] || e->onset_ms + 1u > scans[band]))
            scans[band] = e->onset_ms + 1u;
    }
    for (i = 0; i < 3u; i++) if (scans[i]) {
        unsigned dt = s->now_ms - (scans[i] - 1u);
        int z = (int)(dt * 256u / 840u);
        uint16_t ink = blend(BG, BRIGHT, 112u - dt * 90u / 840u);
        int slice;
        /* Sparse horizontal returns read as a passing optical plane without
         * filling the scene or obscuring the pitch/level data behind it. */
        for (slice = 75; slice < 315; slice += 60)
            world_line(pixels, 15, z, slice, 225, z, slice,
                       blend(BG, AMBER, 28u - dt * 20u / 840u));
        world_line(pixels, 15, z, 0, 225, z, 0, ink);
        world_line(pixels, 15, z, 0, 15, z, 315, ink);
        world_line(pixels, 225, z, 0, 225, z, 315, ink);
    }
    for (pass = 0; pass < 2u; pass++) for (i = 0; i < NF_EVENTS; i++) {
        const nf_event *e = &s->event[i];
        uint32_t age;
        int x, z, w, h, back, level, env;
        uint16_t ink;
        if (!e->serial || e->live != pass) continue;
        age = e->live ? 0u : s->now_ms - e->ended_ms;
        if (age >= 3000u) continue;
        z = e->live ? e->source == NF_KEY ? 5 : e->source == NF_SEQ ? 59 : 12 :
            (int)(12u + age * 244u / 3000u);
        x = pitch_x(e->note);
        w = e->source == NF_KEY ? 6 : e->source == NF_SEQ ? 4 : 3;
        h = 26 + ((int)e->note - 24) * 4;
        h = clampi(h, 20, 310);
        back = clampi(z + 9, 0, 256);
        env = e->live && (!s->track[e->track].muted || e->source != NF_SEQ) ?
            e->envelope * s->track[e->track].level / 127 : 0;
        ink = e->live ? e->source == NF_KEY ? BRIGHT : AMBER :
            blend(BG, AMBER, (3000u - age) * 115u / 3000u);
        world_line(pixels, x - w, z, 0, x - w, z, h, ink);
        world_line(pixels, x - w, z, h, x + w, z, h, ink);
        world_line(pixels, x + w, z, h, x + w, z, 0, ink);
        world_line(pixels, x - w, z, h, x - w, back, h, ink);
        world_line(pixels, x - w, back, h, x + w, back, h, ink);
        world_line(pixels, x + w, back, h, x + w, z, h, ink);
        world_line(pixels, x - w, back, h, x - w, back, 0, DIM);
        world_line(pixels, x + w, back, h, x + w, back, 0, DIM);
        world_line(pixels, x - w, z, 0, x - w, back, 0, GRID);
        world_line(pixels, x + w, z, 0, x + w, back, 0, GRID);
        if (e->live && h > 110) {
            int floor;
            for (floor = h / 3; floor < h; floor += h / 3)
                world_line(pixels, x - w, z, floor, x + w, z, floor,
                           blend(BG, ink, 76u));
        }
        if (env) {
            level = h * env / 255;
            world_line(pixels, x - w, z, level, x + w, z, level, BRIGHT);
            world_line(pixels, x - w + 1, z, 2, x - w + 1, z, level,
                       blend(BG, AMBER, (unsigned)env));
            world_line(pixels, x + w - 1, z, 2, x + w - 1, z, level,
                       blend(BG, AMBER, (unsigned)env));
            if (e->source == NF_KEY) world_line(pixels, x - 2, 0, 0, x + 2, 0, 0, BRIGHT);
        }
        if (e->note < 36u || e->note > 96u) {
            int px, py, dir = e->note < 36u ? -1 : 1;
            project(x, z, h, &px, &py);
            line(pixels, px - dir * 2, py - 3, px + dir * 2, py, ink);
            line(pixels, px + dir * 2, py, px - dir * 2, py + 3, ink);
        }
    }
    for (i = 74u; i < 184u; i += 3u)
        for (unsigned x = 5u; x < 235u; x++)
            pixels[i * NF_WIDTH + x] = blend(pixels[i * NF_WIDTH + x], BG, 24u);
    text(pixels, 8, 185, any ? held ? midi_held ? "MIDI LIVE / PITCH + LEVEL" :
        "LIVE VOICES / PITCH + LEVEL" :
        "RECENT VOICES / 3 SECONDS" : "PLAY KEYS / K1-4:MACROS", any ? AMBER : DIM);
}

static void home_terrain(const nf_state *s, uint16_t *pixels)
{
    unsigned row, column, peak = 0u;
    unsigned phase = s->now_ms - s->terrain_last_ms;
    if (phase > 70u) phase = 70u;
    phase = phase * 256u / 70u;
    text(pixels, 8, 64, "PRE MASTER / SPECTRUM", BRIGHT);
    text(pixels, 207, 64, "20F", DIM);
    rect(pixels, 5, 73, 230, 111, BG);
    for (row = 0u; row <= 8u; row++) {
        int z = (int)row * 256 / 8;
        world_line(pixels, 15, z, 0, 225, z, 0, CITY_GRID);
    }
    for (column = 0u; column <= 5u; column++) {
        int x = 15 + (int)column * 210 / 5;
        world_line(pixels, x, 0, 0, x, 256, 0, CITY_GRID);
    }
    corner(pixels, 7, 74, 1, 1); corner(pixels, 232, 74, -1, 1);
    corner(pixels, 7, 183, 1, -1); corner(pixels, 232, 183, -1, -1);
    for (row = NF_TERRAIN_ROWS; row-- > 0u;) {
        const uint8_t *current = s->terrain[(s->terrain_head +
            NF_TERRAIN_ROWS - row) % NF_TERRAIN_ROWS];
        const uint8_t *older = s->terrain[(s->terrain_head +
            NF_TERRAIN_ROWS - row - 1u) % NF_TERRAIN_ROWS];
        int z = (int)row * 256 / (NF_TERRAIN_ROWS - 1u);
        int next_z = (int)(row + 1u) * 256 / (NF_TERRAIN_ROWS - 1u);
        uint16_t ink = row ? blend(BG, BRIGHT, 190u - row * 7u) : BRIGHT;
        for (column = 0u; column < NF_TERRAIN_BANDS; column++) {
            int x = 15 + (int)column * 210 / (NF_TERRAIN_BANDS - 1u);
            int h = (int)((current[column] * phase +
                older[column] * (256u - phase)) >> 8) * 150 / 255;
            if (column) {
                int prev_x = 15 + (int)(column - 1u) * 210 /
                    (NF_TERRAIN_BANDS - 1u);
                int prev_h = (int)((current[column - 1u] * phase +
                    older[column - 1u] * (256u - phase)) >> 8) * 150 / 255;
                world_line(pixels, prev_x, z, prev_h, x, z, h, ink);
            }
            if (row + 1u < NF_TERRAIN_ROWS && !(column & 1u))
                world_line(pixels, x, z, h, x, next_z,
                    (int)older[column] * 150 / 255,
                    blend(BG, ink, 90u));
        }
    }
    for (column = 1u; column < NF_TERRAIN_BANDS; column++) {
        unsigned previous = (s->terrain_head + NF_TERRAIN_ROWS - 1u) % NF_TERRAIN_ROWS;
        unsigned level = (s->terrain[s->terrain_head][column] * phase +
            s->terrain[previous][column] * (256u - phase)) >> 8;
        unsigned best = (s->terrain[s->terrain_head][peak] * phase +
            s->terrain[previous][peak] * (256u - phase)) >> 8;
        if (level > best) peak = column;
    }
    {
        unsigned previous = (s->terrain_head + NF_TERRAIN_ROWS - 1u) % NF_TERRAIN_ROWS;
        unsigned level = (s->terrain[s->terrain_head][peak] * phase +
            s->terrain[previous][peak] * (256u - phase)) >> 8;
        if (level > 12u) {
            int x, y;
            project(15 + (int)peak * 210 / (NF_TERRAIN_BANDS - 1u), 0,
                (int)level * 150 / 255, &x, &y);
            line(pixels, x - 4, y - 4, x - 4, y + 3, BRIGHT);
            line(pixels, x + 4, y - 4, x + 4, y + 3, BRIGHT);
        }
    }
    for (row = 74u; row < 184u; row += 3u)
        for (column = 5u; column < 235u; column++)
            pixels[row * NF_WIDTH + column] =
                blend(pixels[row * NF_WIDTH + column], BG, 24u);
    text(pixels, 8, 185, "20B AUDIO / HOME:CITY", DIM);
}

void nf_draw_city(const nf_state *s, uint16_t pixels[NF_WIDTH * NF_HEIGHT])
{
    static const char *const names[NF_TRACKS] = {"DRUM", "BASS", "CHORD", "LEAD"};
    uint8_t live_notes[3] = {0u, 0u, 0u};
    unsigned live_count = 0u;
    unsigned i;
    char n[6];
    rect(pixels, 0, 0, NF_WIDTH, NF_HEIGHT, BG);
    text(pixels, 8, 7, "NIGHTFALL", AMBER);
    text(pixels, s->recording ? 207 : 195, 7,
         s->recording ? "REC" : s->playing ? "RUN" : "READY",
         s->recording ? BRIGHT : AMBER);
    line(pixels, 8, 21, 232, 21, GRID);
    for (i = 0u; i < NF_EVENTS; i++) {
        const nf_event *e = &s->event[i];
        if (e->live && e->source != NF_SEQ && e->track < NF_TRACKS &&
            s->track[e->track].instrument != NF_DRUM) {
            unsigned at = 0u;
            while (at < live_count && live_notes[at] < e->note) at++;
            if (at < live_count && live_notes[at] == e->note) continue;
            if (at < 3u) {
                unsigned j = live_count < 3u ? live_count : 2u;
                while (j > at) { live_notes[j] = live_notes[j - 1u]; j--; }
                live_notes[at] = e->note;
                if (live_count < 3u) live_count++;
            }
        }
    }
    if (live_count) {
        char title[16];
        unsigned used = 0u;
        for (i = 0u; i < live_count; i++) {
            char label[5];
            unsigned j = 0u;
            midi_name(live_notes[i], label);
            while (label[j]) j++;
            if (used + j + (used != 0u) > 12u) break;
            if (used) title[used++] = ' ';
            for (j = 0u; label[j]; j++) title[used++] = label[j];
        }
        title[used] = 0;
        text2(pixels, 8, 27, title, BRIGHT);
    } else text2(pixels, 8, 27, chord_name(s), BRIGHT);
    number(n, s->tempo);
    text(pixels, 176, 32, n, AMBER);
    text(pixels, 206, 32, "BPM", DIM);
    text(pixels, 8, 53, names[s->selected_track < NF_TRACKS ?
         s->selected_track : 0u], DIM);
    text(pixels, 45, 53, "/", DIM);
    text(pixels, 57, 53, live_count ? "LIVE NOTES" : page_name(s->page), DIM);
    if (s->page == NF_FX_PAGE) fx_screen(s, pixels);
    else if (s->page == NF_SAVE_PAGE) scratch_screen(s, pixels);
    else if (s->page == NF_ENV_PAGE) env_screen(s, pixels);
    else if (s->page == NF_LFO_PAGE) lfo_screen(s, pixels);
    else if (s->page == NF_EDIT_PAGE) edit_screen(s, pixels);
    else if (s->page == NF_GLO_PAGE) glo_screen(s, pixels);
    else if (s->page == NF_SEQ_PAGE) seq_screen(s, pixels);
    else if (s->page == NF_ARP_PAGE) arp_screen(s, pixels);
    else if (s->page == NF_SCALE_PAGE) scale_screen(s, pixels);
    else if (s->home_view) home_terrain(s, pixels);
    else home_city(s, pixels);
    if (s->ui_notice[0] && (int32_t)(s->ui_notice_until_ms - s->now_ms) > 0) {
        rect(pixels, 5, 180, 230, 15, BG);
        text(pixels, 8, 185, s->ui_notice, BRIGHT);
    }
    line(pixels, 8, 199, 232, 199, GRID);
    text(pixels, 8, 204, scene_name(s->scene), AMBER);
    if (s->queued_scene < 2u) {
        text(pixels, 25, 204, ">", DIM);
        text(pixels, 38, 204, scene_name(s->queued_scene), BRIGHT);
    }
    text(pixels, 168, 204, "BAR", DIM);
    number(n, s->bar + 1u);
    text(pixels, 196, 204, n, AMBER);
    for (i = 0u; i < 16u; i++)
        rect(pixels, 8 + (int)i * 14, 215, 10, 3,
             s->playing && s->step % 16u == i ? BRIGHT :
             i % 4u == 0u ? GRID : PANEL);
    for (i = 0u; i < NF_TRACKS; i++)
        text(pixels, 8 + (int)i * 57, 226, names[i],
             s->track[i].muted ? DIM : i == s->selected_track ? BRIGHT : AMBER);
}
