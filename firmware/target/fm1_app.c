/* SPDX-License-Identifier: GPL-3.0-only
 * NIGHTFALL application port. The unmodified FM-1 board-support headers and
 * startup objects are build-time GPL dependencies; synthesis and UI are ours. */
#define FM1_CPU1_USED 0
#include <stdint.h>
#include "nightfall.h"
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"
#include "nf_usb.h"
#ifdef NF_RECOVERY_MIDI
/* The FM-1 IRQ/guard HAL is single-translation-unit board support. */
#include "nf_usb_midi.c"
#endif

#define AUDIO_FRAMES 128u
#define AUDIO_WORDS (AUDIO_FRAMES * 2u)
#define KEY_BASE 53

extern uint32_t _data_start[], _data_end[], _data_load[];
extern uint32_t _bss_start[], _bss_end[], _pool_start[], _pool_end[];
extern uint32_t _rt_start[], _rt_end[], _rt_load[];
extern void isr_timer5(void), isr_alnk0(void);

static nf_state app __attribute__((section(".pool")));
static nf_state visual __attribute__((section(".pool")));
static nf_link link __attribute__((section(".pool")));
static uint8_t link_reply[NF_PACKET_FRAME_MAX] __attribute__((section(".pool")));
static uint16_t canvas[NF_WIDTH * NF_HEIGHT] __attribute__((section(".pool"), aligned(4)));
static int32_t dma_audio[2u * AUDIO_WORDS] __attribute__((section(".pool"), aligned(4)));
static int16_t mix_audio[AUDIO_WORDS] __attribute__((section(".pool"), aligned(4)));
static volatile uint32_t clock_ms, audio_late;
/* PB6/ADC ch4 is the physical MASTER potentiometer. Keep this after the
 * synth/FX mix so the HOME scope and city remain intentionally pre-master. */
static volatile uint32_t master_gain_q12 = 1024u;
static int32_t master_raw_x16 = 512 * 16;
static uint8_t lcd_pending, lcd_small[8];
static int8_t octave;
static uint8_t held_pitch[27], held_track[27], full_redraw, header_dirty;
static uint8_t held_chord[27];
static uint32_t held_serial[27][3];
static uint32_t held_min_until[27];
static uint8_t held_is_arp[27], arp_active, arp_note, arp_track, arp_index;
static uint32_t arp_serial;
static uint32_t arp_next_ms, arp_release_ms;
static uint32_t held_notes, command_notes;
static uint8_t overwrite_slot = 0xffu;
static uint32_t overwrite_until_ms;
static const uint8_t white_key[16] = {
    0u, 2u, 4u, 6u, 7u, 9u, 11u, 12u,
    14u, 16u, 18u, 19u, 21u, 23u, 24u, 26u
};
static struct { uint32_t magic, pending, failed; } boot_state __attribute__((section(".noinit")));

/* A fault enters ROM recovery. A second early boot failure also enters ROM
 * recovery, so an experimental app cannot trap the owner in a reboot loop. */
static void fm1_fault(const fm1_crash_t *record)
{
    (void)record;
    fm1_audio_stop();
    fm1_enter_uboot();
}

void fm1_timer5_irq(void)
{
    static uint8_t sub, usb_sub;
    fm1_timer5_ack();
    fm1_input_tick();
    if (++usb_sub == 5u) { usb_sub = 0u; nf_usb_poll(); }
    if (++sub == 10u) {
        sub = 0u;
        clock_ms++;
#ifdef NF_RECOVERY_MIDI
        nf_usb_set_ms(clock_ms);
#endif
    }
}

void fm1_alnk0_irq(void)
{
    uint8_t pending = fm1_audio_pending();
    fm1_audio_ack_aux(pending);
    if (pending & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), i;
        uint32_t gain = master_gain_q12;
        /* Web-only monitoring is session-scoped. A lost browser heartbeat
         * restores the physical output without touching the MASTER knob. */
        if ((int32_t)(app.monitor_mute_until_ms - clock_ms) > 0) gain = 0u;
        int32_t *dest = dma_audio + half * AUDIO_WORDS;
        nf_render_audio(&app, mix_audio, AUDIO_FRAMES);
        for (i = 0; i < AUDIO_WORDS; i++)
            dest[i] = ((int32_t)mix_audio[i] * (int32_t)gain / 4096) * 128;
        fm1_audio_ack_half();
        if (fm1_audio_free_half() != half) audio_late++;
    }
}

static void master_boot(void)
{
    uint32_t i;
    int32_t raw;
    fm1_adc_init();
    (void)fm1_adc_read(FM1_ADC_MASTER); /* discard first conversion after pin setup */
    for (i = 0u; i < 3u; i++) {
        raw = fm1_adc_read(FM1_ADC_MASTER);
        if (raw >= 0) {
            master_raw_x16 = raw * 16;
            master_gain_q12 = (uint32_t)(raw * raw) >> 8;
            return;
        }
    }
    /* ADC failure: start at a modest level, not an unexpected full-volume hit. */
    master_raw_x16 = 512 * 16;
    master_gain_q12 = 1024u;
}

static void master_poll(void)
{
    int32_t raw = fm1_adc_read(FM1_ADC_MASTER);
    int32_t target, delta;
    uint32_t smooth;
    if (raw < 0) return;
    target = raw * 16;
    delta = target - master_raw_x16;
    if (delta > -8 && delta < 8) master_raw_x16 = target;
    else master_raw_x16 += delta / 8;
    smooth = (uint32_t)(master_raw_x16 / 16);
    master_gain_q12 = (smooth * smooth) >> 8;
    app.master_level = (uint8_t)(smooth * 100u / 1023u);
}

