#include "nightfall.h"

static uint16_t checksum(const uint8_t *data, size_t n)
{
    uint16_t crc = 0xffffu;
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned b;
        crc ^= (uint16_t)data[i] << 8;
        for (b = 0; b < 8u; b++)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000u) ? 0x1021u : 0u));
    }
    return crc;
}

int nf_project_blob_valid(const uint8_t *data, size_t length)
{
    size_t n = 8u;
    unsigned t, i, f, k;
    uint16_t tempo, crc;
    if (!data || length != NF_PROJECT_BLOB_SIZE ||
        data[0] != 'N' || data[1] != 'F' ||
        data[2] != 'P' || data[3] != 'R' || data[4] != 2u) return 0;
    crc = (uint16_t)(data[length - 2u] | ((uint16_t)data[length - 1u] << 8));
    if (crc != checksum(data, length - 2u)) return 0;
    tempo = (uint16_t)(data[5] | ((uint16_t)data[6] << 8));
    if (tempo < 40u || tempo > 240u || !data[7] || data[7] > NF_STEPS) return 0;
    for (t = 0; t < NF_TRACKS; t++) {
        if (data[n] > NF_DRUM || data[n + 1u] > 127u ||
            data[n + 2u] > 1u || data[n + 3u] > 127u) return 0;
        n += 4u;
        for (i = 0; i < NF_STEPS; i++) {
            if (data[n] > 127u || data[n + 1u] > 127u ||
                data[n + 2u] > 16u || data[n + 3u] > 100u) return 0;
            n += 4u;
        }
    }
    for (f = 0; f < NF_FX_COUNT; f++) {
        if (data[n++] > 1u) return 0;
        for (k = 0; k < 4u; k++)
            if (data[n++] > 127u) return 0;
    }
    if (data[n++] > 2u || data[n++] > 1u || data[n++] > 3u ||
        data[n++] > 2u || data[n++] > 1u || data[n++] > 1u ||
        data[n++] >= NF_TRACKS) return 0;
    for (k = 0; k < 4u; k++) if (data[n++] > 100u) return 0;
    for (t = 0; t < NF_TRACKS; t++)
        for (k = 0; k < 4u; k++) if (data[n++] > 100u) return 0;
    for (k = 0; k < 4u; k++)
        if (data[n++] > (k == 0u ? 7u : k == 2u ? 2u : k == 3u ? 3u : 100u)) return 0;
    for (t = 0; t < NF_TRACKS; t++)
        for (k = 0; k < 4u; k++)
            if (data[n++] > (k == 0u || k == 2u ? 4u : 100u)) return 0;
    for (k = 0; k < 4u; k++)
        if (data[n++] > (k == 0u ? 3u : k == 1u ? 4u : k == 3u ? 2u : 100u)) return 0;
    for (t = 0; t < NF_TRACKS; t++) if (data[n++] > 1u) return 0;
    while (n + 2u < length) if (data[n++] != 0u) return 0;
    return n + 2u == length;
}

size_t nf_export_project(const nf_state *s, unsigned slot, uint8_t *out, size_t capacity)
{
    const nf_project *p;
    size_t n = 0u;
    unsigned t, i, f, k;
    uint16_t crc;
    if (!s || !out || slot >= NF_PROJECT_SLOTS || capacity < NF_PROJECT_BLOB_SIZE)
        return 0u;
    p = &s->project[slot];
    if (!p->valid || p->checksum != nf_project_hash(p)) return 0u;
    out[n++] = 'N'; out[n++] = 'F'; out[n++] = 'P'; out[n++] = 'R';
    out[n++] = 2u;
    out[n++] = (uint8_t)p->tempo; out[n++] = (uint8_t)(p->tempo >> 8);
    out[n++] = (uint8_t)p->pattern_length;
    for (t = 0; t < NF_TRACKS; t++) {
        const nf_track *track = &p->track[t];
        out[n++] = track->instrument; out[n++] = track->level;
        out[n++] = track->muted; out[n++] = track->delay_send;
        for (i = 0; i < NF_STEPS; i++) {
            const nf_step *step = &track->steps[i];
            out[n++] = step->note; out[n++] = step->velocity;
            out[n++] = step->length; out[n++] = step->chance;
        }
    }
    for (f = 0; f < NF_FX_COUNT; f++) {
        out[n++] = p->fx[f].enabled;
        for (k = 0; k < 4u; k++) out[n++] = p->fx[f].value[k];
    }
    out[n++] = p->preset; out[n++] = p->auto_arrange;
    out[n++] = p->scene; out[n++] = p->scale;
    out[n++] = p->arp_enabled; out[n++] = p->home_view;
    out[n++] = p->selected_track;
    for (k = 0; k < 4u; k++) out[n++] = p->macros[k];
    for (t = 0; t < NF_TRACKS; t++)
        for (k = 0; k < 4u; k++) out[n++] = p->envelope_control[t][k];
    for (k = 0; k < 4u; k++) out[n++] = p->lfo_control[k];
    for (t = 0; t < NF_TRACKS; t++)
        for (k = 0; k < 4u; k++) out[n++] = p->voice_control[t][k];
    for (k = 0; k < 4u; k++) out[n++] = p->arp_control[k];
    for (t = 0; t < NF_TRACKS; t++) out[n++] = p->edited[t];
    while (n + 2u < NF_PROJECT_BLOB_SIZE) out[n++] = 0u;
    if (n + 2u != NF_PROJECT_BLOB_SIZE) return 0u;
    crc = checksum(out, n);
    out[n++] = (uint8_t)crc; out[n++] = (uint8_t)(crc >> 8);
    return n;
}

