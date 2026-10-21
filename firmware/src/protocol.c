#include "nightfall.h"
#include "nf_libc.h"

static uint16_t crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffffu;
    size_t i;
    for (i = 0; i < length; i++) {
        unsigned bit;
        crc ^= (uint16_t)data[i] << 8;
        for (bit = 0; bit < 8u; bit++)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000u) ? 0x1021u : 0u));
    }
    return crc;
}

size_t nf_packet_encode(const nf_packet *packet, uint8_t *out, size_t capacity)
{
    size_t n;
    uint16_t crc;
    if (!packet || !out || packet->length > NF_PACKET_MAX_PAYLOAD) return 0u;
    n = 9u + packet->length;
    if (capacity < n) return 0u;
    out[0] = (uint8_t)NF_PACKET_MAGIC;
    out[1] = (uint8_t)(NF_PACKET_MAGIC >> 8);
    out[2] = NF_PACKET_VERSION;
    out[3] = packet->command;
    out[4] = packet->sequence;
    out[5] = (uint8_t)packet->length;
    out[6] = (uint8_t)(packet->length >> 8);
    memcpy(out + 7, packet->payload, packet->length);
    crc = crc16(out, n - 2u);
    out[n - 2u] = (uint8_t)crc;
    out[n - 1u] = (uint8_t)(crc >> 8);
    return n;
}

int nf_packet_decode(nf_packet *packet, const uint8_t *data, size_t length)
{
    uint16_t payload_length, received;
    if (!packet || !data || length < 9u) return -1;
    if (data[0] != (uint8_t)NF_PACKET_MAGIC ||
        data[1] != (uint8_t)(NF_PACKET_MAGIC >> 8) ||
        data[2] != NF_PACKET_VERSION) return -2;
    payload_length = (uint16_t)(data[5] | ((uint16_t)data[6] << 8));
    if (payload_length > NF_PACKET_MAX_PAYLOAD || length != 9u + payload_length) return -3;
    received = (uint16_t)(data[length - 2u] | ((uint16_t)data[length - 1u] << 8));
    if (received != crc16(data, length - 2u)) return -4;
    packet->command = data[3]; packet->sequence = data[4]; packet->length = payload_length;
    memcpy(packet->payload, data + 7, payload_length);
    return 0;
}

static int apply_control(nf_state *s, const uint8_t p[4])
{
    uint8_t group = p[0], index = p[1], parameter = p[2], value = p[3];
    if (group == 0u && !index && !parameter && value <= NF_SCALE_PAGE)
        s->page = value;
    else if (group == 1u && !index && !parameter && value < NF_TRACKS)
        s->selected_track = value;
    else if (group == 2u && !index && !parameter)
        return nf_load_preset(s, value);
    else if (group == 3u && index < 4u && !parameter && value <= 100u) {
        nf_set_macro(s, index, value);
    } else if (group == 4u && index < NF_TRACKS &&
               ((parameter == 0u && value <= 127u) ||
                (parameter == 1u && value <= 1u))) {
        if (parameter == 0u) s->track[index].level = value;
        else s->track[index].muted = value;
    } else if (group == 5u && index < NF_TRACKS && parameter < 4u &&
               value <= 100u) s->envelope_control[index][parameter] = value;
    else if (group == 6u && index < 4u && !parameter &&
             value <= (index == 0u ? 7u : index == 2u ? 2u :
                       index == 3u ? 3u : 100u)) s->lfo_control[index] = value;
    else if (group == 7u && index < NF_TRACKS && parameter < 4u &&
             value <= (parameter == 0u || parameter == 2u ? 4u : 100u))
        s->voice_control[index][parameter] = value;
    else if (group == 8u && index < 4u && !parameter &&
             value <= (index == 0u ? 3u : index == 1u ? 4u :
                       index == 3u ? 2u : 100u)) s->arp_control[index] = value;
    else if (group == 9u && !index && parameter < 3u && value <= 1u) {
        if (parameter == 0u) {
            s->queued_scene = value; s->auto_arrange = 0u;
        } else if (parameter == 1u) s->auto_arrange = value;
        else s->fill_queued = value;
    } else if (group == 10u && !index && !parameter && value <= 2u)
        s->scale = value;
    else if (group == 11u && !index && !parameter && value <= 1u)
        s->recording = value;
    else if (group == 12u && !index && !parameter && value <= 1u)
        s->home_view = value;
    else if (group == 13u && !index && !parameter && value < NF_FX_COUNT)
        s->selected_fx = value;
    else if (group == 14u && !index && !parameter && value < NF_PROJECT_SLOTS)
        s->selected_project = value;
    else if (group == 15u && !index && !parameter && value <= 1u)
        s->save_op = value;
    else if (group == 16u && !index && !parameter &&
             value >= 16u && value <= NF_STEPS && value % 16u == 0u) {
        s->pattern_length = value;
        if (s->step >= value) s->step = 0u;
    }
    else if (group == 17u && !index && !parameter && value <= 1u)
        s->monitor_mute_until_ms = value ? s->now_ms + 2500u : 0u;
    else return -1;
    return 0;
}

