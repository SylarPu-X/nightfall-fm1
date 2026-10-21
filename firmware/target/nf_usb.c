/* SPDX-License-Identifier: GPL-3.0-only
 * NIGHTFALL's USB device protocol, using the GPL FM-1 register-level HAL. */
#include <stdint.h>
#include <stddef.h>
#include "fm1_usb.h"
#include "fm1_time.h"
#include "nf_usb.h"

#ifndef NF_USB_TEST_ID
#error "Provide a unique USB VID/PID for release; NF_USB_TEST_ID is bench-only"
#endif

enum { FADDR = 0, POWER = 1, INTRTX = 2, INTRRX = 4, INTRUSB = 6,
       INTRTXE = 7, INTRRXE = 9, INTRUSBE = 11, INDEX = 14,
       TXMAXP = 16, CSR0 = 17, TXCSR1 = 17, TXCSR2 = 18,
       RXMAXP = 19, RXCSR1 = 20, RXCSR2 = 21,
       RXCOUNT1 = 22, RXCOUNT2 = 23 };

/* The 0x1209:0x0001 identity belongs to the pid.codes TEST pool and may only
 * be used for a private bench build. Never package or publish this binary. */
static const uint8_t device_desc[] = {
    18, 1, 0x00, 0x02, 0, 0, 0, 64,
    0x09, 0x12, 0x01, 0x00, 0x00, 0x01, 1, 2, 0, 1
};
static const uint8_t config_desc[] = {
    9, 2, 32, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0xff, 0, 0, 0,
    7, 5, 0x01, 2, 64, 0, 0,
    7, 5, 0x81, 2, 64, 0, 0
};
static const uint8_t lang_desc[] = {4, 3, 0x09, 0x04};
static const uint8_t maker_desc[] = {
    20, 3, 'N',0,'I',0,'G',0,'H',0,'T',0,'F',0,'A',0,'L',0,'L',0
};
static const uint8_t product_desc[] = {
    30, 3, 'N',0,'I',0,'G',0,'H',0,'T',0,'F',0,'A',0,'L',0,'L',0,
    ' ',0,'F',0,'M',0,'-',0,'1',0
};
static const uint8_t zero_status[2] = {0, 0};
static uint8_t ep0[68] __attribute__((aligned(4)));
static uint8_t ep1_in[64] __attribute__((aligned(4)));
static uint8_t ep1_out[68] __attribute__((aligned(4)));
#define RING_CAP 1024u
static uint8_t rx_ring[RING_CAP], tx_ring[RING_CAP];
static volatile uint16_t rx_r, rx_w, tx_r, tx_w;
static struct {
    volatile uint8_t active, configured;
    uint8_t pending_address, address_pending, sending, zlp, rx_pending;
    const uint8_t *source;
    uint16_t left;
    uint32_t timeout_count, retry_ms;
} usb;

static uint32_t sie_read(uint32_t reg)
{
    uint32_t limit = 20000u;
    if (!fm1_usb_sie_on()) return 0u;
    fm1_usb_sie_rd_start(reg);
    while (!fm1_usb_sie_done())
        if (!--limit) { usb.timeout_count++; return 0u; }
    usb.timeout_count = 0u;
    return fm1_usb_sie_data();
}
static void sie_write(uint32_t reg, uint32_t value)
{
    uint32_t limit = 20000u;
    if (!fm1_usb_sie_on()) return;
    fm1_usb_sie_wr_start(reg, value);
    while (!fm1_usb_sie_done())
        if (!--limit) { usb.timeout_count++; return; }
    usb.timeout_count = 0u;
}

static void ep1_configure(void)
{
    fm1_usb_ep_txbuf(1u, ep1_in);
    fm1_usb_ep_rxbuf(1u, ep1_out);
    sie_write(INDEX, 1u);
    sie_write(TXMAXP, 0xffu);
    sie_write(TXCSR1, 0x48u);
    sie_write(TXCSR2, 0u);
    sie_write(RXMAXP, 0xffu);
    sie_write(RXCSR1, 0x90u);
    sie_write(RXCSR2, 0u);
    sie_write(INTRRXE, 0x02u);
    fm1_usb_ep_enable(1u << 1);
}

