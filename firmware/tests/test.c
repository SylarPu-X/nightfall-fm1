#include "nightfall.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static nf_state state;
static uint16_t pixels[NF_WIDTH * NF_HEIGHT];
static int16_t audio[512 * 2];
static nf_link link;
static struct {
    uint8_t data[NF_STORE_SECTORS * NF_STORE_SECTOR_SIZE];
    int fail_after;
} flash;
static nf_store store;
static uint8_t flash_baseline[NF_STORE_SECTORS * NF_STORE_SECTOR_SIZE];

static int flash_read(void *context, uint32_t offset, uint8_t *out, size_t length)
{
    (void)context;
    if (offset > sizeof flash.data || length > sizeof flash.data - offset) return -1;
    memcpy(out, flash.data + offset, length);
    return 0;
}
static int flash_erase(void *context, uint32_t offset)
{
    (void)context;
    if (offset % NF_STORE_SECTOR_SIZE || offset >= sizeof flash.data) return -1;
    memset(flash.data + offset, 0xff, NF_STORE_SECTOR_SIZE);
    return 0;
}
static int flash_program(void *context, uint32_t offset, const uint8_t *data, size_t length)
{
    size_t i;
    (void)context;
    if (offset > sizeof flash.data || length > sizeof flash.data - offset ||
        (offset & 255u) + length > 256u) return -1;
    if (flash.fail_after == 0) return -1;
    if (flash.fail_after > 0) flash.fail_after--;
    for (i = 0; i < length; i++)
        if ((flash.data[offset + i] & data[i]) != data[i]) return -1;
    for (i = 0; i < length; i++) flash.data[offset + i] &= data[i];
    return 0;
}

static unsigned active_voices(const nf_state *s)
{
    unsigned i, n = 0;
    for (i = 0; i < NF_VOICES; i++) n += s->voice[i].active != 0u;
    return n;
}
static unsigned live_events(const nf_state *s)
{
    unsigned i, n = 0;
    for (i = 0; i < NF_EVENTS; i++) n += s->event[i].live != 0u;
    return n;
}
static void test_note_and_city(void)
{
    uint16_t idle[NF_WIDTH * NF_HEIGHT];
    unsigned i, different = 0;
    nf_init(&state);
    nf_draw_city(&state, idle);
    nf_tick_ms(&state, 500u);
    nf_draw_city(&state, pixels);
    assert(!memcmp(idle + 73u * NF_WIDTH, pixels + 73u * NF_WIDTH,
                   111u * NF_WIDTH * sizeof(uint16_t)));
    nf_note_on(&state, 0, 60, 100, NF_KEY);
    assert(active_voices(&state) == 1u && live_events(&state) == 1u);
    nf_render_audio(&state, audio, 512u);
    nf_draw_city(&state, pixels);
    for (i = 73u * NF_WIDTH; i < 184u * NF_WIDTH; i++)
        different += pixels[i] != idle[i];
    assert(different > 20u);
    assert(state.event[0].envelope > 0u);
    nf_note_off(&state, 0, 60, NF_KEY);
    for (i = 0; i < 80u; i++) nf_render_audio(&state, audio, 512u);
    assert(active_voices(&state) == 0u && live_events(&state) == 0u);
    nf_tick_ms(&state, 2999u);
    nf_draw_city(&state, pixels);
    assert(memcmp(idle + 73u * NF_WIDTH, pixels + 73u * NF_WIDTH,
                  111u * NF_WIDTH * sizeof(uint16_t)));
    nf_tick_ms(&state, 2u);
    nf_draw_city(&state, pixels);
    assert(!memcmp(idle + 73u * NF_WIDTH, pixels + 73u * NF_WIDTH,
                   111u * NF_WIDTH * sizeof(uint16_t)));
}
static void test_polyphony_and_sequence(void)
{
    unsigned i;
    nf_init(&state);
    for (i = 0; i < 20u; i++) nf_note_on(&state, 0, (uint8_t)(48u + i), 100, NF_MIDI);
    assert(active_voices(&state) == NF_VOICES);
    assert(live_events(&state) == NF_VOICES);
    nf_init(&state);
    state.auto_arrange = 0u;
    state.track[0].steps[1] = (nf_step){60, 110, 1, 100};
    nf_toggle_play(&state);
    nf_tick_ms(&state, 1u);
    assert(state.step == 0u && active_voices(&state) == 0u);
    nf_tick_ms(&state, 157u);
    assert(state.step == 1u && active_voices(&state) == 1u);
    assert(state.event[0].source == NF_SEQ);
    assert(state.event[0].duration_ms == 156u);
    nf_tick_ms(&state, 156u);
    assert(state.voice[0].gate == 0u);
    nf_init(&state);
    state.auto_arrange = 0u;
    state.track[0].steps[1] = (nf_step){60, 110, 1, 0};
    nf_toggle_play(&state);
    nf_tick_ms(&state, 158u);
    assert(active_voices(&state) == 0u);
}
static void test_step_recording(void)
{
    nf_init(&state);
    state.recording = 1u;
    nf_note_on(&state, 2u, 72u, 116u, NF_KEY);
    assert(state.track[2].steps[0].note == 72u);
    assert(state.track[2].steps[0].velocity == 116u);
    assert(state.step == 1u);
    state.playing = 1u;
    nf_note_on(&state, 2u, 76u, 90u, NF_KEY);
    assert(state.track[2].steps[1].note == 76u);
    assert(state.step == 1u);
}
static void test_overlapping_note_release(void)
{
    uint32_t first, second;
    nf_init(&state);
    first = nf_note_on_id(&state, 3u, 60u, 100u, NF_KEY);
    second = nf_note_on_id(&state, 3u, 60u, 110u, NF_KEY);
    assert(first && second && first != second);
    nf_note_off_id(&state, second);
    assert(state.voice[0].gate == 1u && state.voice[1].gate == 0u);
    nf_note_off_id(&state, first);
    assert(state.voice[0].gate == 0u);
    nf_init(&state);
    nf_note_on(&state, 2u, 64u, 80u, NF_MIDI);
    nf_note_on(&state, 2u, 64u, 90u, NF_MIDI);
    nf_note_off(&state, 2u, 64u, NF_MIDI);
    assert(state.voice[0].gate == 0u && state.voice[1].gate == 1u);
    nf_note_off(&state, 2u, 64u, NF_MIDI);
    assert(state.voice[1].gate == 0u);
}