static void lcd_sync(void)
{
    if (!lcd_pending) return;
    fm1_lcd_wait();
    fm1_lcd_deselect();
    lcd_pending = 0u;
}
static void lcd_cmd(uint8_t c)
{
    lcd_sync();
    fm1_lcd_send_cmd(c);
    fm1_lcd_wait();
    fm1_lcd_deselect();
}
static void lcd_data(const void *p, uint32_t bytes)
{
    if (!bytes) return;
    lcd_sync();
    fm1_lcd_send_data(p, bytes);
    lcd_pending = 1u;
}
static void lcd_window(uint32_t y, uint32_t h)
{
    lcd_cmd(0x2au);
    lcd_small[0] = lcd_small[1] = 0u;
    lcd_small[2] = 0u; lcd_small[3] = 239u;
    lcd_data(lcd_small, 4u);
    lcd_cmd(0x2bu);
    lcd_small[0] = (uint8_t)(y >> 8); lcd_small[1] = (uint8_t)y;
    lcd_small[2] = (uint8_t)((y + h - 1u) >> 8);
    lcd_small[3] = (uint8_t)(y + h - 1u);
    lcd_data(lcd_small, 4u);
    lcd_cmd(0x2cu);
}
static void lcd_init(void)
{
    fm1_lcd_hw_init();
    fm1_delay_ms(150u);
    lcd_cmd(0x01u); fm1_delay_ms(150u);
    lcd_cmd(0x11u); fm1_delay_ms(120u);
    lcd_cmd(0x3au); lcd_small[0] = 0x55u; lcd_data(lcd_small, 1u);
    lcd_cmd(0x36u); lcd_small[0] = 0u; lcd_data(lcd_small, 1u);
    lcd_cmd(0x21u); lcd_cmd(0x13u); lcd_cmd(0x29u);
    fm1_lcd_baud(4u);
}
static void lcd_prepare(void)
{
    uint32_t i;
    lcd_sync();                       /* previous DMA must finish before drawing */
    nf_draw_city(&visual, canvas);
    for (i = 0u; i < NF_WIDTH * NF_HEIGHT; i++) {
        uint16_t c = canvas[i];
        canvas[i] = (uint16_t)((c >> 8) | (c << 8));
    }
}
static void lcd_flush_region(uint32_t y, uint32_t h)
{
    lcd_window(y, h);
    lcd_data(canvas + y * NF_WIDTH, h * NF_WIDTH * 2u);
}
static void lcd_present(uint32_t y, uint32_t h)
{
    lcd_prepare();
    lcd_flush_region(y, h);
}

static void snapshot(void)
{
    uint32_t i;
    fm1_irq_off();
    visual.now_ms = app.now_ms;
    visual.playing = app.playing;
    visual.recording = app.recording;
    visual.tempo = app.tempo;
    visual.step = app.step;
    visual.bar = app.bar;
    visual.selected_track = app.selected_track;
    visual.page = app.page;
    visual.selected_fx = app.selected_fx;
    visual.selected_project = app.selected_project;
    visual.preset = app.preset;
    visual.home_view = app.home_view;
    visual.auto_arrange = app.auto_arrange;
    visual.scene = app.scene;
    visual.queued_scene = app.queued_scene;
    visual.scale = app.scale;
    visual.arp_enabled = app.arp_enabled;
    visual.save_op = app.save_op;
    visual.seq_focus = app.seq_focus;
    visual.fill_queued = app.fill_queued;
    visual.fill_active = app.fill_active;
    visual.arp_position = app.arp_position;
    visual.ui_notice_until_ms = app.ui_notice_until_ms;
    for (i = 0u; i < sizeof visual.ui_notice; i++)
        visual.ui_notice[i] = app.ui_notice[i];
    for (i = 0; i < 4u; i++) {
        visual.macros[i] = app.macros[i];
        visual.lfo_control[i] = app.lfo_control[i];
        visual.arp_control[i] = app.arp_control[i];
    }
    for (i = 0; i < NF_FX_COUNT; i++) visual.fx[i] = app.fx[i];
    for (i = 0; i < NF_PROJECT_SLOTS; i++) visual.project[i].valid = app.project[i].valid;
    for (i = 0; i < NF_TRACKS; i++) {
        visual.track[i].level = app.track[i].level;
        visual.track[i].muted = app.track[i].muted;
        for (uint32_t j = 0; j < 4u; j++) {
            visual.envelope_control[i][j] = app.envelope_control[i][j];
            visual.voice_control[i][j] = app.voice_control[i][j];
        }
        for (uint32_t j = 0; j < 16u; j++)
            visual.track[i].steps[j] = app.track[i].steps[j];
    }
    for (i = 0; i < NF_VOICES; i++) visual.voice[i] = app.voice[i];
    for (i = 0; i < NF_EVENTS; i++) visual.event[i] = app.event[i];
    for (i = 0; i < 32u; i++) visual.scope[i] = app.scope[i];
    visual.terrain_head = app.terrain_head;
    visual.terrain_serial = app.terrain_serial;
    visual.terrain_last_ms = app.terrain_last_ms;
    for (i = 0u; i < NF_TERRAIN_ROWS; i++)
        for (uint32_t j = 0u; j < NF_TERRAIN_BANDS; j++)
            visual.terrain[i][j] = app.terrain[i][j];
    fm1_irq_on();
}

static int32_t bounded(int32_t value, int32_t lo, int32_t hi)
{
    return value < lo ? lo : value > hi ? hi : value;
}

