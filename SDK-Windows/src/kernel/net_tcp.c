/* ===== x86/kernel/net_tcp.c: minimal TCP client (single connection) ===== */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <net.h>

extern uint32_t sched_get_ticks(void);
extern uint32_t net_get_local_ip(void);
void net_poll(void);
int ip_send(uint32_t dst, uint8_t proto, const void *payload, uint16_t len);

#define TCP_CLOSED     0
#define TCP_SYN_SENT   1
#define TCP_ESTAB      2
#define TCP_FIN_SENT   3
#define TCP_CLOSE_WAIT 4

#define FLAG_FIN 0x01
#define FLAG_SYN 0x02
#define FLAG_ACK 0x10
#define FLAG_PSH 0x08

static struct {
    int state;
    uint32_t rip;
    uint16_t rport;
    uint16_t lport;
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint32_t last_tick;
    int rxlen;
    uint8_t rxbuf[4096];
} tcp;

static uint16_t checksum16(const void *data, uint16_t len, uint32_t *acc) {
    const uint8_t *p = (const uint8_t *)data;
    uint16_t n = len;
    while (n >= 2) {
        *acc += (uint16_t)((p[0] << 8) | p[1]);
        p += 2;
        n -= 2;
    }
    if (n)
        *acc += (uint16_t)(p[0] << 8);
    while (*acc >> 16)
        *acc = (*acc & 0xFFFF) + (*acc >> 16);
    return (uint16_t)~(*acc & 0xFFFF);
}

static uint16_t tcp_checksum(uint32_t src, uint32_t dst,
                             const void *seg, uint16_t seglen) {
    uint32_t acc = 0;
    uint8_t ph[12];
    uint32_t s = htonl(src);
    uint32_t d = htonl(dst);
    memcpy(ph, &s, 4);
    memcpy(ph + 4, &d, 4);
    ph[8] = 0;
    ph[9] = 6;
    ph[10] = (uint8_t)(seglen >> 8);
    ph[11] = (uint8_t)(seglen & 0xFF);
    checksum16(ph, 12, &acc);
    return checksum16(seg, seglen, &acc);
}

/* Send one TCP segment; caller already updated snd_nxt. */
static int tcp_send_seg(uint8_t flags, uint32_t seq, uint32_t ack,
                        const void *data, uint16_t len) {
    static uint8_t seg[1500];
    uint16_t seglen = (uint16_t)(20 + len);
    if (seglen > 1460)
        return -1;
    seg[0] = (uint8_t)(tcp.lport >> 8);
    seg[1] = (uint8_t)(tcp.lport & 0xFF);
    seg[2] = (uint8_t)(tcp.rport >> 8);
    seg[3] = (uint8_t)(tcp.rport & 0xFF);
    seg[4] = (uint8_t)(seq >> 24);
    seg[5] = (uint8_t)(seq >> 16);
    seg[6] = (uint8_t)(seq >> 8);
    seg[7] = (uint8_t)(seq & 0xFF);
    seg[8] = (uint8_t)(ack >> 24);
    seg[9] = (uint8_t)(ack >> 16);
    seg[10] = (uint8_t)(ack >> 8);
    seg[11] = (uint8_t)(ack & 0xFF);
    seg[12] = 0x50; /* data offset 5 */
    seg[13] = flags;
    seg[14] = 0x20; /* window */
    seg[15] = 0x00;
    seg[16] = 0;
    seg[17] = 0;
    seg[18] = 0;
    seg[19] = 0;
    if (len > 0)
        memcpy(seg + 20, data, len);
    uint16_t cs = tcp_checksum(net_get_local_ip(), tcp.rip, seg, seglen);
    seg[16] = (uint8_t)(cs >> 8);
    seg[17] = (uint8_t)(cs & 0xFF);
    return ip_send(tcp.rip, 6, seg, seglen);
}