static void test_live_voice_priority(void)
{
    unsigned i;
    uint32_t held;
    nf_init(&state);
    for (i = 0u; i < NF_VOICES; i++)
        nf_note_on(&state, 2u, (uint8_t)(50u + i), 90u, NF_SEQ);
    held = nf_note_on_id(&state, 3u, 72u, 110u, NF_KEY);
    assert(held != 0u);
    for (i = 0u; i < 50u; i++) {
        nf_note_on(&state, 2u, (uint8_t)(58u + i % 12u), 90u, NF_SEQ);
        nf_render_audio(&state, audio, 128u);
    }
    for (i = 0u; i < NF_VOICES; i++)
        if (state.voice[i].serial == held) {
            assert(state.voice[i].active && state.voice[i].gate);
            break;
        }
    assert(i < NF_VOICES);
    nf_init(&state);
    for (i = 0u; i < NF_VOICES; i++)
        nf_note_on(&state, 3u, (uint8_t)(60u + i), 100u, NF_KEY);
    assert(nf_note_on_id(&state, 2u, 72u, 90u, NF_SEQ) == 0u);
}

static void test_live_audition_on_muted_track(void)
{
    unsigned i, peak = 0u;
    nf_init(&state);
    state.track[3].muted = 1u;
    nf_note_on(&state, 3u, 69u, 110u, NF_KEY);
    nf_render_audio(&state, audio, 512u);
    for (i = 0u; i < 1024u; i++) {
        int32_t sample = audio[i];
        unsigned magnitude = (unsigned)(sample < 0 ? -sample : sample);
        if (magnitude > peak) peak = magnitude;
    }
    assert(peak > 100u);
    nf_init(&state);
    state.track[3].muted = 1u;
    nf_note_on(&state, 3u, 69u, 110u, NF_SEQ);
    nf_render_audio(&state, audio, 512u);
    for (i = 0u; i < 1024u; i++) assert(audio[i] == 0);
}