static void ep0_next(void)
{
    uint32_t i, n = usb.left > 64u ? 64u : usb.left;
    uint8_t last;
    for (i = 0u; i < n; i++) ep0[i] = usb.source[i];
    fm1_usb_ep0_send(ep0, n);
    usb.source += n;
    usb.left = (uint16_t)(usb.left - n);
    last = usb.left == 0u && !(n == 64u && usb.zlp);
    if (usb.left == 0u && n == 64u) usb.zlp = 0u;
    sie_write(INDEX, 0u);
    sie_write(CSR0, last ? 0x0au : 0x02u);
    if (last) usb.sending = 0u;
}

static void ep0_send(const uint8_t *source, uint16_t available, uint16_t asked)
{
    if (available > asked) available = asked;
    usb.source = source;
    usb.left = available;
    usb.zlp = (uint8_t)(available < asked && available % 64u == 0u);
    usb.sending = 1u;
    sie_write(INDEX, 0u);
    sie_write(CSR0, 0x40u);
    ep0_next();
}

static int descriptor(uint16_t value, const uint8_t **source, uint16_t *length)
{
    switch (value) {
    case 0x0100: *source = device_desc; *length = sizeof device_desc; return 1;
    case 0x0200: *source = config_desc; *length = sizeof config_desc; return 1;
    case 0x0300: *source = lang_desc; *length = sizeof lang_desc; return 1;
    case 0x0301: *source = maker_desc; *length = sizeof maker_desc; return 1;
    case 0x0302: *source = product_desc; *length = sizeof product_desc; return 1;
    default: return 0;
    }
}

static void ep0_service(void)
{
    uint32_t csr, i;
    uint8_t setup[8];
    uint16_t value, asked;
    const uint8_t *source;
    uint16_t length;
    if (usb.address_pending) {
        sie_write(FADDR, usb.pending_address);
        usb.address_pending = 0u;
    }
    sie_write(INDEX, 0u);
    csr = sie_read(CSR0);
    if (csr & 4u) { sie_write(CSR0, 0u); usb.sending = 0u; return; }
    if (csr & 0x10u) { sie_write(CSR0, 0x80u); usb.sending = 0u; }
    if (usb.sending) {
        if (!(csr & 2u)) ep0_next();
        return;
    }
    if (!(csr & 1u)) { usb.rx_pending = 0u; return; }
    fm1_usb_rx_sync();
    for (i = 0u; i < 8u; i++) setup[i] = ep0[i];
    value = (uint16_t)(setup[2] | ((uint16_t)setup[3] << 8));
    asked = (uint16_t)(setup[6] | ((uint16_t)setup[7] << 8));
    switch ((uint16_t)((uint16_t)setup[0] << 8 | setup[1])) {
    case 0x0005: /* SET_ADDRESS: takes effect after status stage */
        usb.pending_address = setup[2] & 0x7fu;
        usb.address_pending = 1u;
        break;
    case 0x8006: /* GET_DESCRIPTOR */
        if (!descriptor(value, &source, &length)) goto stall;
        if (asked) { ep0_send(source, length, asked); return; }
        break;
    case 0x0009: /* SET_CONFIGURATION */
        if (value > 1u) goto stall;
        usb.configured = (uint8_t)value;
        if (value) ep1_configure();
        else sie_write(INTRRXE, 0u);
        break;
    case 0x8008: /* GET_CONFIGURATION */
        ep0_send((const uint8_t *)&usb.configured, 1u, asked); return;
    case 0x8000: /* GET_STATUS */
    case 0x8100:
    case 0x8200:
        ep0_send(zero_status, 2u, asked); return;
    case 0x010b: /* SET_INTERFACE alt 0 only */
        if (value) goto stall;
        break;
    case 0x810a: /* GET_INTERFACE */
        ep0_send(zero_status, 1u, asked); return;
    case 0x0201: { /* CLEAR_FEATURE(ENDPOINT_HALT) */
        uint8_t ep = setup[4] & 0x0fu;
        if (value || ep > 1u) goto stall;
        if (ep) {
            sie_write(INDEX, 1u);
            if (setup[4] & 0x80u) sie_write(TXCSR1, 0x40u);
            else sie_write(RXCSR1, 0x80u);
        }
        break;
    }
    default: goto stall;
    }
    sie_write(INDEX, 0u);
    sie_write(CSR0, 0x48u);
    return;
stall:
    sie_write(INDEX, 0u);
    sie_write(CSR0, 0x60u);
}