static void detail_reply(const nf_state *s, nf_packet *reply)
{
    unsigned i, t, k, n = 1u;
    uint8_t mask = 0u;
    reply->payload[n++] = (uint8_t)s->tempo;
    reply->payload[n++] = (uint8_t)(s->tempo >> 8);
    reply->payload[n++] = s->playing;
    reply->payload[n++] = s->recording;
    reply->payload[n++] = s->page;
    reply->payload[n++] = s->selected_track;
    reply->payload[n++] = s->preset;
    reply->payload[n++] = s->scene;
    reply->payload[n++] = s->queued_scene;
    reply->payload[n++] = s->auto_arrange;
    reply->payload[n++] = s->scale;
    reply->payload[n++] = s->arp_enabled;
    reply->payload[n++] = s->home_view;
    reply->payload[n++] = (uint8_t)s->bar;
    reply->payload[n++] = (uint8_t)s->step;
    for (i = 0u; i < 4u; i++) reply->payload[n++] = s->macros[i];
    for (t = 0u; t < NF_TRACKS; t++) reply->payload[n++] = s->track[t].level;
    for (t = 0u; t < NF_TRACKS; t++) reply->payload[n++] = s->track[t].muted;
    for (i = 0u; i < NF_FX_COUNT; i++) reply->payload[n++] = s->fx[i].enabled;
    for (i = 0u; i < NF_FX_COUNT; i++)
        for (k = 0u; k < 4u; k++) reply->payload[n++] = s->fx[i].value[k];
    for (t = 0u; t < NF_TRACKS; t++)
        for (k = 0u; k < 4u; k++) reply->payload[n++] = s->envelope_control[t][k];
    for (i = 0u; i < 4u; i++) reply->payload[n++] = s->lfo_control[i];
    for (t = 0u; t < NF_TRACKS; t++)
        for (k = 0u; k < 4u; k++) reply->payload[n++] = s->voice_control[t][k];
    for (i = 0u; i < 4u; i++) reply->payload[n++] = s->arp_control[i];
    for (i = 0u; i < 8u; i++) {
        uint8_t flags = 0u;
        for (k = 0u; k < 8u; k++)
            if (s->track[s->selected_track].steps[i * 8u + k].velocity) flags |= 1u << k;
        reply->payload[n++] = flags;
    }
    for (t = 0u; t < NF_TRACKS; t++) reply->payload[n++] = s->edited[t];
    for (i = 0u; i < NF_PROJECT_SLOTS; i++)
        if (s->project[i].valid) mask |= 1u << i;
    reply->payload[n++] = mask;
    reply->payload[n++] = s->selected_fx;
    reply->payload[n++] = s->selected_project;
    reply->payload[n++] = s->save_op;
    reply->payload[n++] = s->fill_queued;
    reply->payload[n++] = (uint8_t)s->pattern_length;
    reply->payload[n++] = s->seq_focus;
    reply->length = (uint16_t)n;
}

/* Snapshot of the actual voice/event state used by the 240px LCD renderer.
 * The browser can match the optical scene to the sound engine, rather than
 * guessing from raw key MIDI (which cannot reveal sequencer or muted voices). */
static void live_reply(const nf_state *s, nf_packet *reply)
{
    unsigned i, n = 1u, count_at, count = 0u, active = 0u;
    uint32_t late = s->audio_underruns;
    reply->payload[n++] = (uint8_t)s->now_ms;
    reply->payload[n++] = (uint8_t)(s->now_ms >> 8);
    reply->payload[n++] = (uint8_t)(s->now_ms >> 16);
    reply->payload[n++] = (uint8_t)(s->now_ms >> 24);
    reply->payload[n++] = s->page;
    reply->payload[n++] = s->selected_track;
    reply->payload[n++] = s->playing;
    reply->payload[n++] = s->recording;
    reply->payload[n++] = (uint8_t)s->step;
    reply->payload[n++] = (uint8_t)s->bar;
    reply->payload[n++] = s->master_level;
    reply->payload[n++] = (uint8_t)late;
    reply->payload[n++] = (uint8_t)(late >> 8);
    for (i = 0u; i < NF_VOICES; i++) active += s->voice[i].active != 0u;
    reply->payload[n++] = (uint8_t)active;
    reply->payload[n++] = (uint8_t)s->tempo;
    reply->payload[n++] = (uint8_t)(s->tempo >> 8);
    reply->payload[n++] = s->scene;
    reply->payload[n++] = s->queued_scene;
    reply->payload[n++] = s->home_view;
    reply->payload[n++] = s->selected_fx;
    for (i = 0u; i < 32u; i++) reply->payload[n++] = s->scope[i];
    count_at = n++;
    for (i = 0u; i < NF_EVENTS && count < 20u; i++) {
        const nf_event *e = &s->event[(s->next_event + NF_EVENTS - 1u - i) % NF_EVENTS];
        uint32_t onset_age, ended_age;
        if (!e->serial) continue;
        ended_age = s->now_ms - e->ended_ms;
        if (!e->live && ended_age > 3000u) continue;
        onset_age = s->now_ms - e->onset_ms;
        reply->payload[n++] = (uint8_t)e->serial;
        reply->payload[n++] = e->note;
        reply->payload[n++] = e->track;
        reply->payload[n++] = e->source;
        reply->payload[n++] = e->velocity;
        reply->payload[n++] = e->envelope;
        reply->payload[n++] = (uint8_t)(onset_age / 10u > 255u ? 255u : onset_age / 10u);
        reply->payload[n++] = e->live ? 255u :
            (uint8_t)(ended_age / 10u > 254u ? 254u : ended_age / 10u);
        count++;
    }
    reply->payload[count_at] = (uint8_t)count;
    reply->payload[n++] = s->terrain_serial;
    for (i = 0u; i < NF_TERRAIN_BANDS; i++)
        reply->payload[n++] = s->terrain[s->terrain_head][i];
    reply->length = (uint16_t)n;
}