static unsigned sequence_lead_events(unsigned density)
{
    unsigned i, events = 0u;
    nf_init(&state);
    assert(nf_load_preset(&state, 0u) == 0);
    state.auto_arrange = 0u;
    for (i = 0u; i < 3u; i++) state.track[i].muted = 1u;
    nf_set_macro(&state, 1u, (uint8_t)density);
    nf_toggle_play(&state);
    nf_tick_ms(&state, 470u);
    for (i = 0u; i < NF_EVENTS; i++)
        events += state.event[i].serial && state.event[i].track == 3u &&
                  state.event[i].source == NF_SEQ;
    return events;
}

static void test_performance_macros(void)
{
    int16_t sine[512 * 2];
    unsigned i, changed = 0u;
    assert(sequence_lead_events(0u) == 1u);
    assert(sequence_lead_events(100u) == 4u);
    nf_init(&state);
    nf_set_macro(&state, 2u, 100u);
    assert(state.fx[NF_DELAY].value[3] == 54u);
    assert(state.fx[NF_REVERB].value[3] == 62u);
    state.lfo_control[0] = 7u;
    state.lfo_control[1] = 100u;
    state.lfo_control[2] = 2u;
    state.lfo_control[3] = 0u;
    nf_set_macro(&state, 3u, 100u);
    nf_note_on(&state, 3u, 72u, 100u, NF_KEY);
    nf_render_audio(&state, sine, 512u);
    nf_init(&state);
    state.lfo_control[0] = 7u;
    state.lfo_control[1] = 100u;
    state.lfo_control[2] = 2u;
    state.lfo_control[3] = 1u;
    nf_set_macro(&state, 3u, 100u);
    nf_note_on(&state, 3u, 72u, 100u, NF_KEY);
    nf_render_audio(&state, audio, 512u);
    for (i = 0u; i < 1024u; i++) changed |= sine[i] != audio[i];
    assert(changed);
}
static void test_protocol(void)
{
    nf_packet in = {.command = 3, .sequence = 42, .length = 4,
                    .payload = {0x12, 0x34, 0x56, 0x78}}, out;
    uint8_t bytes[256];
    size_t n = nf_packet_encode(&in, bytes, sizeof bytes);
    assert(n == 13u);
    assert(nf_packet_decode(&out, bytes, n) == 0);
    assert(out.command == in.command && out.sequence == in.sequence);
    assert(out.length == in.length && !memcmp(out.payload, in.payload, in.length));
    bytes[8] ^= 1u;
    assert(nf_packet_decode(&out, bytes, n) == -4);
    assert(nf_packet_decode(&out, bytes, n - 1u) == -3);
    assert(nf_packet_encode(&in, bytes, 12u) == 0u);
    nf_init(&state);
    in.command = NF_CMD_TEMPO; in.length = 2u;
    in.payload[0] = 96u; in.payload[1] = 0u;
    assert(nf_packet_apply(&state, &in, &out) == 0 && state.tempo == 96u);
    in.payload[0] = 10u;
    assert(nf_packet_apply(&state, &in, &out) == -1 && state.tempo == 96u);
    in.command = NF_CMD_FX; in.length = 3u;
    in.payload[0] = NF_REVERB; in.payload[1] = 0u; in.payload[2] = 112u;
    assert(nf_packet_apply(&state, &in, &out) == 0);
    assert(state.fx[NF_REVERB].value[0] == 112u);
    in.command = NF_CMD_STATUS; in.length = 0u;
    assert(nf_packet_apply(&state, &in, &out) == 0);
    assert(out.length == 13u && out.payload[1] == 96u);
    in.command = NF_CMD_NOTE_ON; in.length = 3u;
    in.payload[0] = 0u; in.payload[1] = 64u; in.payload[2] = 100u;
    assert(nf_packet_apply(&state, &in, &out) == 0 && live_events(&state) == 1u);
    in.command = NF_CMD_NOTE_OFF; in.length = 2u;
    assert(nf_packet_apply(&state, &in, &out) == 0 && state.voice[0].gate == 0u);
}