static void ep1_receive(void)
{
    uint32_t csr, count, i;
    sie_write(INDEX, 1u);
    csr = sie_read(RXCSR1) | (sie_read(RXCSR2) << 8);
    if (!(csr & 1u)) return;
    count = sie_read(RXCOUNT1) | (sie_read(RXCOUNT2) << 8);
    if (count > 64u || (uint16_t)(rx_w - rx_r) + count > RING_CAP) return;
    fm1_usb_rx_sync();
    for (i = 0u; i < count; i++) rx_ring[(rx_w + i) % RING_CAP] = ep1_out[i];
    __asm__ volatile("csync" ::: "memory");
    rx_w = (uint16_t)(rx_w + count);
    csr = (csr & ~0x164u) | 0x10u;
    sie_write(RXCSR1, csr & 0xffu);
    sie_write(RXCSR2, csr >> 8);
    usb.rx_pending = 0u;
}

static void ep1_transmit(void)
{
    uint32_t csr, n = 0u;
    if (tx_r == tx_w) return;
    sie_write(INDEX, 1u);
    csr = sie_read(TXCSR1);
    if (csr & 1u) return;
    if (csr & 0x80u) sie_write(TXCSR1, csr & ~0x80u);
    while (tx_r != tx_w && n < 64u) {
        ep1_in[n++] = tx_ring[tx_r % RING_CAP];
        tx_r++;
    }
    fm1_usb_ep_send(1u, ep1_in, n);
    sie_write(TXCSR1, sie_read(TXCSR1) | 1u);
}

void nf_usb_start(void)
{
    usb.active = 0u;
    usb.configured = 0u;
    usb.rx_pending = 0u;
    usb.timeout_count = 0u;
    fm1_usb_reset();
    fm1_delay_ms(25u);
    fm1_usb_attach(ep0);
    sie_write(POWER, 0x60u);
    sie_write(INTRUSBE, 0x07u);
    sie_write(INTRTXE, 0x01u);
    sie_write(INTRRXE, 0u);
    fm1_usb_ep_enable(0x1fu);
    usb.active = usb.timeout_count < 3u;
}

void nf_usb_retry(uint32_t now_ms)
{
    if (!usb.active && now_ms - usb.retry_ms >= 1000u) {
        usb.retry_ms = now_ms;
        nf_usb_start();
    }
}

void nf_usb_poll(void)
{
    uint32_t status, tx, rx;
    if (!usb.active) return;
    status = sie_read(INTRUSB);
    tx = sie_read(INTRTX);
    rx = sie_read(INTRRX);
    if (usb.timeout_count > 50u) { usb.active = 0u; return; }
    if (status & 4u) {
        sie_write(FADDR, 0u);
        usb.configured = usb.sending = usb.address_pending = usb.rx_pending = 0u;
        rx_r = rx_w = tx_r = tx_w = 0u;
        fm1_usb_ep0_buf(ep0);
        sie_write(INTRUSBE, 0x07u);
        sie_write(INTRTXE, 0x01u);
        sie_write(INTRRXE, 0u);
    }
    if (tx & 1u) ep0_service();
    if (usb.configured) {
        if (rx & 2u) usb.rx_pending = 1u;
        if (usb.rx_pending) ep1_receive();
        ep1_transmit();
    }
}

int nf_usb_read_byte(uint8_t *byte)
{
    if (!byte || rx_r == rx_w) return 0;
    __asm__ volatile("csync" ::: "memory");
    *byte = rx_ring[rx_r % RING_CAP];
    rx_r++;
    return 1;
}

int nf_usb_write(const uint8_t *bytes, size_t length)
{
    size_t i;
    if (!bytes || !usb.configured || length > RING_CAP - (uint16_t)(tx_w - tx_r)) return -1;
    for (i = 0u; i < length; i++) tx_ring[(tx_w + i) % RING_CAP] = bytes[i];
    __asm__ volatile("csync" ::: "memory");
    tx_w = (uint16_t)(tx_w + length);
    return 0;
}

int nf_usb_connected(void) { return usb.active && usb.configured; }
void nf_usb_emit_note(uint8_t status, uint8_t note, uint8_t velocity)
{ (void)status; (void)note; (void)velocity; }
