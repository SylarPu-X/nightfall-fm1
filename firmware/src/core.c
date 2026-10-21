#include "nightfall.h"
#include "nf_libc.h"

static uint32_t pitch_increment(uint8_t note)
{
    static const uint32_t ratio_q16[12] = {
        65536u, 69433u, 73561u, 77936u, 82570u, 87480u,
        92682u, 98199u, 104039u, 110227u, 116782u, 123728u
    };
    int n = (int)note - 69, octave = n / 12, semitone = n % 12;
    uint64_t inc = 440ull * 4294967296ull / NF_SAMPLE_RATE;
    if (semitone < 0) { semitone += 12; octave--; }
    inc = inc * ratio_q16[semitone] / 65536u;
    if (octave < 0) inc >>= (unsigned)(-octave);
    else inc <<= (unsigned)octave;
    return inc > UINT32_MAX ? UINT32_MAX : (uint32_t)inc;
}

static nf_event *event_for(nf_state *s, uint32_t serial)
{
    unsigned i;
    for (i = 0; i < NF_EVENTS; i++)
        if (s->event[i].live && s->event[i].serial == serial) return &s->event[i];
    return NULL;
}

/* Live playing must win over the backing track. A sequencer trigger may be
 * dropped under saturation, but it must never steal a held keyboard voice. */
static unsigned voice_priority(const nf_voice *v)
{
    if (!v->gate) return 0u;
    return v->source == NF_SEQ ? 1u :
           v->source == NF_MIDI || v->source == NF_WEB ? 2u : 3u;
}

static const uint8_t preset_bass[3][4] = {
    {33u, 29u, 36u, 31u}, {38u, 34u, 29u, 36u},
    {40u, 36u, 31u, 38u}
};
static const uint8_t preset_chord[3][4][4] = {
    {{57u,60u,67u,71u},{53u,57u,60u,64u},
     {55u,59u,60u,64u},{55u,59u,62u,64u}},
    {{62u,65u,72u,76u},{58u,62u,65u,69u},
     {57u,60u,64u,65u},{60u,64u,67u,69u}},
    {{64u,67u,74u,78u},{60u,64u,67u,71u},
     {59u,62u,66u,67u},{62u,66u,69u,71u}}
};

/* Original, sample-free sound designs. They are compact enough to live in
 * program flash and never depend on copyrighted soundtrack recordings. */
typedef struct {
    const char *name;
    uint8_t voice[4], envelope[4], level, send;
} nf_sound_patch;
static const nf_sound_patch sound_bank[3][NF_TRACKS] = {
    {
        {"TUNNEL NOISE", {0,0,2,84}, {0,35,0,26}, 86,18},
        {"SHADOW PULSE", {2,3,2,94}, {2,42,75,38}, 78,25},
        {"NOIR BRASS", {2,16,2,82}, {38,62,78,72}, 68,62},
        {"GLASS SIGNAL", {1,4,2,88}, {5,38,56,48}, 78,72}
    }, {
        {"MOTOR GRID", {0,0,2,92}, {0,22,0,20}, 91,12},
        {"VOLT SAW", {1,6,2,96}, {2,28,79,26}, 83,24},
        {"CHROME PAD", {2,24,2,78}, {54,71,80,77}, 64,52},
        {"LASER EDGE", {3,5,2,89}, {1,24,50,25}, 80,66}
    }, {
        {"DISTANT STATIC", {0,0,2,74}, {0,49,0,44}, 72,45},
        {"WET SUB", {0,0,2,91}, {10,53,82,68}, 79,30},
        {"FOG CHOIR", {0,31,2,79}, {70,80,87,90}, 65,76},
        {"RAIN BELL", {4,2,2,86}, {1,45,0,55}, 75,82}
    }
};

const char *nf_patch_name(unsigned preset, unsigned track)
{
    return preset < 3u && track < NF_TRACKS ?
        sound_bank[preset][track].name : "UNKNOWN";
}