static void test_live_snapshot(void)
{
    nf_packet request = {.command = NF_CMD_LIVE, .sequence = 9u}, reply;
    unsigned i;
    nf_init(&state);
    state.master_level = 78u;
    state.audio_underruns = 7u;
    state.selected_track = 3u;
    nf_note_on(&state, 3u, 72u, 110u, NF_KEY);
    nf_render_audio(&state, audio, 512u);
    nf_render_audio(&state, audio, 512u);
    nf_update_terrain(&state);
    nf_tick_ms(&state, 20u);
    assert(nf_packet_apply(&state, &request, &reply) == 0);
    assert(reply.length == 83u && reply.payload[53] == 1u);
    assert(reply.payload[6] == 3u && reply.payload[11] == 78u);
    assert(reply.payload[12] == 7u && reply.payload[14] == 1u);
    assert(reply.payload[16] == 0u && reply.payload[17] == state.scene);
    assert(reply.payload[55] == 72u && reply.payload[57] == NF_KEY);
    assert(reply.payload[62] == state.terrain_serial);
    assert(state.terrain_serial > 0u);
    {
        unsigned sum = 0u;
        for (i = 0u; i < NF_TERRAIN_BANDS; i++)
            sum += reply.payload[63u + i];
        assert(sum > 0u);
    }
    for (i = 0u; i < NF_TERRAIN_SAMPLES; i++) state.terrain_samples[i] = 0;
    for (i = 0u; i < NF_TERRAIN_ROWS; i++) {
        nf_tick_ms(&state, 70u);
        nf_update_terrain(&state);
    }
    for (i = 0u; i < NF_TERRAIN_ROWS; i++)
        for (unsigned band = 0u; band < NF_TERRAIN_BANDS; band++)
            assert(state.terrain[i][band] == 0u);
    assert(state.terrain_serial == NF_TERRAIN_ROWS + 1u);
    for (i = 0u; i < 40u; i++)
        nf_note_on(&state, 3u, (uint8_t)(50u + i), 100u, NF_MIDI);
    assert(nf_packet_apply(&state, &request, &reply) == 0);
    assert(reply.length <= NF_PACKET_MAX_PAYLOAD && reply.payload[53] <= 20u);
}

static int transfer(const nf_packet *request, nf_packet *response)
{
    uint8_t frame[NF_PACKET_FRAME_MAX], reply[NF_PACKET_FRAME_MAX];
    size_t length = nf_packet_encode(request, frame, sizeof frame), i;
    int result = 0;
    assert(length);
    for (i = 0; i < length; i++) {
        int n = nf_link_feed(&link, frame[i], reply, sizeof reply);
        if (i + 1u < length) assert(n == 0);
        else result = n;
    }
    assert(result > 0);
    assert(nf_packet_decode(response, reply, (size_t)result) == 0);
    assert(response->sequence == request->sequence);
    return response->payload[0];
}

