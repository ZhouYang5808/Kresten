/* ===== x86/kernel/net_icmp.c: ICMP echo (ping) client/reply ===== */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern uint32_t sched_get_ticks(void);
void net_poll(void);
int ip_send(uint32_t dst, uint8_t proto, const void *payload, uint16_t len);

static int ping_pending = 0;
static uint16_t ping_id = 0;
static uint16_t ping_seq = 0;
static uint32_t ping_rtt = 0;

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

void icmp_rx(const uint8_t *pkt, uint16_t len, uint32_t src) {
    (void)src;
    if (len < 8)
        return;
    uint8_t type = pkt[0];
    uint16_t id = (uint16_t)((pkt[4] << 8) | pkt[5]);
    uint16_t seq = (uint16_t)((pkt[6] << 8) | pkt[7]);
    if (type == 8) {
        /* echo request: answer with an echo reply */
        static uint8_t reply[64];
        uint16_t n = len < 64 ? len : 64;
        memcpy(reply, pkt, n);
        reply[0] = 0;
        reply[1] = 0;
        reply[2] = 0;
        reply[3] = 0;
        uint16_t cs = checksum(reply, n);
        reply[2] = (uint8_t)(cs >> 8);
        reply[3] = (uint8_t)(cs & 0xFF);
        ip_send(src, 1, reply, n);
    } else if (type == 0 && ping_pending && id == ping_id && seq == ping_seq) {
        ping_pending = 0;
        ping_rtt = sched_get_ticks();
    }
}

int icmp_ping(uint32_t ip, int count) {
    static uint8_t req[16];
    int ok = 0;
    for (int i = 0; i < count && i < 10; i++) {
        ping_seq++;
        ping_id = (uint16_t)(0x1234 + ping_seq);
        ping_pending = 1;
        ping_rtt = 0;

        memset(req, 0, sizeof(req));
        req[0] = 8; /* echo request */
        req[1] = 0;
        req[4] = (uint8_t)(ping_id >> 8);
        req[5] = (uint8_t)(ping_id & 0xFF);
        req[6] = (uint8_t)(ping_seq >> 8);
        req[7] = (uint8_t)(ping_seq & 0xFF);
        memcpy(req + 8, "kresten", 8);
        uint16_t cs = checksum(req, sizeof(req));
        req[2] = (uint8_t)(cs >> 8);
        req[3] = (uint8_t)(cs & 0xFF);

        uint32_t t0 = sched_get_ticks();
        if (ip_send(ip, 1, req, sizeof(req)) != 0) {
            ping_pending = 0;
            printf("  ping: send failed\n");
            continue;
        }
        uint32_t deadline = t0 + 30;
        while (ping_pending && (int32_t)(sched_get_ticks() - deadline) < 0)
            net_poll();
        if (!ping_pending) {
            uint32_t rtt = sched_get_ticks() - t0;
            printf("  reply from %u.%u.%u.%u: seq=%u rtt=%u ms\n",
                   (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
                   (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF),
                   (unsigned)ping_seq, (unsigned)(rtt * 10));
            ok++;
        } else {
            ping_pending = 0;
            printf("  request timed out (seq=%d)\n", ping_seq);
        }
    }
    return ok;
}