int nf_load_preset(nf_state *s, unsigned preset)
{
    static const uint16_t tempo[3] = {96u, 112u, 84u};
    static const uint8_t macros[3][4] = {
        {55u,45u,65u,35u}, {76u,73u,43u,62u}, {39u,32u,83u,27u}
    };
    unsigned t, i;
    if (!s || preset >= 3u || s->playing || s->recording) return -1;
    s->preset = (uint8_t)preset;
    s->tempo = tempo[preset];
    s->pattern_length = 16u;
    s->step = s->bar = 0u;
    s->scene = 0u;
    s->queued_scene = 0xffu;
    s->arp_position = 0xffu;
    s->fill_queued = s->fill_active = 0u;
    for (i = 0u; i < 4u; i++) s->macros[i] = macros[preset][i];
    s->fx[NF_FILTER].enabled = preset == 1u;
    s->fx[NF_DRIVE].enabled = preset == 1u;
    s->fx[NF_CHORUS].enabled = preset != 0u;
    s->fx[NF_PHASER].enabled = 0u;
    s->fx[NF_DELAY].enabled = 1u;
    s->fx[NF_REVERB].enabled = 1u;
    s->fx[NF_DELAY].value[3] = (uint8_t)(s->macros[2] * 54u / 100u);
    s->fx[NF_REVERB].value[3] = (uint8_t)(s->macros[2] * 62u / 100u);
    for (t = 0; t < NF_TRACKS; t++) {
        const nf_sound_patch *patch = &sound_bank[preset][t];
        s->edited[t] = 0u;
        s->track[t].level = patch->level;
        s->track[t].delay_send = patch->send;
        for (i = 0u; i < 4u; i++) {
            s->voice_control[t][i] = patch->voice[i];
            s->envelope_control[t][i] = patch->envelope[i];
        }
        for (i = 0; i < NF_STEPS; i++)
            s->track[t].steps[i] = (nf_step){0u, 0u, 0u, 100u};
    }
    for (i = 0; i < 16u; i++) {
        if (!(i & 1u))
            s->track[0].steps[i] = (nf_step){i % 8u == 0u ? 36u :
                i % 8u == 4u ? 38u : 42u, 110u, 1u, 100u};
        if (i == 0u || i == 6u || i == 8u || i == 14u)
            s->track[1].steps[i] = (nf_step){preset_bass[preset][0], 102u, 2u, 100u};
        if (i == 0u)
            s->track[2].steps[i] = (nf_step){preset_chord[preset][0][0], 78u, 12u, 100u};
        if (!(i & 1u))
            s->track[3].steps[i] = (nf_step){
                (uint8_t)(preset_chord[preset][0][2] + 12u), 78u, 1u, 100u};
    }
    return 0;
}

void nf_init(nf_state *s)
{
    unsigned i, j;
    static const uint8_t instrument[NF_TRACKS] = {NF_DRUM, NF_BASS, NF_PAD, NF_LEAD};
    static const uint8_t level[NF_TRACKS] = {80u, 75u, 66u, 75u};
    static const uint8_t voice[NF_TRACKS][4] = {
        {0u, 0u, 2u, 80u}, {2u, 2u, 2u, 90u},
        {2u, 8u, 2u, 72u}, {1u, 0u, 2u, 82u}
    };
    memset(s, 0, sizeof *s);
    s->tempo = 96u;
    s->pattern_length = 16u;
    s->random = 0x91e10da5u;
    s->selected_track = 3u;
    s->selected_fx = NF_DELAY;
    s->queued_scene = 0xffu;
    s->auto_arrange = s->arp_enabled = s->scale = 1u;
    s->macros[0] = 55u; s->macros[1] = 45u;
    s->macros[2] = 65u; s->macros[3] = 35u;
    s->lfo_control[0] = 3u; s->lfo_control[1] = 35u;
    s->arp_control[1] = 1u; s->arp_control[2] = 65u;
    s->fx[NF_FILTER] = (nf_fx){0u, {68u, 20u, 0u, 100u}};
    s->fx[NF_DRIVE] = (nf_fx){0u, {25u, 65u, 65u, 45u}};
    s->fx[NF_CHORUS] = (nf_fx){0u, {28u, 45u, 85u, 45u}};
    s->fx[NF_PHASER] = (nf_fx){0u, {25u, 55u, 50u, 65u}};
    s->fx[NF_DELAY] = (nf_fx){1u, {57u, 38u, 48u, 35u}};
    s->fx[NF_REVERB] = (nf_fx){1u, {65u, 50u, 25u, 40u}};
    for (i = 0; i < NF_TRACKS; i++) {
        nf_track *t = &s->track[i];
        t->instrument = instrument[i];
        t->level = level[i];
        t->delay_send = i == 3u ? 0u : 32u;
        for (j = 0; j < 4u; j++) s->voice_control[i][j] = voice[i][j];
        s->envelope_control[i][0] = 18u;
        s->envelope_control[i][1] = 58u;
        s->envelope_control[i][2] = 70u;
        s->envelope_control[i][3] = 45u;
        for (j = 0; j < NF_STEPS; j++) t->steps[j].chance = 100u;
    }
}

