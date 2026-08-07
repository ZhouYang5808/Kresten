/* ===== x86/kernel/net_udp.c: UDP client with DNS (A record) lookup ===== */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <net.h>

extern uint32_t sched_get_ticks(void);
extern uint32_t net_get_local_ip(void);
void net_poll(void);
int ip_send(uint32_t dst, uint8_t proto, const void *payload, uint16_t len);

/* UDP pseudo-header + datagram checksum (RFC 768). */
static uint16_t udp_checksum(uint32_t src, uint32_t dst,
                             const void *data, uint16_t len) {
    uint8_t ph[12];
    uint32_t s = htonl(src);
    uint32_t d = htonl(dst);
    memcpy(ph, &s, 4);
    memcpy(ph + 4, &d, 4);
    ph[8] = 0;
    ph[9] = 17;
    ph[10] = (uint8_t)(len >> 8);
    ph[11] = (uint8_t)(len & 0xFF);
    uint32_t sum = 0;
    for (int i = 0; i < 12; i += 2)
        sum += (uint16_t)((ph[i] << 8) | ph[i + 1]);
    const uint8_t *p = (const uint8_t *)data;
    uint16_t n = len;
    while (n >= 2) {
        sum += (uint16_t)((p[0] << 8) | p[1]);
        p += 2;
        n -= 2;
    }
    if (n)
        sum += (uint16_t)(p[0] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

struct udp_sock {
    uint16_t sport;
    int active;
    void (*cb)(const uint8_t *data, uint16_t len, uint32_t src);
};

static struct udp_sock usocks[4];

int udp_bind(uint16_t port, void (*cb)(const uint8_t *, uint16_t, uint32_t)) {
    for (int i = 0; i < 4; i++) {
        if (!usocks[i].active) {
            usocks[i].sport = port;
            usocks[i].cb = cb;
            usocks[i].active = 1;
            return 0;
        }
    }
    return -1;
}

void udp_rx(const uint8_t *pkt, uint16_t len, uint32_t src) {
    if (len < 8)
        return;
    uint16_t sport = (uint16_t)((pkt[0] << 8) | pkt[1]);
    uint16_t dport = (uint16_t)((pkt[2] << 8) | pkt[3]);
    uint16_t ulen = (uint16_t)((pkt[4] << 8) | pkt[5]);
    if (ulen < 8 || ulen > len)
        return;
    for (int i = 0; i < 4; i++)
        if (usocks[i].active && usocks[i].sport == dport) {
            if (usocks[i].cb)
                usocks[i].cb(pkt + 8, (uint16_t)(ulen - 8), src);
            return;
        }
    (void)sport;
}

int udp_send(uint32_t dst, uint16_t sport, uint16_t dport,
             const void *data, uint16_t len) {
    static uint8_t seg[1500];
    if (len + 8 > 1472)
        return -1;
    uint16_t ulen = (uint16_t)(8 + len);
    seg[0] = (uint8_t)(sport >> 8);
    seg[1] = (uint8_t)(sport & 0xFF);
    seg[2] = (uint8_t)(dport >> 8);
    seg[3] = (uint8_t)(dport & 0xFF);
    seg[4] = (uint8_t)(ulen >> 8);
    seg[5] = (uint8_t)(ulen & 0xFF);
    seg[6] = 0;
    seg[7] = 0;
    if (len > 0)
        memcpy(seg + 8, data, len);
    uint16_t cs = udp_checksum(net_get_local_ip(), dst, seg, ulen);
    seg[6] = (uint8_t)(cs >> 8);
    seg[7] = (uint8_t)(cs & 0xFF);
    return ip_send(dst, 17, seg, ulen);
}

/* ---- DNS: single A-record lookup, blocking with net_poll() ---- */

#define DNS_PORT 53

static int dns_pending = 0;
static uint32_t dns_result = 0;
static uint16_t dns_id = 0;
static uint16_t dns_sport = 0;

static void dns_rx(const uint8_t *data, uint16_t len, uint32_t src) {
    (void)src;
    if (!dns_pending || len < 12)
        return;
    uint16_t rid = (uint16_t)((data[0] << 8) | data[1]);
    uint16_t flags = (uint16_t)((data[2] << 8) | data[3]);
    if (rid != dns_id || !(flags & 0x8000))
        return;
    uint16_t qd = (uint16_t)((data[4] << 8) | data[5]);
    uint16_t an = (uint16_t)((data[6] << 8) | data[7]);
    uint16_t off = 12;
    while (qd > 0 && off < len) {
        while (off < len && data[off]) {
            off += (uint16_t)(data[off] + 1);
        }
        off += 5; /* zero label + type + class */
        qd--;
    }
    for (uint16_t i = 0; i < an && off < len; i++) {
        if (data[off] & 0xC0) {
            off += 2;
        } else {
            while (off < len && data[off])
                off += (uint16_t)(data[off] + 1);
            if (off >= len)
                return;
            off++;
        }
        if (off + 10 > len)
            return;
        uint16_t type = (uint16_t)((data[off] << 8) | data[off + 1]);
        uint16_t rlen = (uint16_t)((data[off + 8] << 8) | data[off + 9]);
        off += 10;
        if (type == 1 && rlen == 4 && off + 4 <= len) {
            dns_result = (uint32_t)((data[off] << 24) | (data[off + 1] << 16) |
                                    (data[off + 2] << 8) | data[off + 3]);
            dns_pending = 0;
            return;
        }
        off += rlen;
    }
    dns_pending = 0;
}

uint32_t net_nslookup(const char *domain) {
    static uint8_t q[512];
    if (dns_sport == 0) {
        dns_sport = 0xE300;
        udp_bind(dns_sport, dns_rx);
    }
    int qlen = 0;
    q[qlen++] = 0;
    q[qlen++] = 0;
    q[qlen++] = (uint8_t)(dns_id >> 8);
    q[qlen++] = (uint8_t)(dns_id & 0xFF);
    dns_id++;
    q[qlen++] = 0x01; /* RD */
    q[qlen++] = 0x00;
    q[qlen++] = 0x00;
    q[qlen++] = 0x01;
    q[qlen++] = 0x00;
    q[qlen++] = 0x00;
    q[qlen++] = 0x00;
    q[qlen++] = 0x00;
    const char *p = domain;
    while (*p) {
        const char *dot = strchr(p, '.');
        int n = dot ? (int)(dot - p) : (int)strlen(p);
        if (n <= 0 || n > 63 || qlen + n + 2 > (int)sizeof(q)) {
            dns_pending = 0;
            return 0;
        }
        q[qlen++] = (uint8_t)n;
        memcpy(q + qlen, p, (size_t)n);
        qlen += n;
        p += n + (dot ? 1 : 0);
    }
    q[qlen++] = 0;
    q[qlen++] = 0;
    q[qlen++] = 1; /* type A */
    q[qlen++] = 0;
    q[qlen++] = 1; /* class IN */
    q[0] = (uint8_t)(dns_id >> 8);
    q[1] = (uint8_t)(dns_id & 0xFF);

    NetConfig *nc = net_get_config();
    dns_pending = 1;
    dns_result = 0;
    if (udp_send(nc->dns, dns_sport, DNS_PORT, q, (uint16_t)qlen) != 0) {
        dns_pending = 0;
        return 0;
    }
    uint32_t deadline = sched_get_ticks() + 50;
    while (dns_pending && (int32_t)(sched_get_ticks() - deadline) < 0)
        net_poll();
    dns_pending = 0;
    return dns_result;
}