int nf_packet_apply(nf_state *state, const nf_packet *request, nf_packet *reply)
{
    const uint8_t *p;
    if (!state || !request || !reply || request->length > NF_PACKET_MAX_PAYLOAD) return -1;
    reply->command = (uint8_t)(request->command | 0x80u);
    reply->sequence = request->sequence;
    reply->length = 1u;
    reply->payload[0] = 0u;
    p = request->payload;
    switch (request->command) {
    case NF_CMD_HELLO:
        if (request->length) return -1;
        reply->length = 12u;
        reply->payload[0] = 0u;
        memcpy(reply->payload + 1, "NIGHTFALL", 9u);
        reply->payload[10] = NF_PACKET_VERSION;
        reply->payload[11] = NF_TRACKS;
        return 0;
    case NF_CMD_STATUS: {
        unsigned i;
        if (request->length) return -1;
        reply->length = 13u;
        reply->payload[1] = (uint8_t)state->tempo;
        reply->payload[2] = (uint8_t)(state->tempo >> 8);
        reply->payload[3] = state->playing;
        reply->payload[4] = state->recording;
        reply->payload[5] = state->selected_track;
        reply->payload[6] = state->page;
        for (i = 0; i < NF_FX_COUNT; i++) reply->payload[7u + i] = state->fx[i].enabled;
        return 0;
    }
    case NF_CMD_NOTE_ON:
        if (request->length != 3u || p[0] >= NF_TRACKS || p[1] > 127u ||
            p[2] == 0u || p[2] > 127u) return -1;
        nf_note_on(state, p[0], p[1], p[2], NF_WEB);
        return 0;
    case NF_CMD_NOTE_OFF:
        if (request->length != 2u || p[0] >= NF_TRACKS || p[1] > 127u) return -1;
        nf_note_off(state, p[0], p[1], NF_WEB);
        return 0;
    case NF_CMD_TEMPO: {
        uint16_t tempo;
        if (request->length != 2u) return -1;
        tempo = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
        if (tempo < 40u || tempo > 240u) return -1;
        state->tempo = tempo;
        return 0;
    }
    case NF_CMD_FX:
        if (request->length != 3u || p[0] >= NF_FX_COUNT || p[1] > 4u ||
            p[2] > (p[1] == 4u ? 1u : 127u)) return -1;
        if (p[1] == 4u) state->fx[p[0]].enabled = p[2];
        else state->fx[p[0]].value[p[1]] = p[2];
        return 0;
    case NF_CMD_STEP:
        if (request->length != 6u || p[0] >= NF_TRACKS || p[1] >= NF_STEPS ||
            p[2] > 127u || p[3] > 127u || p[4] > 16u || p[5] > 100u) return -1;
        state->track[p[0]].steps[p[1]] = (nf_step){p[2], p[3], p[4], p[5]};
        state->edited[p[0]] = 1u;
        return 0;
    case NF_CMD_PLAY:
        if (request->length != 1u || p[0] > 1u) return -1;
        if (state->playing != p[0]) nf_toggle_play(state);
        return 0;
    case NF_CMD_PROJECT_SAVE:
    case NF_CMD_PROJECT_LOAD:
        if (request->length != 1u || p[0] >= NF_PROJECT_SLOTS) return -1;
        if (request->command == NF_CMD_PROJECT_SAVE) return nf_save_project(state, p[0]);
        return nf_load_project(state, p[0]);
    case NF_CMD_CONTROL:
        if (request->length != 4u) return -1;
        return apply_control(state, p);
    case NF_CMD_DETAIL:
        if (request->length) return -1;
        detail_reply(state, reply);
        return 0;
    case NF_CMD_LIVE:
        if (request->length) return -1;
        live_reply(state, reply);
        return 0;
    default:
        return -2;
    }
}