void nf_set_macro(nf_state *s, unsigned index, uint8_t value)
{
    if (!s || index >= 4u || value > 100u) return;
    s->macros[index] = value;
    if (index == 2u) {
        s->fx[NF_DELAY].value[3] = (uint8_t)(value * 54u / 100u);
        s->fx[NF_REVERB].value[3] = (uint8_t)(value * 62u / 100u);
    }
}

uint32_t nf_note_on_id(nf_state *s, uint8_t track, uint8_t note,
                       uint8_t velocity, nf_source source)
{
    nf_voice *v = NULL;
    nf_event *e;
    unsigned i;
    if (!s || track >= NF_TRACKS || note > 127u || velocity == 0u || source > NF_WEB) return 0u;
    if (source == NF_KEY && s->recording) {
        nf_step *step = &s->track[track].steps[s->step];
        *step = (nf_step){note, velocity, 1u, 100u};
        s->edited[track] = 1u;
        if (!s->playing)
            s->step = (uint16_t)((s->step + 1u) % (s->pattern_length ? s->pattern_length : 16u));
    }
    for (i = 0; i < NF_VOICES; i++)
        if (!s->voice[i].active) { v = &s->voice[i]; break; }
    if (!v) {
        unsigned best_priority;
        v = &s->voice[0];
        best_priority = voice_priority(v);
        for (i = 1; i < NF_VOICES; i++) {
            unsigned priority = voice_priority(&s->voice[i]);
            if (priority < best_priority ||
                (priority == best_priority &&
                 (s->voice[i].envelope < v->envelope ||
                  (s->voice[i].envelope == v->envelope && s->voice[i].serial < v->serial)))) {
                v = &s->voice[i];
                best_priority = priority;
            }
        }
        if (source == NF_SEQ && best_priority > 1u) return 0u;
        e = event_for(s, v->serial);
        if (e) { e->live = 0u; e->ended_ms = s->now_ms; }
    }
    memset(v, 0, sizeof *v);
    v->active = v->gate = 1u;
    v->track = track; v->note = note; v->velocity = velocity; v->source = (uint8_t)source;
    v->increment = pitch_increment(note);
    v->serial = ++s->serial;
    v->noise = 0x9e3779b9u ^ (v->serial * 1664525u);
    e = &s->event[s->next_event];
    s->next_event = (uint8_t)((s->next_event + 1u) % NF_EVENTS);
    /* Never discard an active event without releasing its corresponding voice. */
    if (e->live) {
        for (i = 0; i < NF_VOICES; i++)
            if (s->voice[i].serial == e->serial) s->voice[i].gate = 0u;
    }
    *e = (nf_event){ .serial = v->serial, .onset_ms = s->now_ms,
        .live = 1u, .note = note, .velocity = velocity, .track = track,
        .source = (uint8_t)source };
    return v->serial;
}

void nf_note_on(nf_state *s, uint8_t track, uint8_t note, uint8_t velocity, nf_source source)
{
    (void)nf_note_on_id(s, track, note, velocity, source);
}

void nf_note_off_id(nf_state *s, uint32_t serial)
{
    unsigned i;
    if (!s || !serial) return;
    for (i = 0u; i < NF_VOICES; i++)
        if (s->voice[i].active && s->voice[i].serial == serial) {
            s->voice[i].gate = 0u;
            return;
        }
}

void nf_note_off(nf_state *s, uint8_t track, uint8_t note, nf_source source)
{
    unsigned i;
    nf_voice *oldest = NULL;
    if (!s) return;
    for (i = 0; i < NF_VOICES; i++) {
        nf_voice *v = &s->voice[i];
        if (v->active && v->gate && v->track == track && v->note == note &&
            v->source == source && (!oldest || v->serial < oldest->serial))
            oldest = v;
    }
    if (oldest) oldest->gate = 0u;
}

