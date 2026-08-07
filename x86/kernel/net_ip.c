/* ===== x86/kernel/net_ip.c: IPv4 send/receive with checksum ===== */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <net.h>

uint32_t net_get_local_ip(void);
int eth_send(const uint8_t *dst, uint16_t ethertype,
             const void *payload, uint16_t len);
int arp_resolve(uint32_t ip, uint8_t *mac);

void icmp_rx(const uint8_t *pkt, uint16_t len, uint32_t src);
void udp_rx(const uint8_t *pkt, uint16_t len, uint32_t src);
void tcp_rx(const uint8_t *pkt, uint16_t len, uint32_t src);

static uint16_t ip_id = 0x4000;

/* Standard one's-complement Internet checksum over the given bytes. */
static uint16_t checksum(const void *data, uint16_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    while (len >= 2) {
        sum += (uint16_t)((p[0] << 8) | p[1]);
        p += 2;
        len -= 2;
    }
    if (len)
        sum += (uint16_t)(p[0] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

void ip_rx(const uint8_t *frame, uint16_t len) {
    if (len < 34)
        return;
    const uint8_t *ip = frame + 14;
    uint16_t ihl = (uint16_t)((ip[0] & 0x0F) * 4);
    uint16_t total = (uint16_t)((ip[2] << 8) | ip[3]);
    uint32_t src = (uint32_t)((ip[12] << 24) | (ip[13] << 16) |
                              (ip[14] << 8) | ip[15]);
    uint32_t dst = (uint32_t)((ip[16] << 24) | (ip[17] << 16) |
                              (ip[18] << 8) | ip[19]);
    if (ihl < 20 || len < 14 + ihl)
        return;
    if (total > len - 14 || total < ihl)
        return;
    if (dst != net_get_local_ip() && dst != 0xFFFFFFFF)
        return;
    const uint8_t *payload = ip + ihl;
    uint16_t plen = (uint16_t)(total - ihl);
    switch (ip[9]) {
    case 1:
        icmp_rx(payload, plen, src);
        break;
    case 17:
        udp_rx(payload, plen, src);
        break;
    case 6:
        tcp_rx(payload, plen, src);
        break;
    default:
        break;
    }
}

int ip_send(uint32_t dst, uint8_t proto, const void *payload, uint16_t len) {
    static uint8_t pkt[1500];
    if (len > 1480)
        return -1;
    uint8_t *ip = pkt;
    ip[0] = 0x45;
    ip[1] = 0;
    uint16_t total = (uint16_t)(20 + len);
    ip[2] = (uint8_t)(total >> 8);
    ip[3] = (uint8_t)(total & 0xFF);
    ip_id++;
    ip[4] = (uint8_t)(ip_id >> 8);
    ip[5] = (uint8_t)(ip_id & 0xFF);
    ip[6] = 0x40; /* DF */
    ip[7] = 0;
    ip[8] = 64;   /* TTL */
    ip[9] = proto;
    uint32_t src = net_get_local_ip();
    uint32_t src_n = htonl(src);
    uint32_t dst_n = htonl(dst);
    memcpy(ip + 12, &src_n, 4);
    memcpy(ip + 16, &dst_n, 4);
    ip[10] = 0;
    ip[11] = 0;
    uint16_t cs = checksum(ip, 20);
    ip[10] = (uint8_t)(cs >> 8);
    ip[11] = (uint8_t)(cs & 0xFF);
    if (len > 0)
        memcpy(ip + 20, payload, len);

    uint8_t mac[6];
    if (arp_resolve(dst, mac) != 0)
        return -1;
    return eth_send(mac, 0x0800, pkt, total);
}
