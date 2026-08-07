/* ===== x86/kernel/net_eth.c: Ethernet frame send/receive ===== */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern const uint8_t *rtl8139_get_mac(void);
extern int rtl8139_send(const uint8_t *frame, uint16_t len);

void ip_rx(const uint8_t *frame, uint16_t len);
void arp_rx(const uint8_t *frame, uint16_t len);

/* libc has no memcmp: compare raw bytes (MACs may contain 0x00) */
static int memcmp8(const void *a, const void *b, size_t n) {
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return (int)x[i] - (int)y[i];
    return 0;
}

void eth_rx(const uint8_t *frame, uint16_t len) {
    if (len < 14)
        return;
    const uint8_t *mac = rtl8139_get_mac();
    if (memcmp8(frame, mac, 6) != 0 && !(frame[0] & 0x01))
        return; /* not addressed to us */
    uint16_t type = (uint16_t)((frame[12] << 8) | frame[13]);
    switch (type) {
    case 0x0806:
        arp_rx(frame, len);
        break;
    case 0x0800:
        ip_rx(frame, len);
        break;
    default:
        break;
    }
}

int eth_send(const uint8_t *dst, uint16_t ethertype,
             const void *payload, uint16_t len) {
    static uint8_t frame[1514];
    if (len > 1500)
        return -1;
    const uint8_t *mac = rtl8139_get_mac();
    memcpy(frame, dst, 6);
    memcpy(frame + 6, mac, 6);
    frame[12] = (uint8_t)(ethertype >> 8);
    frame[13] = (uint8_t)(ethertype & 0xFF);
    if (len > 0)
        memcpy(frame + 14, payload, len);
    if (14 + len < 60) {
        memset(frame + 14 + len, 0, 60 - 14 - len);
        return rtl8139_send(frame, 60);
    }
    return rtl8139_send(frame, (uint16_t)(14 + len));
}