void nf_toggle_play(nf_state *s)
{
    unsigned i;
    if (!s->playing) {
        s->playing = 1u;
        s->bar = 0u;
        s->transport_started = 0u;
        s->step = (uint16_t)((s->pattern_length ? s->pattern_length : 16u) - 1u);
        s->step_clock = 60000u - (uint32_t)s->tempo * 4u;
        return;
    }
    s->playing = 0u;
    s->transport_started = 0u;
    s->step_clock = 0u;
    for (i = 0; i < NF_VOICES; i++)
        if (s->voice[i].active && s->voice[i].source == NF_SEQ) s->voice[i].gate = 0u;
}

static void fire_seq_note(nf_state *s, unsigned track, uint8_t note,
                          uint8_t velocity, uint32_t duration_ms)
{
    nf_event *e;
    uint32_t serial = nf_note_on_id(s, (uint8_t)track, note, velocity, NF_SEQ);
    e = event_for(s, serial);
    if (e) e->duration_ms = duration_ms;
}

void nf_tick_ms(nf_state *s, uint32_t milliseconds)
{
    uint32_t m;
    for (m = 0; m < milliseconds; m++) {
        unsigned i;
        s->now_ms++;
        for (i = 0; i < NF_EVENTS; i++)
            if (s->event[i].live && s->event[i].source == NF_SEQ &&
                s->event[i].duration_ms &&
                s->now_ms - s->event[i].onset_ms >= s->event[i].duration_ms) {
                nf_note_off_id(s, s->event[i].serial);
                s->event[i].duration_ms = 0u;
            }
        if (!s->playing || s->tempo < 30u) continue;
        s->step_clock += s->tempo * 4u;
        if (s->step_clock < 60000u) continue;
        s->step_clock -= 60000u;
        s->step = (uint16_t)((s->step + 1u) % (s->pattern_length ? s->pattern_length : 16u));
        if (s->step % 16u == 0u) {
            if (s->transport_started) s->bar++;
            s->transport_started = 1u;
            s->fill_active = s->fill_queued;
            s->fill_queued = 0u;
            if (s->queued_scene < 2u) {
                s->scene = s->queued_scene;
                s->queued_scene = 0xffu;
            }
            if (s->auto_arrange) {
                if (s->bar >= 16u) { nf_toggle_play(s); continue; }
                s->scene = s->bar < 2u ? 2u : s->bar < 8u ? 0u :
                    s->bar < 14u ? 1u : 3u;
            }
        }
        for (i = 0; i < NF_TRACKS; i++) {
            const nf_step *step = &s->track[i].steps[s->step];
            uint32_t duration_ms;
            uint8_t note = step->note, velocity = step->velocity, fire;
            unsigned chord_index;
            s->random ^= s->random << 13;
            s->random ^= s->random >> 17;
            s->random ^= s->random << 5;
            fire = !s->track[i].muted && step->velocity &&
                (step->chance >= 100u || s->random % 100u < step->chance);
            if (i == 3u && !s->edited[i] && s->track[i].steps[0].velocity) {
                unsigned stride = s->macros[1] > 75u ? 1u :
                                  s->macros[1] > 20u ? 2u : 4u;
                fire = !s->track[i].muted && s->step % stride == 0u;
                if (!velocity) velocity = 78u;
            }
            if (i == 0u && s->fill_active && s->step % 16u >= 12u &&
                (s->step & 1u) && !s->track[i].muted) {
                fire_seq_note(s, 0u, s->step == 15u ? 38u : 42u,
                              78u, 60000u / ((uint32_t)s->tempo * 8u));
            }
            if (s->auto_arrange &&
                (((s->scene == 2u || s->scene == 3u) && i < 2u) ||
                 (s->scene == 3u && i == 3u))) fire = 0u;
            if (!fire) continue;
            duration_ms = (uint32_t)(step->length ? step->length : 1u) *
                60000u / ((uint32_t)s->tempo * 4u);
            if (!s->edited[i]) {
                chord_index = s->bar % 4u;
                if (i == 1u)
                    note = (uint8_t)(preset_bass[s->preset][chord_index] +
                                     (s->step == 6u ? 12u : 0u));
                if (i == 2u) {
                    unsigned n;
                    for (n = 0u; n < 4u; n++)
                        fire_seq_note(s, i, preset_chord[s->preset][chord_index][n],
                                      velocity, duration_ms);
                    continue;
                }
                if (i == 3u)
                    note = (uint8_t)(preset_chord[s->preset][chord_index]
                        [(s->step / 2u) % 4u] + 12u);
            }
            fire_seq_note(s, i, note, velocity, duration_ms);
        }
    }
}

