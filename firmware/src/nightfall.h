#ifndef NIGHTFALL_H
#define NIGHTFALL_H
#include <stddef.h>
#include <stdint.h>

#define NF_TRACKS 4u
#define NF_VOICES 8u
#define NF_STEPS 64u
#define NF_EVENTS 64u
#define NF_PROJECT_SLOTS 3u
#define NF_PROJECT_BLOB_SIZE 1260u
#define NF_WIDTH 240u
#define NF_HEIGHT 240u
#define NF_SAMPLE_RATE 44118u
#define NF_TERRAIN_BANDS 20u
#define NF_TERRAIN_ROWS 20u
#define NF_TERRAIN_SAMPLES 256u

typedef enum { NF_KEY, NF_MIDI, NF_SEQ, NF_WEB } nf_source;
typedef enum { NF_BASS, NF_PAD, NF_LEAD, NF_DRUM } nf_instrument;
typedef enum { NF_HOME, NF_FX_PAGE, NF_SAVE_PAGE, NF_ENV_PAGE, NF_LFO_PAGE,
               NF_EDIT_PAGE, NF_GLO_PAGE, NF_SEQ_PAGE, NF_ARP_PAGE,
               NF_SCALE_PAGE } nf_page;
typedef enum { NF_FILTER, NF_DRIVE, NF_CHORUS, NF_PHASER, NF_DELAY, NF_REVERB,
               NF_FX_COUNT } nf_fx_kind;

typedef struct {
    uint8_t enabled, value[4];
} nf_fx;

typedef struct {
    uint8_t note, velocity, length, chance;
} nf_step;

typedef struct {
    uint8_t instrument, level, muted, delay_send;
    nf_step steps[NF_STEPS];
} nf_track;

typedef struct {
    uint8_t active, gate, note, velocity, track, source, stage;
    uint32_t phase, detune_phase, increment, serial, noise;
    int32_t envelope, noise_z;
} nf_voice;

typedef struct {
    uint32_t serial, onset_ms, ended_ms, duration_ms;
    uint8_t live, note, velocity, track, source, envelope;
} nf_event;

typedef struct {
    uint32_t checksum;
    uint16_t tempo, pattern_length;
    uint8_t valid, preset, auto_arrange, scene, scale, arp_enabled,
            home_view, selected_track;
    uint8_t macros[4], envelope_control[NF_TRACKS][4], lfo_control[4],
            voice_control[NF_TRACKS][4], arp_control[4], edited[NF_TRACKS];
    nf_track track[NF_TRACKS];
    nf_fx fx[NF_FX_COUNT];
} nf_project;

typedef struct {
    nf_track track[NF_TRACKS];
    nf_voice voice[NF_VOICES];
    nf_event event[NF_EVENTS];
    nf_fx fx[NF_FX_COUNT];
    nf_project project[NF_PROJECT_SLOTS];
    uint32_t now_ms, serial, frame, step_clock, delay_pos, random;
    /* Session-only monitor mute expires unless the browser keeps renewing it. */
    uint32_t monitor_mute_until_ms;
    uint16_t tempo, step, pattern_length, bar;
    uint8_t playing, recording, next_event, selected_track, page, selected_fx, selected_project;
    uint8_t preset, home_view, auto_arrange, scene, queued_scene, scale,
            arp_enabled, save_op, transport_started, seq_focus, fill_queued,
            fill_active, arp_position;
    uint8_t macros[4], envelope_control[NF_TRACKS][4], lfo_control[4],
            voice_control[NF_TRACKS][4], arp_control[4], edited[NF_TRACKS];
    uint8_t scope[32];
    /* Pre-MASTER audio, decimated to ~11 kHz for the HOME spectrum. The
     * visual history is updated off the audio interrupt by nf_update_terrain. */
    int16_t terrain_samples[NF_TERRAIN_SAMPLES];
    uint16_t terrain_write;
    uint32_t terrain_last_ms;
    uint8_t terrain_decimator, terrain_head, terrain_serial;
    uint8_t terrain[NF_TERRAIN_ROWS][NF_TERRAIN_BANDS];
    char ui_notice[32];
    uint32_t ui_notice_until_ms;
    int16_t delay[12000];
    int16_t chorus[2048], reverb_a[2048], reverb_b[2048];
    int32_t filter_z, drive_z, phaser_in[2], phaser_out[2];
    int32_t reverb_lp_a, reverb_lp_b;
    uint32_t chorus_pos, chorus_phase, phaser_phase, reverb_pos_a, reverb_pos_b;
    uint32_t lfo_phase;
    int32_t tone_z;
    uint32_t audio_underruns;
    uint8_t master_level;
} nf_state;

void nf_init(nf_state *s);
uint32_t nf_note_on_id(nf_state *s, uint8_t track, uint8_t note,
                       uint8_t velocity, nf_source source);