int nf_import_project(nf_state *s, unsigned slot, const uint8_t *data, size_t length)
{
    nf_project tmp;
    size_t n = 8u;
    unsigned t, i, f, k;
    uint16_t crc;
    if (!s || !data || slot >= NF_PROJECT_SLOTS || length != NF_PROJECT_BLOB_SIZE)
        return -1;
    if (data[0] != 'N' || data[1] != 'F' || data[2] != 'P' || data[3] != 'R' || data[4] != 2u)
        return -2;
    crc = (uint16_t)(data[length - 2u] | ((uint16_t)data[length - 1u] << 8));
    if (crc != checksum(data, length - 2u)) return -3;
    tmp.tempo = (uint16_t)(data[5] | ((uint16_t)data[6] << 8));
    tmp.pattern_length = data[7];
    if (tmp.tempo < 40u || tmp.tempo > 240u ||
        tmp.pattern_length == 0u || tmp.pattern_length > NF_STEPS) return -4;
    for (t = 0; t < NF_TRACKS; t++) {
        nf_track *track = &tmp.track[t];
        track->instrument = data[n++]; track->level = data[n++];
        track->muted = data[n++]; track->delay_send = data[n++];
        if (track->instrument > NF_DRUM || track->level > 127u ||
            track->muted > 1u || track->delay_send > 127u) return -4;
        for (i = 0; i < NF_STEPS; i++) {
            nf_step *step = &track->steps[i];
            step->note = data[n++]; step->velocity = data[n++];
            step->length = data[n++]; step->chance = data[n++];
            if (step->note > 127u || step->velocity > 127u ||
                step->length > 16u || step->chance > 100u) return -4;
        }
    }
    for (f = 0; f < NF_FX_COUNT; f++) {
        tmp.fx[f].enabled = data[n++];
        if (tmp.fx[f].enabled > 1u) return -4;
        for (k = 0; k < 4u; k++) {
            tmp.fx[f].value[k] = data[n++];
            if (tmp.fx[f].value[k] > 127u) return -4;
        }
    }
    tmp.preset = data[n++]; tmp.auto_arrange = data[n++];
    tmp.scene = data[n++]; tmp.scale = data[n++];
    tmp.arp_enabled = data[n++]; tmp.home_view = data[n++];
    tmp.selected_track = data[n++];
    for (k = 0; k < 4u; k++) tmp.macros[k] = data[n++];
    for (t = 0; t < NF_TRACKS; t++)
        for (k = 0; k < 4u; k++) tmp.envelope_control[t][k] = data[n++];
    for (k = 0; k < 4u; k++) tmp.lfo_control[k] = data[n++];
    for (t = 0; t < NF_TRACKS; t++)
        for (k = 0; k < 4u; k++) tmp.voice_control[t][k] = data[n++];
    for (k = 0; k < 4u; k++) tmp.arp_control[k] = data[n++];
    for (t = 0; t < NF_TRACKS; t++) tmp.edited[t] = data[n++];
    if (!nf_project_blob_valid(data, length)) return -4;
    if (s->playing) return -5;
    tmp.valid = 1u;
    tmp.checksum = nf_project_hash(&tmp);
    s->project[slot] = tmp;
    return 0;
}