static int32_t wave_at(uint32_t phase, uint8_t wave)
{
    uint32_t p = phase >> 16;
    switch (wave) {
    case 0u: { /* sine: parabolic, zero at both half-cycle boundaries */
        int32_t q = (int32_t)(p & 32767u);
        int32_t y = q * (32768 - q) / 26843;
        return p < 32768u ? y : -y;
    }
    case 1u: { /* triangle */
        int32_t y = p < 16384u ? (int32_t)p :
                    p < 49152u ? 32768 - (int32_t)p : (int32_t)p - 65536;
        return y * 10000 / 16384;
    }
    case 2u: return ((int32_t)p - 32768) / 3; /* saw */
    case 3u: return p < 32768u ? 6500 : -6500;
    default: {
        int32_t a = p < 32768u ? (int32_t)p : 65535 - (int32_t)p;
        uint32_t phase3 = phase * 3u;
        uint32_t q = phase3 >> 16;
        int32_t b = q < 32768u ? (int32_t)q : 65535 - (int32_t)q;
        return ((a - 16384) * (b - 16384)) / 32768;
    }
    }
}

static int32_t oscillator(nf_voice *v, uint8_t instrument, uint8_t wave,
                          uint8_t spread)
{
    if (instrument == NF_DRUM) {
        int32_t noise, bright;
        v->noise ^= v->noise << 13;
        v->noise ^= v->noise >> 17;
        v->noise ^= v->noise << 5;
        noise = (int32_t)(v->noise & 65535u) - 32768;
        v->noise_z += (noise - v->noise_z) / 8;
        bright = noise - v->noise_z;
        if (v->note == 36u) return wave_at(v->phase, 0u) * 2;
        if (v->note == 38u) return (bright + wave_at(v->phase, 0u)) / 2;
        return bright / (v->note == 46u ? 2 : 3);
    }
    return (wave_at(v->phase, wave) * (int32_t)(100u - spread / 2u) +
            wave_at(v->detune_phase, wave) * (int32_t)(spread / 2u)) / 100;
}