void tcp_rx(const uint8_t *pkt, uint16_t len, uint32_t src) {
    (void)src;
    if (tcp.state == TCP_CLOSED || len < 20)
        return;
    uint16_t dport = (uint16_t)((pkt[2] << 8) | pkt[3]);
    if (dport != tcp.lport)
        return;
    uint32_t seq = (uint32_t)((pkt[4] << 24) | (pkt[5] << 16) |
                              (pkt[6] << 8) | pkt[7]);
    uint32_t ack = (uint32_t)((pkt[8] << 24) | (pkt[9] << 16) |
                              (pkt[10] << 8) | pkt[11]);
    uint8_t flags = pkt[13];
    uint16_t doff = (uint16_t)((pkt[12] >> 4) * 4);
    if (doff < 20 || doff > len)
        return;
    const uint8_t *payload = pkt + doff;
    uint16_t dlen = (uint16_t)(len - doff);

    if (tcp.state == TCP_SYN_SENT) {
        if (flags & FLAG_SYN) {
            if (!(flags & FLAG_ACK))
                return;
            tcp.snd_una = ack;
            tcp.rcv_nxt = seq + 1;
            tcp.state = TCP_ESTAB;
            tcp_send_seg(FLAG_ACK, tcp.snd_nxt, tcp.rcv_nxt, NULL, 0);
            return;
        }
        return;
    }
    if (tcp.state == TCP_ESTAB || tcp.state == TCP_CLOSE_WAIT) {
        if (flags & FLAG_ACK && (int32_t)(ack - tcp.snd_una) > 0)
            tcp.snd_una = ack;
        if (dlen > 0 && seq == tcp.rcv_nxt &&
            tcp.rxlen + (int)dlen <= (int)sizeof(tcp.rxbuf)) {
            memcpy(tcp.rxbuf + tcp.rxlen, payload, dlen);
            tcp.rxlen += dlen;
            tcp.rcv_nxt = seq + dlen;
            tcp_send_seg(FLAG_ACK, tcp.snd_nxt, tcp.rcv_nxt, NULL, 0);
        }
        if (flags & FLAG_FIN) {
            tcp.rcv_nxt += 1;
            tcp.state = TCP_CLOSE_WAIT;
            tcp_send_seg(FLAG_ACK, tcp.snd_nxt, tcp.rcv_nxt, NULL, 0);
        }
        return;
    }
    if (tcp.state == TCP_FIN_SENT) {
        if (flags & FLAG_ACK)
            tcp.state = TCP_CLOSED;
        return;
    }
}

int tcp_connect(uint32_t ip, uint16_t port) {
    tcp.rip = ip;
    tcp.rport = port;
    tcp.lport = (uint16_t)(0x9000 + (sched_get_ticks() & 0xFF));
    tcp.snd_una = 0;
    tcp.snd_nxt = 0x1000;
    tcp.rcv_nxt = 0;
    tcp.rxlen = 0;
    tcp.last_tick = sched_get_ticks();
    tcp.state = TCP_SYN_SENT;
    tcp_send_seg(FLAG_SYN, tcp.snd_nxt++, 0, NULL, 0);
    uint32_t deadline = tcp.last_tick + 100;
    while (tcp.state != TCP_ESTAB &&
           (int32_t)(sched_get_ticks() - deadline) < 0) {
        if ((int32_t)(sched_get_ticks() - tcp.last_tick) >= 10) {
            tcp.last_tick = sched_get_ticks();
            tcp_send_seg(FLAG_SYN, tcp.snd_nxt, 0, NULL, 0);
        }
        net_poll();
    }
    return tcp.state == TCP_ESTAB ? 0 : -1;
}

int tcp_send(const void *data, uint16_t len) {
    if (tcp.state != TCP_ESTAB)
        return -1;
    if (tcp_send_seg(FLAG_PSH | FLAG_ACK, tcp.snd_nxt, tcp.rcv_nxt,
                     data, len) != 0)
        return -1;
    tcp.snd_nxt += len;
    return 0;
}

void tcp_close(void) {
    if (tcp.state != TCP_ESTAB && tcp.state != TCP_CLOSE_WAIT)
        return;
    tcp.state = TCP_FIN_SENT;
    tcp_send_seg(FLAG_FIN | FLAG_ACK, tcp.snd_nxt, tcp.rcv_nxt, NULL, 0);
    tcp.snd_nxt++;
    uint32_t deadline = sched_get_ticks() + 50;
    while (tcp.state != TCP_CLOSED &&
           (int32_t)(sched_get_ticks() - deadline) < 0)
        net_poll();
    tcp.state = TCP_CLOSED;
}

int tcp_rxlen(void) {
    return tcp.rxlen;
}

const uint8_t *tcp_rxdata(void) {
    return tcp.rxbuf;
}

void tcp_reset_rx(void) {
    tcp.rxlen = 0;
}

int tcp_connected(void) {
    return tcp.state == TCP_ESTAB;
}
