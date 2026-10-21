/* SPDX-License-Identifier: GPL-3.0-only
 * NIGHTFALL first-board-test USB bridge. Application, synthesis and UI are
 * NIGHTFALL's; this build reuses Felucca's GPL FM-1 USB-MIDI and M-UPGRADE
 * infrastructure as external board/update support. usb_nightfall.c is made
 * from the unmodified upstream source with only the USB strings rebranded. */
#include <stddef.h>
#include <stdint.h>
#include "fm1_audio.h"
#include "fm1_flash.h"
#include "fm1_irq.h"
#include "fm1_sys.h"
#include "fm1_time.h"
#include "nf_usb.h"
#include "nightfall.h"

#define FELUCCA_CDC 0
#define FELUCCA_UAC 0
#define FELUCCA_OTA 1
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_ID "FM-1_999"
#define FELUCCA_USB_PID 0x0001
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "usb_nightfall.c"

static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!FL_IN(off, 0x1000u, FL_OTA_LO, FL_OTA_HI) || (off & 0xfffu)) return -8;
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!FL_IN(off, n, FL_OTA_LO, FL_OTA_HI)) return -8;
    return fl_write(off, (const uint8_t *)p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n)
{
    int rc;
    if (!FL_IN(off, n, 0u, 0x100000u)) return -8;
    fm1_irq_off();
    rc = FL_FAR(fl_read_ram)(off, (uint8_t *)p, n);
    fm1_irq_on();
    return rc;
}
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm)
{
    usb_detach();
    fm1_delay_ms(30u);
    fm1_enter_update(parm);
}
#include "ota.c"

void nf_usb_start(void) { usb_start(); }
void nf_usb_poll(void) { usb_poll(); }
void nf_usb_retry(uint32_t now_ms) { usb_retry(now_ms); }
int nf_usb_connected(void) { return usb.config != 0u; }
void nf_usb_set_ms(uint32_t now_ms) { fm1_ms = now_ms; }
int nf_usb_update_pending(void) { return usb.ota_req != 0u; }
/* 7D is the MIDI non-commercial manufacturer ID. NIGHTFALL packets are
 * isolated from Felucca's 00 59 updater frames and retain the same checked
 * packet format as the native bulk transport. One request is processed at a
 * time; the host waits for its sequence-numbered reply. */
static uint8_t nf_rx[NF_PACKET_FRAME_MAX];
static uint16_t nf_rx_size, nf_rx_pos;
void nf_usb_service_identity(void)
{
    const uint8_t *frame;
    uint32_t n, decoded;
    if (!nf_rx_size && ota_frame_get(&frame, &n) && n >= 5u &&
        frame[0] == 0x7du && frame[1] == 'N' && frame[2] == 'F' &&
        frame[3] == 1u) {
        decoded = ota_unpack7(frame + 4u, n - 4u, nf_rx, sizeof nf_rx);
        ota_frame_done();
        if (decoded >= 9u && decoded <= sizeof nf_rx) {
            nf_rx_size = (uint16_t)decoded;
            nf_rx_pos = 0u;
        }
    }
    if (!nf_rx_size) ota_service();
}
int nf_usb_uboot_pending(void) { return usb.uboot_req != 0u; }
void nf_usb_enter_uboot(void)
{
    usb_detach();
    fm1_delay_ms(30u);
    fm1_enter_uboot();
}
int nf_usb_run_update(void)
{
    usb.ota_req = 0u;
    return ota_session();
}
int nf_usb_update_cleanup(void) { return ota_boot_cleanup(); }
int nf_usb_take_midi(uint8_t *status, uint8_t *note, uint8_t *velocity)
{
    uint32_t packet;
    if (!status || !note || !velocity || mi_r == mi_w) return 0;
    RING_PUBLISH();
    packet = midi_in_q[mi_r % MQ];
    mi_r++;
    *status = (uint8_t)(packet >> 8);
    *note = (uint8_t)(packet >> 16);
    *velocity = (uint8_t)(packet >> 24);
    if (midi_in_overflow && mi_r == mi_w) midi_in_overflow = 0u;
    return 1;
}
void nf_usb_emit_note(uint8_t status, uint8_t note, uint8_t velocity)
{
    if (!usb.config) return;
    midi_out_event(((status & 0xf0u) == 0x80u ? 0x08u : 0x09u) |
                   ((uint32_t)status << 8) |
                   ((uint32_t)note << 16) | ((uint32_t)velocity << 24));
}
int nf_usb_read_byte(uint8_t *byte)
{
    if (!byte || nf_rx_pos >= nf_rx_size) return 0;
    *byte = nf_rx[nf_rx_pos++];
    if (nf_rx_pos == nf_rx_size) nf_rx_pos = nf_rx_size = 0u;
    return 1;
}
int nf_usb_write(const uint8_t *bytes, size_t length)
{
    uint8_t wire[2u + 4u + (NF_PACKET_FRAME_MAX * 8u + 6u) / 7u];
    uint32_t packed;
    if (!bytes || !length || length > NF_PACKET_FRAME_MAX || !usb.config)
        return -1;
    wire[0] = 0xf0u;
    wire[1] = 0x7du;
    wire[2] = 'N';
    wire[3] = 'F';
    wire[4] = 1u;
    packed = ota_pack7(bytes, (uint32_t)length, wire + 5u);
    wire[5u + packed] = 0xf7u;
    return ota_wire_send(wire, packed + 6u);
}