static int16_t clip16(int32_t x)
{
    return (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
}

uint32_t nf_project_hash(const nf_project *p)
{
    uint32_t h = 2166136261u;
    unsigned t, i, f, k;
#define HASH_VALUE(v) do { h = (h ^ (uint32_t)(v)) * 16777619u; } while (0)
    HASH_VALUE(p->tempo); HASH_VALUE(p->pattern_length);
    for (t = 0; t < NF_TRACKS; t++) {
        const nf_track *track = &p->track[t];
        HASH_VALUE(track->instrument); HASH_VALUE(track->level);
        HASH_VALUE(track->muted); HASH_VALUE(track->delay_send);
        for (i = 0; i < NF_STEPS; i++) {
            const nf_step *step = &track->steps[i];
            HASH_VALUE(step->note); HASH_VALUE(step->velocity);
            HASH_VALUE(step->length); HASH_VALUE(step->chance);
        }
    }
    for (f = 0; f < NF_FX_COUNT; f++) {
        HASH_VALUE(p->fx[f].enabled);
        for (k = 0; k < 4u; k++) HASH_VALUE(p->fx[f].value[k]);
    }
    HASH_VALUE(p->preset); HASH_VALUE(p->auto_arrange);
    HASH_VALUE(p->scene); HASH_VALUE(p->scale);
    HASH_VALUE(p->arp_enabled); HASH_VALUE(p->home_view);
    HASH_VALUE(p->selected_track);
    for (k = 0; k < 4u; k++) {
        HASH_VALUE(p->macros[k]); HASH_VALUE(p->lfo_control[k]);
        HASH_VALUE(p->arp_control[k]);
    }
    for (t = 0; t < NF_TRACKS; t++) {
        HASH_VALUE(p->edited[t]);
        for (k = 0; k < 4u; k++) {
            HASH_VALUE(p->envelope_control[t][k]);
            HASH_VALUE(p->voice_control[t][k]);
        }
    }
#undef HASH_VALUE
    return h;
}

int nf_save_project(nf_state *s, unsigned slot)
{
    nf_project *p;
    unsigned i;
    if (!s || slot >= NF_PROJECT_SLOTS || s->playing) return -1;
    if (s->lfo_control[0] > 7u || s->lfo_control[1] > 100u ||
        s->lfo_control[2] > 2u || s->lfo_control[3] > 3u ||
        s->arp_control[0] > 3u || s->arp_control[1] > 4u ||
        s->arp_control[2] > 100u || s->arp_control[3] > 2u) return -2;
    p = &s->project[slot];
    p->valid = 0u;
    p->tempo = s->tempo; p->pattern_length = s->pattern_length;
    p->preset = s->preset; p->auto_arrange = s->auto_arrange;
    p->scene = s->scene; p->scale = s->scale;
    p->arp_enabled = s->arp_enabled; p->home_view = s->home_view;
    p->selected_track = s->selected_track;
    for (i = 0; i < 4u; i++) {
        p->macros[i] = s->macros[i];
        p->lfo_control[i] = s->lfo_control[i];
        p->arp_control[i] = s->arp_control[i];
    }
    for (i = 0; i < NF_TRACKS; i++) {
        unsigned j;
        p->track[i] = s->track[i];
        p->edited[i] = s->edited[i];
        for (j = 0; j < 4u; j++) {
            p->envelope_control[i][j] = s->envelope_control[i][j];
            p->voice_control[i][j] = s->voice_control[i][j];
        }
    }
    for (i = 0; i < NF_FX_COUNT; i++) p->fx[i] = s->fx[i];
    p->checksum = nf_project_hash(p);
    p->valid = 1u;
    return 0;
}

int nf_load_project(nf_state *s, unsigned slot)
{
    const nf_project *p;
    unsigned i;
    if (slot >= NF_PROJECT_SLOTS || s->playing) return -1;
    p = &s->project[slot];
    if (!p->valid || p->checksum != nf_project_hash(p) ||
        p->tempo < 40u || p->tempo > 240u ||
        p->pattern_length == 0u || p->pattern_length > NF_STEPS) return -2;
    s->preset = p->preset; s->auto_arrange = p->auto_arrange;
    s->scene = p->scene; s->scale = p->scale;
    s->arp_enabled = p->arp_enabled; s->home_view = p->home_view;
    s->selected_track = p->selected_track;
    for (i = 0; i < 4u; i++) {
        s->macros[i] = p->macros[i];
        s->lfo_control[i] = p->lfo_control[i];
        s->arp_control[i] = p->arp_control[i];
    }
    for (i = 0; i < NF_TRACKS; i++) {
        unsigned j;
        s->track[i] = p->track[i];
        s->edited[i] = p->edited[i];
        for (j = 0; j < 4u; j++) {
            s->envelope_control[i][j] = p->envelope_control[i][j];
            s->voice_control[i][j] = p->voice_control[i][j];
        }
    }
    for (i = 0; i < NF_FX_COUNT; i++) s->fx[i] = p->fx[i];
    s->tempo = p->tempo;
    s->pattern_length = p->pattern_length;
    s->step = 0u; s->step_clock = 0u;
    s->bar = 0u; s->queued_scene = 0xffu;
    s->fill_queued = s->fill_active = 0u;
    return 0;
}

void nf_render_audio(nf_state *s, int16_t *stereo, size_t frames)
{
    static const uint16_t lfo_divider[8] = {16u,8u,4u,2u,1u,1u,1u,1u};
    size_t n;
    uint32_t peak = 0u;
    uint32_t lfo_rate = s->lfo_control[0] > 7u ? 7u : s->lfo_control[0];
    uint64_t lfo_step = (uint64_t)s->tempo * 4294967296ull /
        (60u * NF_SAMPLE_RATE * lfo_divider[lfo_rate]);
    if (lfo_rate > 4u) lfo_step <<= lfo_rate - 4u;
    for (n = 0; n < frames; n++) {
        static const int8_t pan[NF_TRACKS] = {0, 0, -22, 28};
        int32_t dry = 0, send = 0, side = 0;
        int32_t lfo, depth;
        unsigned i;
        {
            s->lfo_phase += (uint32_t)lfo_step;
            {
                uint32_t p = s->lfo_phase >> 16;
                uint32_t shape = s->lfo_control[3];
                lfo = shape == 2u ? p < 32768u ? 32767 : -32767 :
                    shape == 3u ? (int32_t)p - 32768 :
                    p < 32768u ? (int32_t)p * 2 - 32768 :
                                 98302 - (int32_t)p * 2;
                if (shape == 0u) {
                    int32_t magnitude = lfo < 0 ? -lfo : lfo;
                    lfo = lfo * (65536 - magnitude) / 32768;
                }
            }
        }
        depth = (int32_t)s->lfo_control[1] * s->macros[3] / 100;
        for (i = 0; i < NF_VOICES; i++) {
            nf_voice *v = &s->voice[i];
            const nf_track *t;
            const uint8_t *env, *edit;
            uint32_t inc;
            int32_t sample;
            if (!v->active) continue;
            t = &s->track[v->track];
            env = s->envelope_control[v->track];
            edit = s->voice_control[v->track];
            if (t->instrument == NF_DRUM) {
                if (v->stage == 0u) {
                    v->envelope += 2400;
                    if (v->envelope >= 32767) {
                        v->envelope = 32767; v->stage = 1u;
                    }
                } else {
                    v->envelope -= v->note == 36u ? 5 :
                                   v->note == 38u ? 9 : 26;
                    if (v->envelope <= 0) {
                        nf_event *e = event_for(s, v->serial);
                        if (e) { e->live = 0u; e->ended_ms = s->now_ms; e->envelope = 0u; }
                        v->active = 0u;
                        continue;
                    }
                }
            } else if (v->gate) {
                if (v->stage == 0u) {
                    v->envelope += 2 + (100 - env[0]) *
                        (t->instrument == NF_PAD ? 1 : 2);
                    if (v->envelope >= 32767) {
                        v->envelope = 32767; v->stage = 1u;
                    }
                } else {
                    int32_t target = (int32_t)env[2] * 32767 / 100;
                    if (v->envelope > target) {
                        v->envelope -= 1 + (100 - env[1]) / 12;
                        if (v->envelope < target) v->envelope = target;
                    }
                }
            } else {
                v->envelope -= 1 + (100 - env[3]) / 8;
                if (v->envelope <= 0) {
                    nf_event *e = event_for(s, v->serial);
                    if (e) { e->live = 0u; e->ended_ms = s->now_ms; e->envelope = 0u; }
                    v->active = 0u;
                    continue;
                }
            }
            inc = v->increment;
            if (edit[2] > 2u) inc <<= edit[2] - 2u > 2u ? 2u : edit[2] - 2u;
            else if (edit[2] < 2u) inc >>= 2u - edit[2];
            if (s->lfo_control[2] == 2u)
                inc = (uint32_t)((int64_t)inc + (int64_t)inc * lfo * depth / 32768 / 400);
            if (t->instrument == NF_DRUM && v->note == 36u)
                inc += (uint32_t)((uint64_t)inc * (uint32_t)v->envelope / 32767u);
            v->phase += inc;
            v->detune_phase += inc + (uint32_t)((uint64_t)inc * edit[1] / 5000u);
            sample = oscillator(v, t->instrument, edit[0] > 4u ? 0u : edit[0], edit[1]);
            sample = sample * v->envelope / 32767 * v->velocity / 127 *
                edit[3] / 100;
            if (s->lfo_control[2] == 1u)
                sample = sample * (100 - depth / 3 + lfo * depth / 32768 / 3) / 100;
            /* Mute removes the arrangement, not live audition. A selected
             * muted track must still respond to the player's keyboard. */
            if (!t->muted || v->source != NF_SEQ) {
                int32_t contribution = sample * t->level / 127;
                dry += contribution;
                side += contribution * pan[v->track] / 100;
                send += sample * t->delay_send / 127;
            }
        }
        {
            int32_t cutoff = 3 + (int32_t)s->macros[0] * 12 / 10;
            if (s->lfo_control[2] == 0u)
                cutoff += lfo * depth / 32768 / 2;
            if (cutoff < 2) cutoff = 2;
            if (cutoff > 127) cutoff = 127;
            s->tone_z += (dry - s->tone_z) * cutoff / 128;
            dry = s->tone_z;
        }
        {
            int32_t master = nf_process_fx(s, dry, send);
            stereo[n * 2u] = clip16(master + side);
            stereo[n * 2u + 1u] = clip16(master - side);
            if ((s->terrain_decimator++ & 3u) == 0u) {
                int32_t mono = ((int32_t)stereo[n * 2u] +
                                stereo[n * 2u + 1u]) / 2;
                s->terrain_samples[s->terrain_write++ & (NF_TERRAIN_SAMPLES - 1u)] =
                    (int16_t)mono;
            }
        }
        {
            int32_t magnitude = stereo[n * 2u];
            if (magnitude < 0) magnitude = -magnitude;
            if ((uint32_t)magnitude > peak) peak = (uint32_t)magnitude;
        }
    }
    for (n = 0; n < NF_VOICES; n++) {
        nf_voice *v = &s->voice[n];
        nf_event *e = v->active ? event_for(s, v->serial) : NULL;
        if (e) e->envelope = (uint8_t)(v->envelope >> 7);
    }
    for (n = 0u; n < 31u; n++) s->scope[n] = s->scope[n + 1u];
    s->scope[31u] = (uint8_t)(peak > 32767u ? 255u :
        peak * 255u / (peak + 2000u));
    s->frame++;
}

static uint32_t terrain_sqrt(uint64_t n)
{
    uint64_t bit = (uint64_t)1u << 62;
    uint64_t result = 0u;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= result + bit) {
            n -= result + bit;
            result = (result >> 1) + bit;
        } else result >>= 1;
        bit >>= 2;
    }
    return result > 0xffffffffu ? 0xffffffffu : (uint32_t)result;
}