static void set_notice(const char *message)
{
    unsigned i = 0u;
    while (message[i] && i + 1u < sizeof app.ui_notice) {
        app.ui_notice[i] = message[i];
        i++;
    }
    app.ui_notice[i] = 0;
    app.ui_notice_until_ms = app.now_ms + 1700u;
}

static void set_notice_text(const char *label, const char *value)
{
    char message[32];
    unsigned n = 0u, j = 0u;
    while (label[n] && n + 2u < sizeof message) {
        message[n] = label[n];
        n++;
    }
    message[n++] = ' ';
    while (value[j] && n + 1u < sizeof message) message[n++] = value[j++];
    message[n] = 0;
    set_notice(message);
}

static void set_notice_value(const char *label, int32_t value)
{
    char message[32], digits[12];
    unsigned n = 0u, d = 0u;
    uint32_t number;
    while (label[n] && n + 1u < sizeof message) {
        message[n] = label[n];
        n++;
    }
    if (n + 2u >= sizeof message) return;
    message[n++] = ' ';
    if (value < 0) { message[n++] = '-'; number = (uint32_t)(-value); }
    else number = (uint32_t)value;
    do { digits[d++] = (char)('0' + number % 10u); number /= 10u; }
    while (number && d < sizeof digits);
    while (d && n + 1u < sizeof message) message[n++] = digits[--d];
    message[n] = 0;
    set_notice(message);
}

static void set_notice_pitch(uint8_t note)
{
    static const char *const names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    char label[5];
    const char *name = names[note % 12u];
    unsigned n = 0u, octave = note / 12u;
    while (*name) label[n++] = *name++;
    if (octave == 0u) { label[n++] = '-'; label[n++] = '1'; }
    else {
        octave--;
        if (octave >= 10u) label[n++] = '1';
        label[n++] = (char)('0' + octave % 10u);
    }
    label[n] = 0;
    set_notice_text("PITCH", label);
}