static void test_link_project_transfer(void)
{
    uint8_t blob[NF_PROJECT_BLOB_SIZE], corrupt[NF_PACKET_FRAME_MAX];
    nf_packet request = {.sequence = 71u}, reply;
    size_t n;
    unsigned i;
    nf_init(&state);
    nf_link_init(&link, &state);
    state.track[0].steps[5] = (nf_step){77u, 102u, 2u, 100u};
    assert(nf_save_project(&state, 0u) == 0);
    assert(nf_export_project(&state, 0u, blob, sizeof blob) == sizeof blob);
    request.command = NF_CMD_PROJECT_IMPORT_BEGIN;
    request.length = 1u; request.payload[0] = 1u;
    assert(transfer(&request, &reply) == 0);
    request.command = NF_CMD_PROJECT_IMPORT_COMMIT;
    assert(transfer(&request, &reply) != 0);
    assert(!state.project[1].valid);
    for (i = NF_PROJECT_CHUNKS; i-- > 0u;) {
        request.command = NF_CMD_PROJECT_IMPORT_CHUNK;
        request.length = NF_PROJECT_CHUNK_SIZE + 2u;
        request.payload[0] = 1u; request.payload[1] = (uint8_t)i;
        memcpy(request.payload + 2u, blob + i * NF_PROJECT_CHUNK_SIZE,
               NF_PROJECT_CHUNK_SIZE);
        assert(transfer(&request, &reply) == 0);
        assert(!state.project[1].valid);
    }
    request.command = NF_CMD_PROJECT_IMPORT_COMMIT;
    request.length = 1u;
    assert(transfer(&request, &reply) == 0);
    assert(state.project[1].valid);
    state.project[1].valid = 0u;
    request.command = NF_CMD_PROJECT_IMPORT_BEGIN;
    assert(transfer(&request, &reply) == 0);
    for (i = 0; i < NF_PROJECT_CHUNKS; i++) {
        request.command = NF_CMD_PROJECT_IMPORT_CHUNK;
        request.length = NF_PROJECT_CHUNK_SIZE + 2u;
        request.payload[0] = 1u; request.payload[1] = (uint8_t)i;
        memcpy(request.payload + 2u, blob + i * NF_PROJECT_CHUNK_SIZE,
               NF_PROJECT_CHUNK_SIZE);
        if (i == 2u) request.payload[10] ^= 1u;
        assert(transfer(&request, &reply) == 0);
    }
    request.command = NF_CMD_PROJECT_IMPORT_COMMIT;
    request.length = 1u;
    assert(transfer(&request, &reply) != 0);
    assert(!state.project[1].valid);
    request.command = NF_CMD_PROJECT_IMPORT_ABORT;
    assert(transfer(&request, &reply) == 0);
    request.command = NF_CMD_PROJECT_EXPORT;
    request.length = 2u; request.payload[0] = 0u; request.payload[1] = 4u;
    assert(transfer(&request, &reply) == 0);
    assert(reply.length == NF_PROJECT_CHUNK_SIZE + 2u && reply.payload[1] == 4u);
    assert(!memcmp(reply.payload + 2u, blob + 4u * NF_PROJECT_CHUNK_SIZE,
                   NF_PROJECT_CHUNK_SIZE));
    request.command = NF_CMD_HELLO;
    request.length = 0u;
    n = nf_packet_encode(&request, corrupt, sizeof corrupt);
    corrupt[n - 1u] ^= 1u;
    for (i = 0; i + 1u < n; i++) assert(nf_link_feed(&link, corrupt[i], corrupt, sizeof corrupt) == 0);
    assert(nf_link_feed(&link, corrupt[n - 1u], corrupt, sizeof corrupt) == -1);
    assert(transfer(&request, &reply) == 0);
}

static void test_projects(void)
{
    uint8_t blob[NF_PROJECT_BLOB_SIZE];
    nf_init(&state);
    state.tempo = 96u;
    state.macros[2] = 77u;
    state.envelope_control[3][0] = 31u;
    state.voice_control[1][2] = 4u;
    state.track[0].steps[3] = (nf_step){66u, 101u, 2u, 75u};
    assert(nf_save_project(&state, 1u) == 0);
    assert(nf_export_project(&state, 1u, blob, sizeof blob) == sizeof blob);
    assert(nf_import_project(&state, 2u, blob, sizeof blob) == 0);
    state.tempo = 180u;
    state.macros[2] = 11u;
    state.envelope_control[3][0] = 2u;
    state.track[0].steps[3].note = 42u;
    assert(nf_load_project(&state, 1u) == 0);
    assert(state.tempo == 96u && state.track[0].steps[3].note == 66u &&
           state.macros[2] == 77u && state.envelope_control[3][0] == 31u &&
           state.voice_control[1][2] == 4u);
    assert(nf_load_project(&state, 2u) == 0);
    blob[20] ^= 1u;
    assert(nf_import_project(&state, 0u, blob, sizeof blob) == -3);
    state.project[1].track[0].steps[3].note ^= 1u;
    assert(nf_load_project(&state, 1u) == -2);
    assert(nf_load_project(&state, 0u) == -2);
    state.playing = 1u;
    assert(nf_save_project(&state, 0u) == -1);
}

