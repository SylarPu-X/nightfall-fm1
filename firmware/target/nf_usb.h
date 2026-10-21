#ifndef NF_USB_H
#define NF_USB_H
#include <stddef.h>
#include <stdint.h>

/* Development-only vendor bulk interface. EP1 OUT/IN carry application
 * packets byte-for-byte. The USB test identity must be replaced before any
 * distribution; this API is not a firmware updater. */
void nf_usb_start(void);
void nf_usb_poll(void);
void nf_usb_retry(uint32_t now_ms);
int nf_usb_read_byte(uint8_t *byte);
int nf_usb_write(const uint8_t *bytes, size_t length);
int nf_usb_connected(void);
/* Recovery-MIDI first-board-test variant only; vendor-bulk stubs are inert. */
void nf_usb_set_ms(uint32_t now_ms);
int nf_usb_update_pending(void);
void nf_usb_service_identity(void);
int nf_usb_uboot_pending(void);
void nf_usb_enter_uboot(void);
int nf_usb_run_update(void);
int nf_usb_update_cleanup(void);
int nf_usb_take_midi(uint8_t *status, uint8_t *note, uint8_t *velocity);
void nf_usb_emit_note(uint8_t status, uint8_t note, uint8_t velocity);

#endif