void nf_note_off_id(nf_state *s, uint32_t serial);
void nf_note_on(nf_state *s, uint8_t track, uint8_t note, uint8_t velocity, nf_source source);
void nf_note_off(nf_state *s, uint8_t track, uint8_t note, nf_source source);
void nf_set_macro(nf_state *s, unsigned index, uint8_t value);
void nf_toggle_play(nf_state *s);
int nf_load_preset(nf_state *s, unsigned preset);
const char *nf_patch_name(unsigned preset, unsigned track);
void nf_tick_ms(nf_state *s, uint32_t milliseconds);
void nf_render_audio(nf_state *s, int16_t *stereo, size_t frames);
void nf_update_terrain(nf_state *s);
int32_t nf_process_fx(nf_state *s, int32_t dry, int32_t send);
int nf_save_project(nf_state *s, unsigned slot);
int nf_load_project(nf_state *s, unsigned slot);
uint32_t nf_project_hash(const nf_project *project);
size_t nf_export_project(const nf_state *s, unsigned slot, uint8_t *out, size_t capacity);
int nf_import_project(nf_state *s, unsigned slot, const uint8_t *data, size_t length);
int nf_project_blob_valid(const uint8_t *data, size_t length);
void nf_draw_city(const nf_state *s, uint16_t pixels[NF_WIDTH * NF_HEIGHT]);

/* The transport (USB CDC/HID, WebUSB, etc.) must carry these bytes unchanged. */
#define NF_PACKET_MAGIC 0x4e46u
#define NF_PACKET_VERSION 1u
#define NF_PACKET_MAX_PAYLOAD 240u
typedef struct {
    uint8_t command, sequence;
    uint16_t length;
    uint8_t payload[NF_PACKET_MAX_PAYLOAD];
} nf_packet;
typedef enum {
    NF_CMD_HELLO = 1, NF_CMD_STATUS, NF_CMD_NOTE_ON, NF_CMD_NOTE_OFF,
    NF_CMD_TEMPO, NF_CMD_FX, NF_CMD_STEP, NF_CMD_PLAY,
    NF_CMD_PROJECT_SAVE, NF_CMD_PROJECT_LOAD,
    NF_CMD_PROJECT_EXPORT, NF_CMD_PROJECT_IMPORT_BEGIN,
    NF_CMD_PROJECT_IMPORT_CHUNK, NF_CMD_PROJECT_IMPORT_COMMIT,
    NF_CMD_PROJECT_IMPORT_ABORT, NF_CMD_CONTROL, NF_CMD_DETAIL,
    NF_CMD_LIVE
} nf_command;
size_t nf_packet_encode(const nf_packet *packet, uint8_t *out, size_t capacity);
int nf_packet_decode(nf_packet *packet, const uint8_t *data, size_t length);
int nf_packet_apply(nf_state *state, const nf_packet *request, nf_packet *reply);

#define NF_PACKET_FRAME_MAX (NF_PACKET_MAX_PAYLOAD + 9u)
#define NF_PROJECT_CHUNK_SIZE 180u
#define NF_PROJECT_CHUNKS (NF_PROJECT_BLOB_SIZE / NF_PROJECT_CHUNK_SIZE)
typedef struct {
    uint8_t bytes[NF_PACKET_FRAME_MAX];
    uint16_t used, expected;
} nf_stream;
typedef struct {
    nf_state *state;
    nf_stream stream;
    uint8_t staging[NF_PROJECT_BLOB_SIZE];
    uint8_t received, upload_slot, uploading;
} nf_link;
void nf_link_init(nf_link *link, nf_state *state);
/* One byte at a time because USB bulk reads may split or join frames.
 * Returns encoded reply size, zero while collecting a frame, negative on an
 * invalid frame or undersized reply buffer. */
int nf_link_feed(nf_link *link, uint8_t byte, uint8_t *reply, size_t capacity);

/* A/B NOR journal for three projects. The caller owns the six 4 KiB sectors
 * beginning at base and must provide erase/program/read operations. This
 * module never touches flash controller registers or the bootloader area. */
#define NF_STORE_SECTOR_SIZE 4096u
#define NF_STORE_SECTORS (NF_PROJECT_SLOTS * 2u)
typedef struct {
    void *context;
    int (*read)(void *context, uint32_t offset, uint8_t *out, size_t length);
    int (*erase)(void *context, uint32_t sector_offset);
    int (*program)(void *context, uint32_t offset, const uint8_t *data, size_t length);
} nf_store_io;
typedef struct {
    nf_store_io io;
    uint32_t base;
    uint8_t scratch[NF_PROJECT_BLOB_SIZE];
} nf_store;
void nf_store_init(nf_store *store, nf_store_io io, uint32_t base);
int nf_store_load(nf_store *store, nf_state *state, unsigned slot);
int nf_store_save(nf_store *store, const nf_state *state, unsigned slot);

#endif