static void test_project_control_ranges(void)
{
    uint8_t blob[NF_PROJECT_BLOB_SIZE];
    nf_init(&state);
    assert(nf_save_project(&state, 0u) == 0);
    assert(nf_export_project(&state, 0u, blob, sizeof blob) == sizeof blob);
    assert(nf_project_blob_valid(blob, sizeof blob));
    state.lfo_control[0] = 8u;
    assert(nf_save_project(&state, 0u) == -2);
    assert(nf_export_project(&state, 0u, blob, sizeof blob) == sizeof blob);
    assert(nf_project_blob_valid(blob, sizeof blob));
    state.lfo_control[0] = 3u;
    state.arp_control[3] = 3u;
    assert(nf_save_project(&state, 0u) == -2);
    assert(nf_export_project(&state, 0u, blob, sizeof blob) == sizeof blob);
    assert(nf_project_blob_valid(blob, sizeof blob));
    state.project[0].lfo_control[0] = 8u;
    state.project[0].checksum = nf_project_hash(&state.project[0]);
    assert(nf_export_project(&state, 0u, blob, sizeof blob) == sizeof blob);
    assert(!nf_project_blob_valid(blob, sizeof blob));
    state.project[0].lfo_control[0] = 3u;
    state.project[0].arp_control[3] = 3u;
    state.project[0].checksum = nf_project_hash(&state.project[0]);
    assert(nf_export_project(&state, 0u, blob, sizeof blob) == sizeof blob);
    assert(!nf_project_blob_valid(blob, sizeof blob));
}