/* Twenty measured, logarithmically spaced bins from ~43 Hz to ~5.2 kHz.
 * Analysis is deliberately outside the audio IRQ. A silent window still
 * advances an empty slice: the final sounding moment must recede and vanish. */
void nf_update_terrain(nf_state *s)
{
    static const int16_t coefficient[NF_TERRAIN_BANDS] = {
        32758,32729,32679,32610,32522,32413,32286,32138,31786,31357,
        30572,29269,27246,24279,19520,12540,3212,-9512,-23170,-32286
    };
    int16_t window[NF_TERRAIN_SAMPLES];
    uint32_t total = 0u;
    uint16_t start = s->terrain_write;
    unsigned band, i, row;
    for (i = 0u; i < NF_TERRAIN_SAMPLES; i++) {
        int32_t sample = s->terrain_samples[(start + i) &
                                           (NF_TERRAIN_SAMPLES - 1u)] >> 5;
        unsigned taper = i < NF_TERRAIN_SAMPLES / 2u ? i :
                         NF_TERRAIN_SAMPLES - 1u - i;
        sample = sample * (int32_t)taper / 128;
        window[i] = (int16_t)sample;
        total += (uint32_t)(sample < 0 ? -sample : sample);
    }
    /* The history recedes even during silence, with a gentle phosphor fade. */
    for (row = 0u; row < NF_TERRAIN_ROWS; row++)
        for (band = 0u; band < NF_TERRAIN_BANDS; band++) {
            unsigned v = (unsigned)s->terrain[row][band] * 244u >> 8;
            s->terrain[row][band] = (uint8_t)(v > 2u ? v : 0u);
        }
    s->terrain_head = (uint8_t)((s->terrain_head + 1u) % NF_TERRAIN_ROWS);
    for (band = 0u; band < NF_TERRAIN_BANDS; band++) {
        int32_t a = 0, b = 0;
        int64_t power;
        uint32_t magnitude, level;
        if (total < 384u) {
            s->terrain[s->terrain_head][band] = 0u;
            continue;
        }
        for (i = 0u; i < NF_TERRAIN_SAMPLES; i++) {
            int32_t q = window[i] +
                (int32_t)(((int64_t)coefficient[band] * a) >> 14) - b;
            b = a; a = q;
        }
        power = (int64_t)a * a + (int64_t)b * b -
            (((int64_t)coefficient[band] * a * b) >> 14);
        magnitude = terrain_sqrt(power > 0 ? (uint64_t)power : 0u);
        level = (uint32_t)((uint64_t)magnitude *
            (20u + band / 2u) * 255u / (20u * 8000u));
        s->terrain[s->terrain_head][band] =
            (uint8_t)(level > 255u ? 255u : level);
    }
    s->terrain_last_ms = s->now_ms;
    s->terrain_serial++;
}