static void notice_knob(unsigned i)
{
    static const char *const fx_labels[NF_FX_COUNT][4] = {
        {"CUTOFF", "RESO", "MOTION", "MIX"},
        {"DRIVE", "TONE", "BODY", "MIX"},
        {"RATE", "DEPTH", "WIDTH", "MIX"},
        {"RATE", "DEPTH", "TONE", "MIX"},
        {"TIME", "FDBK", "TONE", "MIX"},
        {"SIZE", "DAMP", "PRE", "MIX"}
    };
    static const char *const env_labels[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
    static const char *const edit_labels[4] = {"WAVE", "SPREAD", "OCTAVE", "LEVEL"};
    static const char *const lfo_labels[4] = {"RATE", "DEPTH", "TARGET", "SHAPE"};
    static const char *const arp_labels[4] = {"ORDER", "DIV", "GATE", "OCTAVE"};
    static const char *const track_labels[4] = {"DRUM LEVEL", "BASS LEVEL", "CHORD LEVEL", "LEAD LEVEL"};
    static const char *const waves[5] = {"SINE", "TRI", "SAW", "SQR", "METAL"};
    static const char *const octaves[5] = {"-2", "-1", "0", "+1", "+2"};
    static const char *const rates[8] = {"4BAR", "2BAR", "1BAR", "1/2", "1/4", "1/8", "1/16", "1/32"};
    static const char *const destinations[3] = {"TONE", "AMP", "PITCH"};
    static const char *const shapes[4] = {"SINE", "TRI", "SQR", "SAW"};
    static const char *const orders[4] = {"UP", "DOWN", "UP/DN", "RANDOM"};
    static const char *const divisions[5] = {"1/4", "1/8", "1/16", "1/32", "1/64"};
    static const char *const arp_octaves[3] = {"0", "+1", "+2"};
    if (app.page == NF_HOME) {
        static const char *const labels[4] = {"BRIGHT", "DENSITY", "SPACE", "MOTION"};
        set_notice_value(labels[i], app.macros[i]);
    } else if (app.page == NF_FX_PAGE)
        set_notice_value(fx_labels[app.selected_fx][i], app.fx[app.selected_fx].value[i]);
    else if (app.page == NF_ENV_PAGE)
        set_notice_value(env_labels[i], app.envelope_control[app.selected_track][i]);
    else if (app.page == NF_EDIT_PAGE) {
        unsigned v = app.voice_control[app.selected_track][i];
        if (i == 0u) set_notice_text("WAVE", waves[v]);
        else if (i == 2u) set_notice_text("OCTAVE", octaves[v]);
        else set_notice_value(edit_labels[i], v);
    } else if (app.page == NF_LFO_PAGE) {
        unsigned v = app.lfo_control[i];
        if (i == 0u) set_notice_text("RATE", rates[v]);
        else if (i == 2u) set_notice_text("TARGET", destinations[v]);
        else if (i == 3u) set_notice_text("SHAPE", shapes[v]);
        else set_notice_value(lfo_labels[i], v);
    } else if (app.page == NF_ARP_PAGE) {
        unsigned v = app.arp_control[i];
        if (i == 0u) set_notice_text("ORDER", orders[v]);
        else if (i == 1u) set_notice_text("DIV", divisions[v]);
        else if (i == 3u) set_notice_text("OCTAVE", arp_octaves[v]);
        else set_notice_value(arp_labels[i], v);
    } else if (app.page == NF_GLO_PAGE)
        set_notice_value(track_labels[i], app.track[i].level);
    else if (app.page == NF_SEQ_PAGE) {
        const nf_step *step = &app.track[app.selected_track].steps[app.seq_focus];
        if (i == 0u) set_notice_pitch(step->note);
        else if (i == 1u) set_notice_value("VELOCITY", step->velocity);
        else if (i == 2u) set_notice_value("LENGTH", step->length);
        else set_notice_value("CHANCE", step->chance);
    } else if (app.page == NF_SAVE_PAGE)
        set_notice(app.save_op ? "LOAD / SELECT SLOT" : "SAVE / SELECT SLOT");
    else set_notice("SEL: CHANGE SCALE");
}

static void toggle_page(uint8_t page)
{
    fm1_irq_off();
    if (app.page == page) {
        if (page == NF_HOME) app.home_view ^= 1u;
        else app.page = NF_HOME;
    } else app.page = page;
    fm1_irq_on();
    if (app.page == NF_GLO_PAGE || app.page == NF_SEQ_PAGE)
        set_notice("KEYS: EDIT / HOME: PLAY");
    full_redraw = 1u;
}

static int white_index(unsigned chromatic)
{
    unsigned j;
    for (j = 0u; j < 16u; j++) if (white_key[j] == chromatic) return (int)j;
    return -1;
}

static uint8_t scale_note(uint8_t note)
{
    static const uint8_t minor[12] = {1u, 0u, 1u, 1u, 0u, 1u,
                                      0u, 1u, 1u, 0u, 1u, 0u};
    unsigned root = app.preset == 0u ? 9u : app.preset == 1u ? 2u : 4u;
    if (app.scale == 0u || app.selected_track == 0u) return note;
    while (note && !minor[((unsigned)note + 12u - root) % 12u]) note--;
    return note;
}

static uint8_t performance_note(unsigned key, uint8_t melodic)
{
    static const uint8_t drums[4] = {36u, 38u, 42u, 46u};
    int white = white_index(key);
    if (app.selected_track == 0u)
        return drums[(unsigned)(white >= 0 ? white : (int)key) & 3u];
    return scale_note(melodic);
}

static void release_key(unsigned i)
{
    if (held_is_arp[i]) {
        held_is_arp[i] = 0u;
        held_serial[i][0] = held_serial[i][1] = held_serial[i][2] = 0u;
        return;
    }
    nf_note_off_id(&app, held_serial[i][0]);
    nf_usb_emit_note((uint8_t)(0x80u | (held_track[i] & 3u)), held_pitch[i], 0u);
    header_dirty = 1u;
    if (held_chord[i]) {
        nf_note_off_id(&app, held_serial[i][1]);
        nf_note_off_id(&app, held_serial[i][2]);
        held_chord[i] = 0u;
    }
    held_serial[i][0] = held_serial[i][1] = held_serial[i][2] = 0u;
}

static void arp_poll(uint32_t now)
{
    uint8_t notes[27], count = 0u;
    uint32_t period, gate;
    unsigned i, index;
    for (i = 0u; i < 27u; i++)
        if ((held_notes & (1u << i)) && held_is_arp[i])
            notes[count++] = held_pitch[i];
    if (arp_active && (count == 0u || !app.arp_enabled || now >= arp_release_ms)) {
        fm1_irq_off(); nf_note_off_id(&app, arp_serial); fm1_irq_on();
        arp_active = 0u;
    }
    if (!count || !app.arp_enabled) {
        arp_next_ms = 0u; arp_index = 0u; app.arp_position = 0xffu;
        return;
    }
    if (arp_next_ms && now < arp_next_ms) return;
    period = 60000u / ((uint32_t)app.tempo *
        (1u << (app.arp_control[1] > 4u ? 4u : app.arp_control[1])));
    if (period < 25u) period = 25u;
    gate = period * (app.arp_control[2] > 95u ? 95u : app.arp_control[2]) / 100u;
    if (gate < 10u) gate = 10u;
    if (arp_active) {
        fm1_irq_off(); nf_note_off_id(&app, arp_serial); fm1_irq_on();
    }
    index = arp_index % count;
    if (app.arp_control[0] == 1u) index = count - 1u - index;
    if (app.arp_control[0] == 2u && count > 1u) {
        unsigned position = arp_index % (2u * count - 2u);
        index = position < count ? position : 2u * count - 2u - position;
    }
    if (app.arp_control[0] == 3u) {
        app.random ^= app.random << 13;
        app.random ^= app.random >> 17;
        app.random ^= app.random << 5;
        index = (app.random >> 8) % count;
    }
    arp_note = (uint8_t)(notes[index] + 12u *
        ((arp_index / count) %
         (1u + (app.arp_control[3] > 2u ? 2u : app.arp_control[3]))));
    if (arp_note > 127u) arp_note = 127u;
    arp_track = 3u;
    fm1_irq_off(); arp_serial = nf_note_on_id(&app, arp_track, arp_note, 105u, NF_KEY); fm1_irq_on();
    arp_active = 1u;
    app.arp_position = (uint8_t)(arp_index % 8u);
    arp_index++;
    arp_release_ms = now + gate;
    arp_next_ms = now + period;
}

static void input_poll(void)
{
    uint32_t pressed, released, note_pressed, keys, keep_notes = 0u, i;
    int32_t turn;
    pressed = fm1_input_edges(&released);
    note_pressed = fm1_input_note_edges();
    keys = fm1_in.notes;
    (void)released;
    if (pressed & (1u << 12)) {
        fm1_irq_off(); nf_toggle_play(&app); fm1_irq_on();
        set_notice(app.playing ? "PLAYING" : "STOPPED");
        full_redraw = 1u;
    }
    if (pressed & (1u << 13)) {
        fm1_irq_off(); app.recording ^= 1u; fm1_irq_on();
        if (app.recording && (app.page == NF_GLO_PAGE ||
                              app.page == NF_SEQ_PAGE || app.page == NF_SAVE_PAGE)) {
            app.page = NF_HOME;
            set_notice("RECORD ON / HOME");
        } else set_notice(app.recording ? "RECORD ON" : "RECORD OFF");
        full_redraw = 1u;
    }
    if (pressed & (1u << 2)) toggle_page(NF_FX_PAGE);
    if (pressed & (1u << 4)) toggle_page(NF_ENV_PAGE);
    if (pressed & (1u << 5)) toggle_page(NF_LFO_PAGE);
    if (pressed & (1u << 6)) toggle_page(NF_EDIT_PAGE);
    if (pressed & (1u << 7)) toggle_page(NF_GLO_PAGE);
    if (pressed & (1u << 8)) toggle_page(NF_HOME);
    if (pressed & (1u << 9)) toggle_page(NF_SAVE_PAGE);
    if (pressed & (1u << 10)) toggle_page(NF_ARP_PAGE);
    if (pressed & (1u << 11)) toggle_page(NF_SEQ_PAGE);
    if (pressed & (1u << 3)) {
        fm1_irq_off();
        if (app.page == NF_FX_PAGE) {
            app.fx[app.selected_fx].enabled ^= 1u;
            set_notice(app.fx[app.selected_fx].enabled ? "FX ON" : "FX BYPASS");
        } else if (app.page == NF_SAVE_PAGE) {
            int rc;
            if (app.playing || app.recording) set_notice("STOP TO SAVE / LOAD");
            else if (held_notes) set_notice("RELEASE KEYS FIRST");
            else if (app.save_op) {
                rc = nf_load_project(&app, app.selected_project);
                set_notice(rc == 0 ? "LOADED / RAM ONLY" : "EMPTY OR INVALID SLOT");
                overwrite_slot = 0xffu;
            } else if (app.project[app.selected_project].valid &&
                       (overwrite_slot != app.selected_project ||
                        (int32_t)(overwrite_until_ms - app.now_ms) <= 0)) {
                overwrite_slot = app.selected_project;
                overwrite_until_ms = app.now_ms + 4000u;
                set_notice("SEL AGAIN: OVERWRITE");
            } else {
                rc = nf_save_project(&app, app.selected_project);
                set_notice(rc == 0 ? "SAVED / RAM ONLY" : "SAVE FAILED");
                overwrite_slot = 0xffu;
            }
        } else if (app.page == NF_SEQ_PAGE) {
            nf_step *step = &app.track[app.selected_track].steps[app.seq_focus];
            if (step->velocity) step->velocity = 0u;
            else {
                step->note = (uint8_t)(KEY_BASE + white_key[app.seq_focus % 16u] + 12 * octave);
                step->velocity = 100u;
                step->length = 1u;
                step->chance = 100u;
            }
            app.edited[app.selected_track] = 1u;
            set_notice(step->velocity ? "STEP ON" : "STEP OFF");
        } else if (app.page == NF_ARP_PAGE) {
            app.arp_enabled ^= 1u;
            set_notice(app.arp_enabled ? "ARP ON" : "ARP OFF");
        } else if (app.page == NF_SCALE_PAGE) {
            app.scale = (uint8_t)((app.scale + 1u) % 3u);
            set_notice(app.scale == 0u ? "CHROMATIC" :
                       app.scale == 1u ? "SCALE LOCK" : "ONE KEY CHORD");
        } else {
            app.page = NF_SCALE_PAGE;
            set_notice("SCALE: SEL TO CYCLE");
        }
        fm1_irq_on();
        full_redraw = 1u;
    }
    if (app.page == NF_SEQ_PAGE && (pressed & 3u)) {
        int32_t bank = (int32_t)(app.seq_focus / 16u) +
                       ((pressed & 2u) ? 1 : -1);
        bank = bounded(bank, 0, 3);
        app.seq_focus = (uint8_t)(bank * 16 + app.seq_focus % 16u);
        app.pattern_length = (uint16_t)((bank + 1) * 16);
        set_notice_value("PATTERN STEPS", app.pattern_length);
        full_redraw = 1u;
    } else {
        if (pressed & 1u && octave > -2) { octave--; set_notice_value("OCTAVE", octave); }
        if (pressed & 2u && octave < 2) { octave++; set_notice_value("OCTAVE", octave); }
    }
    turn = fm1_enc_take(0u);         /* SELECT: tempo, FX, scratch slot, step */
    if (turn) {
        fm1_irq_off();
        if (app.page == NF_SAVE_PAGE)
            app.selected_project = (uint8_t)bounded((int32_t)app.selected_project + turn, 0, NF_PROJECT_SLOTS - 1);
        else if (app.page == NF_FX_PAGE)
            app.selected_fx = (uint8_t)bounded((int32_t)app.selected_fx + turn, 0, NF_FX_COUNT - 1);
        else if (app.page == NF_SEQ_PAGE)
            app.seq_focus = (uint8_t)bounded((int32_t)app.seq_focus + turn, 0, 63);
        else app.tempo = (uint16_t)bounded((int32_t)app.tempo + turn, 40, 240);
        fm1_irq_on();
        set_notice_value(app.page == NF_SAVE_PAGE ? "SLOT" :
                         app.page == NF_FX_PAGE ? "FX" :
                         app.page == NF_SEQ_PAGE ? "STEP" : "BPM",
                         app.page == NF_SAVE_PAGE ? app.selected_project + 1 :
                         app.page == NF_FX_PAGE ? app.selected_fx + 1 :
                         app.page == NF_SEQ_PAGE ? app.seq_focus + 1 : app.tempo);
        if (app.page == NF_SAVE_PAGE) overwrite_slot = 0xffu;
        full_redraw = 1u;
    }
    turn = fm1_enc_take(1u);         /* ALGORITHM: track */
    if (turn) {
        fm1_irq_off();
        app.selected_track = (uint8_t)bounded((int32_t)app.selected_track + turn, 0, NF_TRACKS - 1);
        fm1_irq_on(); full_redraw = 1u;
        set_notice(app.selected_track == 0u ? "TRACK DRUM" :
                   app.selected_track == 1u ? "TRACK BASS" :
                   app.selected_track == 2u ? "TRACK CHORD" : "TRACK LEAD");
    }
    turn = fm1_enc_take(6u);         /* PRESETS: ready-to-play song */
    if (turn) {
        if (app.playing || app.recording) set_notice("STOP TO CHANGE PRESET");
        else {
            int32_t p = bounded((int32_t)app.preset + turn, 0, 2);
            fm1_irq_off(); (void)nf_load_preset(&app, (unsigned)p); fm1_irq_on();
            set_notice(p == 0 ? "PRESET MIDNIGHT" :
                       p == 1 ? "PRESET NEON RUN" : "PRESET AFTER RAIN");
        }
        full_redraw = 1u;
    }
    for (i = 0; i < 4u; i++) {
        turn = fm1_enc_take(i + 2u);
        if (!turn) continue;
        if (app.page == NF_FX_PAGE) {
            uint8_t effect = app.selected_fx;
            int32_t value = (int32_t)app.fx[effect].value[i] + turn;
            if (value < 0) value = 0;
            if (value > 127) value = 127;
            fm1_irq_off(); app.fx[effect].value[i] = (uint8_t)value; fm1_irq_on();
        } else if (app.page == NF_ENV_PAGE) {
            uint8_t t = app.selected_track;
            fm1_irq_off(); app.envelope_control[t][i] = (uint8_t)bounded(
                (int32_t)app.envelope_control[t][i] + turn, 0, 100); fm1_irq_on();
        } else if (app.page == NF_EDIT_PAGE) {
            uint8_t t = app.selected_track;
            fm1_irq_off(); app.voice_control[t][i] = (uint8_t)bounded(
                (int32_t)app.voice_control[t][i] + turn, 0,
                i == 0u ? 4 : i == 2u ? 4 : 100); fm1_irq_on();
        } else if (app.page == NF_LFO_PAGE || app.page == NF_ARP_PAGE) {
            uint8_t *control = app.page == NF_LFO_PAGE ? app.lfo_control : app.arp_control;
            int32_t hi = app.page == NF_LFO_PAGE ?
                i == 0u ? 7 : i == 2u ? 2 : i == 3u ? 3 : 100 :
                i == 0u ? 3 : i == 1u ? 4 : i == 3u ? 2 : 100;
            fm1_irq_off(); control[i] = (uint8_t)bounded((int32_t)control[i] + turn, 0, hi); fm1_irq_on();
        } else if (app.page == NF_SAVE_PAGE && i == 0u) {
            fm1_irq_off(); app.save_op = (uint8_t)bounded((int32_t)app.save_op + turn, 0, 1); fm1_irq_on();
            overwrite_slot = 0xffu;
        } else if (app.page == NF_GLO_PAGE && i < NF_TRACKS) {
            fm1_irq_off(); app.track[i].level = (uint8_t)bounded((int32_t)app.track[i].level + turn, 0, 127); fm1_irq_on();
        } else if (app.page == NF_SEQ_PAGE) {
            nf_step *step = &app.track[app.selected_track].steps[app.seq_focus];
            fm1_irq_off();
            if (i == 0u) {
                int32_t pitch = step->note ? step->note :
                    KEY_BASE + (int32_t)white_key[app.seq_focus % 16u] + 12 * octave;
                step->note = (uint8_t)bounded(pitch + turn, 0, 127);
                app.edited[app.selected_track] = 1u;
            } else if (i == 1u) {
                step->velocity = (uint8_t)bounded((int32_t)step->velocity + turn, 0, 127);
                if (step->velocity && !step->length) {
                    step->note = (uint8_t)(KEY_BASE + white_key[app.seq_focus % 16u] + 12 * octave);
                    step->length = 1u;
                    step->chance = 100u;
                }
                app.edited[app.selected_track] = 1u;
            } else if (i == 2u) {
                step->length = (uint8_t)bounded((int32_t)step->length + turn, 1, 16);
                app.edited[app.selected_track] = 1u;
            } else {
                step->chance = (uint8_t)bounded((int32_t)step->chance + turn, 0, 100);
                app.edited[app.selected_track] = 1u;
            }
            fm1_irq_on();
        } else if (app.page == NF_HOME) {
            uint8_t value = (uint8_t)bounded((int32_t)app.macros[i] + turn, 0, 100);
            fm1_irq_off(); nf_set_macro(&app, i, value); fm1_irq_on();
        }
        notice_knob(i);
        full_redraw = 1u;
    }
    for (i = 0; i < 27u; i++) {
        uint32_t bit = 1u << i;
        uint8_t note = (uint8_t)(KEY_BASE + (int32_t)i + 12 * octave);
        int pad = white_index(i);
        if ((note_pressed & bit) &&
            (app.page == NF_GLO_PAGE || app.page == NF_SEQ_PAGE)) {
            command_notes |= bit;
            if (app.page == NF_GLO_PAGE && pad >= 0 && pad < 8) {
                fm1_irq_off();
                if (pad < 4) app.track[pad].muted ^= 1u;
                else if (pad < 6) {
                    app.queued_scene = (uint8_t)(pad - 4);
                    app.auto_arrange = 0u;
                } else if (pad == 6) app.fill_queued = 1u;
                else app.auto_arrange ^= 1u;
                fm1_irq_on();
                set_notice(pad < 4 ? app.track[pad].muted ? "TRACK MUTED" : "TRACK ON" :
                           pad == 4 ? "SCENE A QUEUED" : pad == 5 ? "SCENE B QUEUED" :
                           pad == 6 ? "FILL QUEUED" :
                           app.auto_arrange ? "AUTO ARRANGE ON" : "AUTO ARRANGE OFF");
            } else if (app.page == NF_SEQ_PAGE && pad >= 0) {
                uint8_t step_index = (uint8_t)((app.seq_focus / 16u) * 16u + (unsigned)pad);
                nf_step *step = &app.track[app.selected_track].steps[step_index];
                fm1_irq_off();
                if (step->velocity) step->velocity = 0u;
                else { step->note = note; step->velocity = 100u;
                       step->length = 1u; step->chance = 100u; }
                app.edited[app.selected_track] = 1u;
                app.seq_focus = step_index;
                fm1_irq_on();
                set_notice_value(step->velocity ? "STEP ON" : "STEP OFF", step_index + 1);
            } else set_notice(app.page == NF_GLO_PAGE ?
                              "USE WHITE KEYS 1-8" : "USE WHITE KEYS 1-16");
            full_redraw = 1u;
            continue;
        }
        /* The scan ISR latches short presses while a full LCD transfer keeps
         * this loop busy. Play every latched onset, including a tap that has
         * already been released; its note-off follows on the next poll. */
        if (note_pressed & bit) {
            if (held_notes & bit) {
                fm1_irq_off();
                release_key(i);
                fm1_irq_on();
            }
            held_pitch[i] = performance_note(i, note);
            held_track[i] = app.selected_track;
            held_min_until[i] = app.now_ms + 35u;
            held_is_arp[i] = app.arp_enabled && app.selected_track == 3u;
            held_chord[i] = app.scale == 2u && app.selected_track != 0u &&
                held_pitch[i] < 120u && !held_is_arp[i];
            if (!held_is_arp[i]) {
                fm1_irq_off();
                held_serial[i][0] = nf_note_on_id(&app, held_track[i], held_pitch[i], 110u, NF_KEY);
                nf_usb_emit_note((uint8_t)(0x90u | (held_track[i] & 3u)), held_pitch[i], 110u);
                if (held_chord[i]) {
                    held_serial[i][1] = nf_note_on_id(&app, held_track[i],
                        (uint8_t)(held_pitch[i] + 3u), 90u, NF_MIDI);
                    held_serial[i][2] = nf_note_on_id(&app, held_track[i],
                        (uint8_t)(held_pitch[i] + 7u), 82u, NF_MIDI);
                }
                fm1_irq_on();
            }
            header_dirty = 1u;
            fm1_led_key(i + 14u, 1);
        } else if (!(keys & bit) && (held_notes & bit)) {
            if (!(command_notes & bit) && (int32_t)(held_min_until[i] - app.now_ms) > 0) {
                keep_notes |= bit;
                continue;
            }
            if (command_notes & bit) command_notes &= ~bit;
            else {
                fm1_irq_off();
                release_key(i);
                fm1_irq_on();
            }
            fm1_led_key(i + 14u, 0);
        }
    }
    held_notes = keys | (note_pressed & ~keys) | keep_notes;
    arp_poll(app.now_ms);
    fm1_led_key(12u, app.playing);
    fm1_led_key(13u, app.recording);
    fm1_led_key(3u, app.page == NF_FX_PAGE ? app.fx[app.selected_fx].enabled :
                app.page == NF_ARP_PAGE ? app.arp_enabled :
                app.page == NF_SCALE_PAGE);
    for (i = 2u; i <= 11u; i++)
        if (i != 3u) fm1_led_key(i, app.page == (i == 2u ? NF_FX_PAGE :
            i == 4u ? NF_ENV_PAGE : i == 5u ? NF_LFO_PAGE :
            i == 6u ? NF_EDIT_PAGE : i == 7u ? NF_GLO_PAGE :
            i == 8u ? NF_HOME : i == 9u ? NF_SAVE_PAGE :
            i == 10u ? NF_ARP_PAGE : NF_SEQ_PAGE));
    if (app.page == NF_SEQ_PAGE || app.page == NF_GLO_PAGE)
        for (i = 0u; i < 27u; i++) {
            int pad = white_index(i);
            int lit = 0;
            if (pad >= 0 && app.page == NF_SEQ_PAGE)
                lit = app.track[app.selected_track].steps[(app.seq_focus / 16u) * 16u + (unsigned)pad].velocity != 0u;
            else if (pad >= 0 && app.page == NF_GLO_PAGE) {
                if (pad < 4) lit = !app.track[pad].muted;
                else if (pad < 6) lit = app.scene == (uint8_t)(pad - 4);
                else if (pad == 6) lit = app.fill_queued || app.fill_active;
                else if (pad == 7) lit = app.auto_arrange;
            }
            fm1_led_key(i + 14u, lit || (command_notes & (1u << i)) != 0u);
        }
    else
        for (i = 0u; i < 27u; i++)
            fm1_led_key(i + 14u, (held_notes & (1u << i)) != 0u &&
                                      (command_notes & (1u << i)) == 0u);
}

static void run(void)
{
    uint32_t handled_ms = 0u, draw_ms = 0u, recovery_ms = 0u, feed_ms = 0u,
             master_ms = 0u, terrain_ms = 0u, header_ms = 0u,
             footer_ms = 0u;
    nf_init(&app);
    (void)nf_load_preset(&app, 0u);
    nf_link_init(&link, &app);
#ifdef NF_RECOVERY_MIDI
    (void)nf_usb_update_cleanup(); /* clear any staged loader after a reboot */
#endif
    /* Four sound types are available before any stored project is loaded. */
    fm1_input_init();
    master_boot();
    lcd_init();
    fm1_audio_init(dma_audio, AUDIO_WORDS, isr_alnk0, 3u);
    nf_usb_start();
    fm1_timer5_start(isr_timer5, 4u);
    fm1_irq_enable_all();
    snapshot(); lcd_present(0u, NF_HEIGHT);
    for (;;) {
        uint32_t now = clock_ms;
        if (now != handled_ms) {
            while (handled_ms != now) {
                fm1_irq_off(); nf_tick_ms(&app, 1u); fm1_irq_on();
                handled_ms++;
            }
            input_poll();
            if (now - master_ms >= 10u) { master_poll(); master_ms = now; }
            app.audio_underruns = audio_late;
            if (now - feed_ms >= 100u) { fm1_wdt_feed(); feed_ms = now; }
        }
        nf_usb_retry(now);
        if (now - terrain_ms >= 70u) {
            terrain_ms = now;
            nf_update_terrain(&app);
        }
#ifdef NF_RECOVERY_MIDI
        nf_usb_service_identity();
        if (nf_usb_uboot_pending()) {
            boot_state.pending = 0u;
            fm1_audio_stop();
            lcd_sync();
            nf_usb_enter_uboot();
        }
        {
            uint8_t status, note, velocity;
            unsigned budget = 32u;
            while (budget-- && nf_usb_take_midi(&status, &note, &velocity)) {
                uint8_t kind = status & 0xf0u;
                uint8_t track = status & 3u;
                if (note > 127u) continue;
                fm1_irq_off();
                if (kind == 0x90u && velocity)
                    nf_note_on(&app, track, note, velocity, NF_MIDI);
                else if (kind == 0x80u || (kind == 0x90u && !velocity))
                    nf_note_off(&app, track, note, NF_MIDI);
                fm1_irq_on();
                header_dirty = 1u;
            }
        }
        if (nf_usb_update_pending()) {
            /* A failed staging attempt must not count as a boot-loop crash.
             * The RAM loader is only committed after the host confirms it. */
            boot_state.pending = 0u;
            fm1_audio_stop();
            lcd_sync();
            (void)nf_usb_run_update();
            fm1_reboot();
        }
#endif
        {
            uint8_t byte;
            unsigned budget = 96u;
            while (budget-- && nf_usb_read_byte(&byte)) {
                int reply_size;
                fm1_irq_off();
                reply_size = nf_link_feed(&link, byte, link_reply, sizeof link_reply);
                fm1_irq_on();
                if (reply_size > 0) {
                    (void)nf_usb_write(link_reply, (size_t)reply_size);
                    if ((link_reply[3] & 0x7fu) > NF_CMD_NOTE_OFF) full_redraw = 1u;
                }
            }
        }
        if (now - draw_ms >= 50u || full_redraw) {
            draw_ms = now;
            snapshot();
            if (app.page == NF_HOME && app.home_view && !full_redraw) {
                /* At 12 MHz, a 240x240 RGB565 transfer alone takes ~77 ms.
                 * Stream the animated middle (137 rows, ~44 ms) each frame;
                 * refresh the note header and transport footer separately. */
                lcd_prepare();
                lcd_flush_region(62u, 137u);
                if (header_dirty || now - header_ms >= 350u) {
                    lcd_flush_region(0u, 62u);
                    header_ms = now;
                    header_dirty = 0u;
                } else if (now - footer_ms >= 150u) {
                    lcd_flush_region(199u, 41u);
                    footer_ms = now;
                }
            } else {
                lcd_present(0u, NF_HEIGHT);
                header_ms = footer_ms = now;
                header_dirty = 0u;
            }
            full_redraw = 0u;
        }
        if (!recovery_ms && (fm1_in.buttons & 3u) == 3u) recovery_ms = now;
        if ((fm1_in.buttons & 3u) != 3u) recovery_ms = 0u;
        if (recovery_ms && now - recovery_ms >= 5000u) {
            fm1_audio_stop(); lcd_sync(); fm1_enter_uboot();
        }
        if (now > 30000u) { boot_state.pending = 0u; boot_state.failed = 0u; }
    }
}

void fm1_cstart(void)
{
    uint32_t *p, *q;
    fm1_time_init();
    fm1_wdt_arm(0x0du);
    if (boot_state.magic != 0x4e465542u) {
        boot_state.magic = 0x4e465542u;
        boot_state.pending = boot_state.failed = 0u;
    }
    if (boot_state.pending) boot_state.failed++;
    boot_state.pending = 1u;
    if (boot_state.failed >= 2u) { boot_state.pending = 0u; fm1_enter_uboot(); }
    fm1_irq_init();
    for (p = _bss_start; p < _bss_end; p++) *p = 0u;
    for (p = _pool_start; p < _pool_end; p++) *p = 0u;
    for (p = _data_start, q = _data_load; p < _data_end; p++, q++) *p = *q;
    for (p = _rt_start, q = _rt_load; p < _rt_end; p++, q++) *p = *q;
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    run();
}