static void test_power_loss_journal(void)
{
    nf_store_io io = {NULL, flash_read, flash_erase, flash_program};
    unsigned interruption;
    memset(flash.data, 0xff, sizeof flash.data);
    flash.fail_after = -1;
    nf_store_init(&store, io, 0u);
    nf_init(&state);
    assert(nf_store_load(&store, &state, 0u) == -2);
    state.tempo = 96u;
    assert(nf_save_project(&state, 0u) == 0);
    assert(nf_store_save(&store, &state, 0u) == 0);
    memcpy(flash_baseline, flash.data, sizeof flash_baseline);
    state.tempo = 172u;
    assert(nf_save_project(&state, 0u) == 0);
    for (interruption = 0u; interruption < 7u; interruption++) {
        memcpy(flash.data, flash_baseline, sizeof flash.data);
        flash.fail_after = (int)interruption;
        assert(nf_store_save(&store, &state, 0u) == -3);
        flash.fail_after = -1;
        state.project[0].valid = 0u;
        assert(nf_store_load(&store, &state, 0u) == 0);
        assert(nf_load_project(&state, 0u) == 0 && state.tempo == 96u);
        state.tempo = 172u;
        assert(nf_save_project(&state, 0u) == 0);
    }
    memcpy(flash.data, flash_baseline, sizeof flash.data);
    flash.fail_after = -1;
    state.project[0].valid = 0u;
    assert(nf_store_load(&store, &state, 0u) == 0);
    assert(nf_load_project(&state, 0u) == 0 && state.tempo == 96u);
    state.tempo = 172u;
    assert(nf_save_project(&state, 0u) == 0);
    assert(nf_store_save(&store, &state, 0u) == 0);
    state.project[0].valid = 0u;
    assert(nf_store_load(&store, &state, 0u) == 0);
    assert(nf_load_project(&state, 0u) == 0 && state.tempo == 172u);
    state.playing = 1u;
    assert(nf_store_save(&store, &state, 0u) == -1);
}
static void test_fx_chain(void)
{
    int32_t baseline[4096];
    unsigned kind, i, changed;
    nf_init(&state);
    for (kind = 0; kind < NF_FX_COUNT; kind++) state.fx[kind].enabled = 0u;
    for (i = 0; i < 4096u; i++)
        baseline[i] = nf_process_fx(&state, i % 64u < 32u ? 5000 : -5000, 4000);
    for (kind = 0; kind < NF_FX_COUNT; kind++) {
        nf_init(&state);
        for (i = 0; i < NF_FX_COUNT; i++) state.fx[i].enabled = i == kind;
        state.fx[kind].value[3] = 100u;
        if (kind == NF_DELAY) state.fx[kind].value[0] = 0u;
        changed = 0u;
        for (i = 0; i < 4096u; i++) {
            int32_t v = nf_process_fx(&state, i % 64u < 32u ? 5000 : -5000, 4000);
            assert(v >= -32768 && v <= 32767);
            changed |= v != baseline[i];
        }
        if (!changed) fprintf(stderr, "FX kind %u had no output change\n", kind);
        assert(changed);
    }
}
static void test_fx_parameters(void)
{
    unsigned kind, param, i, changed;
    int32_t before[4096];
    for (kind = 0; kind < NF_FX_COUNT; kind++)
        for (param = 0; param < 4u; param++) {
            nf_init(&state);
            for (i = 0; i < NF_FX_COUNT; i++) state.fx[i].enabled = i == kind;
            for (i = 0; i < 4u; i++) state.fx[kind].value[i] = 70u;
            if (kind == NF_DELAY) state.fx[kind].value[0] = 0u;
            state.fx[kind].value[param] = 0u;
            for (i = 0; i < 4096u; i++)
                before[i] = nf_process_fx(&state, i % 117u < 44u ? 6000 : -4000, 6000);
            nf_init(&state);
            for (i = 0; i < NF_FX_COUNT; i++) state.fx[i].enabled = i == kind;
            for (i = 0; i < 4u; i++) state.fx[kind].value[i] = 70u;
            if (kind == NF_DELAY) state.fx[kind].value[0] = 0u;
            state.fx[kind].value[param] = 127u;
            changed = 0u;
            for (i = 0; i < 4096u; i++)
                changed |= before[i] != nf_process_fx(&state,
                    i % 117u < 44u ? 6000 : -4000, 6000);
            if (!changed) fprintf(stderr, "FX %u parameter %u inert\n", kind, param);
            assert(changed);
        }
}
static void test_live_controls_and_preset(void)
{
    uint32_t slow, fast, phase;
    unsigned i, audible = 0u;
    assert(nf_load_preset(&state, 3u) == -1);
    nf_init(&state);
    assert(nf_load_preset(&state, 1u) == 0);
    assert(state.tempo == 112u && state.track[2].steps[0].velocity != 0u);
    assert(state.track[0].instrument == NF_DRUM &&
           state.track[2].instrument == NF_PAD);
    state.envelope_control[3][0] = 100u;
    nf_note_on(&state, 3u, 72u, 110u, NF_KEY);
    nf_render_audio(&state, audio, 512u);
    slow = (uint32_t)state.voice[0].envelope;
    assert(state.scope[31] > 0u);
    nf_init(&state);
    state.envelope_control[3][0] = 0u;
    nf_note_on(&state, 3u, 72u, 110u, NF_KEY);
    nf_render_audio(&state, audio, 512u);
    fast = (uint32_t)state.voice[0].envelope;
    assert(fast > slow);
    nf_init(&state);
    state.voice_control[3][3] = 0u;
    nf_note_on(&state, 3u, 72u, 110u, NF_KEY);
    nf_render_audio(&state, audio, 512u);
    for (i = 0u; i < 1024u; i++) audible |= audio[i] != 0;
    assert(!audible);
    phase = state.lfo_phase;
    nf_render_audio(&state, audio, 512u);
    assert(state.lfo_phase != phase);
}
static void test_web_controls(void)
{
    nf_packet request = {.command = NF_CMD_CONTROL, .length = 4u}, reply;
    nf_init(&state);
    request.payload[0] = 2u;
    request.payload[3] = 2u;
    assert(nf_packet_apply(&state, &request, &reply) == 0);
    assert(state.preset == 2u && state.tempo == 84u);
    request.payload[0] = 3u;
    request.payload[1] = 2u;
    request.payload[3] = 70u;
    assert(nf_packet_apply(&state, &request, &reply) == 0);
    assert(state.macros[2] == 70u && state.fx[NF_DELAY].value[3] == 37u);
    request.payload[0] = 5u;
    request.payload[1] = 3u;
    request.payload[2] = 0u;
    request.payload[3] = 29u;
    assert(nf_packet_apply(&state, &request, &reply) == 0);
    assert(state.envelope_control[3][0] == 29u);
    request.payload[3] = 101u;
    assert(nf_packet_apply(&state, &request, &reply) == -1);
    assert(state.envelope_control[3][0] == 29u);
    request.payload[0] = 16u;
    request.payload[1] = request.payload[2] = 0u;
    request.payload[3] = 64u;
    assert(nf_packet_apply(&state, &request, &reply) == 0);
    assert(state.pattern_length == 64u);
    request.payload[3] = 17u;
    assert(nf_packet_apply(&state, &request, &reply) == -1);
    assert(state.pattern_length == 64u);
    request.payload[0] = 17u;
    request.payload[3] = 1u;
    assert(nf_packet_apply(&state, &request, &reply) == 0);
    assert(state.monitor_mute_until_ms == state.now_ms + 2500u);
    request.payload[3] = 0u;
    assert(nf_packet_apply(&state, &request, &reply) == 0);
    assert(state.monitor_mute_until_ms == 0u);
    request.command = NF_CMD_DETAIL;
    request.length = 0u;
    assert(nf_packet_apply(&state, &request, &reply) == 0);
    assert(reply.length == 117u);
    assert(reply.payload[1] == 84u && reply.payload[7] == 2u);
    assert(reply.payload[18] == 70u);
    assert(reply.payload[115] == 64u);
}
static void test_original_sound_bank(void)
{
    nf_init(&state);
    assert(nf_load_preset(&state, 0u) == 0);
    assert(!strcmp(nf_patch_name(0u, 2u), "NOIR BRASS"));
    assert(state.voice_control[2][0] == 2u && state.fx[NF_DRIVE].enabled == 0u);
    assert(nf_load_preset(&state, 1u) == 0);
    assert(state.voice_control[1][0] == 1u && state.fx[NF_DRIVE].enabled == 1u);
    assert(nf_load_preset(&state, 2u) == 0);
    assert(!strcmp(nf_patch_name(2u, 3u), "RAIN BELL"));
    assert(state.voice_control[3][0] == 4u && state.macros[2] == 83u);
    assert(nf_save_project(&state, 0u) == 0);
    assert(nf_export_project(&state, 0u, link.staging, sizeof link.staging) == NF_PROJECT_BLOB_SIZE);
    assert(nf_project_blob_valid(link.staging, NF_PROJECT_BLOB_SIZE));
}
static void test_every_page_render(void)
{
    unsigned page, i;
    nf_init(&state);
    assert(nf_load_preset(&state, 2u) == 0);
    nf_note_on(&state, 3u, 120u, 127u, NF_KEY);
    nf_render_audio(&state, audio, 512u);
    for (page = NF_HOME; page <= NF_SCALE_PAGE; page++) {
        unsigned colored = 0u;
        state.page = (uint8_t)page;
        nf_draw_city(&state, pixels);
        for (i = 0u; i < NF_WIDTH * NF_HEIGHT; i++)
            colored += pixels[i] != pixels[0];
        assert(colored > 500u);
    }
    state.page = NF_HOME;
    state.home_view = 1u;
    nf_draw_city(&state, pixels);
    {
        unsigned phosphor = 0u;
        for (i = 0u; i < NF_WIDTH * NF_HEIGHT; i++)
            phosphor += pixels[i] == 0x4fecu; /* #48ff66 RGB565, saturated green */
        assert(phosphor > 30u);
    }
}
int main(void)
{
    test_note_and_city();
    test_polyphony_and_sequence();
    test_step_recording();
    test_overlapping_note_release();
    test_live_voice_priority();
    test_live_audition_on_muted_track();
    test_performance_macros();
    test_protocol();
    test_live_snapshot();
    test_link_project_transfer();
    test_projects();
    test_project_control_ranges();
    test_power_loss_journal();
    test_fx_chain();
    test_fx_parameters();
    test_live_controls_and_preset();
    test_web_controls();
    test_original_sound_bank();
    test_every_page_render();
    puts("NIGHTFALL host tests passed");
    return 0;
}
