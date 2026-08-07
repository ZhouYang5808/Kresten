/* ===== kernel: net.c ===== */
#include <net.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <ctype.h>
#include <defense.h>

/* ===== kernel: net ===== */

static NetConfig g_net;

#define ARP_CACHE_SIZE 8

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    int valid;
} ArpEntry;

static ArpEntry g_arp_cache[ARP_CACHE_SIZE];
static int g_in_poll = 0;

static volatile int g_ping_received = 0;
static volatile uint16_t g_ping_seq = 0;
static volatile int g_dns_received = 0;
static volatile uint32_t g_dns_resolved_ip = 0;
static volatile int g_dhcp_state = 0;

static volatile int g_tcp_state = 0;
static volatile uint32_t g_tcp_server_seq = 0;
static volatile uint32_t g_tcp_my_seq = 0;
static char *g_tcp_rx_buf = NULL;
static int g_tcp_rx_len = 0;
static int g_tcp_rx_max = 0;

#define ETH_HDR 14
#define IP_HDR 20
#define ICMP_HDR 8
#define UDP_HDR 8
#define TCP_HDR 20
#define ARP_HDR 28

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xFF);
}

static uint32_t csum_accum(const uint8_t *b, size_t len) {
    uint32_t sum = 0;
    size_t i = 0;
    for (; i + 1 < len; i += 2)
        sum += ((uint16_t)b[i] << 8) | b[i + 1];
    if (i < len)
        sum += (uint16_t)b[i] << 8;
    return sum;
}

static uint16_t csum_fold(uint32_t sum) {
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

static uint16_t net_checksum(const void *buf, size_t len) {
    return csum_fold(csum_accum((const uint8_t *)buf, len));
}

static uint16_t tcp_udp_checksum(uint32_t src_ip, uint32_t dst_ip, uint8_t proto, const void *seg, uint16_t seg_len) {
    uint8_t pseudo[12];
    memcpy(&pseudo[0], &src_ip, 4);
    memcpy(&pseudo[4], &dst_ip, 4);
    pseudo[8] = 0;
    pseudo[9] = proto;
    pseudo[10] = (uint8_t)(seg_len >> 8);
    pseudo[11] = (uint8_t)(seg_len & 0xFF);
    uint32_t sum = csum_accum(pseudo, 12) + csum_accum((const uint8_t *)seg, seg_len);
    return csum_fold(sum);
}

void ip_to_str(uint32_t ip, char *buf) {
    const uint8_t *p = (const uint8_t *)&ip;
    sprintf(buf, "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
}

uint32_t str_to_ip(const char *str) {
    int a = 0, b = 0, c = 0, d = 0;
    int i = 0;
    const char *p = str;
    if (!p) return 0;
    while (*p && *p >= '0' && *p <= '9') { a = a * 10 + (*p - '0'); p++; i++; }
    if (*p == '.') p++; else if (i == 0) return 0;
    i = 0;
    while (*p && *p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; i++; }
    if (*p == '.') p++; else if (i == 0) return 0;
    i = 0;
    while (*p && *p >= '0' && *p <= '9') { c = c * 10 + (*p - '0'); p++; i++; }
    if (*p == '.') p++; else if (i == 0) return 0;
    i = 0;
    while (*p && *p >= '0' && *p <= '9') { d = d * 10 + (*p - '0'); p++; i++; }
    if (i == 0 || *p) return 0;
    uint8_t bytes[4];
    bytes[0] = (uint8_t)a;
    bytes[1] = (uint8_t)b;
    bytes[2] = (uint8_t)c;
    bytes[3] = (uint8_t)d;
    uint32_t v;
    memcpy(&v, bytes, 4);
    return v;
}

#define SMC_BASE 0x10010000

static inline void smc_bank(int b) {
    *(volatile uint16_t *)(SMC_BASE + 0x0E) = (uint16_t)b;
}

static inline uint16_t smc_read(int b, int reg) {
    smc_bank(b);
    return *(volatile uint16_t *)(SMC_BASE + reg);
}

static inline void smc_write(int b, int reg, uint16_t val) {
    smc_bank(b);
    *(volatile uint16_t *)(SMC_BASE + reg) = val;
}

static int smc_init(void) {
    uint16_t sig = smc_read(0, 0x0E);
    if ((sig & 0xFF00) != 0x3300)
        return -1;

    g_net.mac[0] = (uint8_t)(smc_read(1, 0x04) & 0xFF);
    g_net.mac[1] = (uint8_t)(smc_read(1, 0x04) >> 8);
    g_net.mac[2] = (uint8_t)(smc_read(1, 0x06) & 0xFF);
    g_net.mac[3] = (uint8_t)(smc_read(1, 0x06) >> 8);
    g_net.mac[4] = (uint8_t)(smc_read(1, 0x08) & 0xFF);
    g_net.mac[5] = (uint8_t)(smc_read(1, 0x08) >> 8);

    smc_write(1, 0x0C, 0x0800);
    smc_write(2, 0x00, 0x0040);
    smc_write(0, 0x00, 0x0081);
    smc_write(0, 0x04, 0x0304);
    return 0;
}

static int smc_send(const void *buf, uint16_t len) {
    if (len == 0 || len > 1514)
        return -1;

    smc_write(2, 0x00, 0x0020);
    int timeout = 10000;
    uint16_t arr = 0;
    while (timeout-- > 0) {
        arr = smc_read(2, 0x02);
        if ((arr & 0x8000) == 0)
            break;
    }
    if (timeout <= 0)
        return -1;

    uint8_t pkt_num = (arr >> 8) & 0x3F;
    smc_write(2, 0x02, pkt_num);
    smc_write(2, 0x06, 0x4000);

    const uint8_t *b = (const uint8_t *)buf;
    uint16_t even = len & ~1;
    uint16_t count = (uint16_t)(6 + even);

    volatile uint16_t *d16 = (volatile uint16_t *)(SMC_BASE + 0x08);
    smc_bank(2);
    *d16 = 0x0000;
    *d16 = count;

    uint16_t i;
    for (i = 0; i + 1 < even; i += 2)
        *d16 = (uint16_t)(b[i] | (b[i + 1] << 8));

    volatile uint8_t *d8 = (volatile uint8_t *)(SMC_BASE + 0x08);
    if (len & 1) {
        *d8 = b[len - 1];
        *d8 = 0x20;
    } else {
        *d8 = 0x00;
        *d8 = 0x00;
    }

    smc_write(2, 0x00, 0x00C0);
    g_net.tx_packets++;
    return 0;
}

static int smc_recv(void *buf, uint16_t max_len) {
    smc_bank(2);
    uint16_t fifo = *(volatile uint16_t *)(SMC_BASE + 0x04);
    if (fifo & 0x8000)
        return 0;

    uint8_t pkt_num = (fifo >> 8) & 0x3F;
    *(volatile uint16_t *)(SMC_BASE + 0x02) = pkt_num;
    *(volatile uint16_t *)(SMC_BASE + 0x06) = 0xC000;

    volatile uint16_t *d16 = (volatile uint16_t *)(SMC_BASE + 0x08);
    uint16_t status = *d16;
    uint16_t count = *d16;
    (void)status;

    if (count < 6) {
        *(volatile uint16_t *)(SMC_BASE + 0x00) = 0x0080;
        return 0;
    }

    uint16_t payload = (uint16_t)(count - 6);
    uint16_t read_len = payload;
    if (read_len > max_len)
        read_len = max_len;

    uint8_t *b = (uint8_t *)buf;
    uint16_t i;
    for (i = 0; i + 1 < read_len; i += 2) {
        uint16_t w = *d16;
        b[i] = (uint8_t)(w & 0xFF);
        b[i + 1] = (uint8_t)(w >> 8);
    }
    if (read_len & 1)
        b[read_len - 1] = *(volatile uint8_t *)(SMC_BASE + 0x08);

    uint8_t last = *(volatile uint8_t *)(SMC_BASE + 0x08);
    uint8_t control = *(volatile uint8_t *)(SMC_BASE + 0x08);
    if ((control & 0x20) && payload < max_len) {
        b[payload] = last;
        payload++;
    }

    *(volatile uint16_t *)(SMC_BASE + 0x00) = 0x0080;
    g_net.rx_packets++;
    return (int)payload;
}

static void delay_loop(void) {
    for (volatile int i = 0; i < 20000; i++);
}

static unsigned udiv(unsigned a, unsigned b) {
    unsigned q = 0;
    if (b == 0)
        return 0;
    while (a >= b) {
        a -= b;
        q++;
    }
    return q;
}

static void arp_request(uint32_t target_ip) {
    uint8_t frame[ETH_HDR + ARP_HDR];
    memset(frame, 0xFF, 6);
    memcpy(frame + 6, g_net.mac, 6);
    wr16(frame + 12, 0x0806);
    uint8_t *a = frame + ETH_HDR;
    wr16(a + 0, 1);
    wr16(a + 2, 0x0800);
    a[4] = 6;
    a[5] = 4;
    wr16(a + 6, 1);
    memcpy(a + 8, g_net.mac, 6);
    memcpy(a + 14, &g_net.ip, 4);
    memset(a + 18, 0, 6);
    memcpy(a + 24, &target_ip, 4);
    smc_send(frame, ETH_HDR + ARP_HDR);
}

static int arp_get_mac(uint32_t target_ip, uint8_t mac_out[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == target_ip) {
            memcpy(mac_out, g_arp_cache[i].mac, 6);
            return 0;
        }
    }

    arp_request(target_ip);

    for (int i = 0; i < 400; i++) {
        net_poll();
        for (int j = 0; j < ARP_CACHE_SIZE; j++) {
            if (g_arp_cache[j].valid && g_arp_cache[j].ip == target_ip) {
                memcpy(mac_out, g_arp_cache[j].mac, 6);
                return 0;
            }
        }
        delay_loop();
    }
    return -1;
}

static void net_send_ip(uint32_t dst_ip, uint8_t proto, const void *payload, uint16_t payload_len) {
    uint8_t frame[1536];
    uint16_t total = (uint16_t)(ETH_HDR + IP_HDR + payload_len);
    if (total > 1514)
        return;

    uint8_t bcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t mac[6];
    uint8_t *dst_mac = bcast_mac;

    if (dst_ip != 0xFFFFFFFF) {
        uint32_t next_hop = dst_ip;
        if ((dst_ip & g_net.netmask) != (g_net.ip & g_net.netmask) && g_net.gateway != 0)
            next_hop = g_net.gateway;
        if (arp_get_mac(next_hop, mac) == 0)
            dst_mac = mac;
    }

    memcpy(frame, dst_mac, 6);
    memcpy(frame + 6, g_net.mac, 6);
    wr16(frame + 12, 0x0800);

    uint8_t *ip = frame + ETH_HDR;
    memset(ip, 0, IP_HDR);
    ip[0] = 0x45;
    wr16(ip + 2, (uint16_t)(IP_HDR + payload_len));
    static uint16_t ip_id = 1;
    wr16(ip + 4, ip_id++);
    ip[8] = 64;
    ip[9] = proto;
    memcpy(ip + 12, &g_net.ip, 4);
    memcpy(ip + 16, &dst_ip, 4);
    wr16(ip + 10, net_checksum(ip, IP_HDR));

    memcpy(ip + IP_HDR, payload, payload_len);
    smc_send(frame, total);
}

static void udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, const void *payload, uint16_t payload_len) {
    uint8_t buf[1400];
    uint16_t total = (uint16_t)(UDP_HDR + payload_len);
    wr16(buf + 0, src_port);
    wr16(buf + 2, dst_port);
    wr16(buf + 4, total);
    wr16(buf + 6, 0);
    memcpy(buf + UDP_HDR, payload, payload_len);
    net_send_ip(dst_ip, 17, buf, total);
}

static void tcp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, uint32_t seq, uint32_t ack, uint8_t flags, const void *payload, uint16_t payload_len) {
    uint8_t buf[1400];
    uint16_t total = (uint16_t)(TCP_HDR + payload_len);
    memset(buf, 0, TCP_HDR);
    wr16(buf + 0, src_port);
    wr16(buf + 2, dst_port);
    wr32(buf + 4, seq);
    wr32(buf + 8, ack);
    buf[12] = 0x50;
    buf[13] = flags;
    wr16(buf + 14, 8192);
    wr16(buf + 18, 0);
    if (payload_len > 0 && payload)
        memcpy(buf + TCP_HDR, payload, payload_len);
    wr16(buf + 16, tcp_udp_checksum(g_net.ip, dst_ip, 6, buf, total));
    net_send_ip(dst_ip, 6, buf, total);
}

static void icmp_process(const uint8_t *pkt, uint16_t len, uint32_t src_ip) {
    if (len < ICMP_HDR)
        return;
    uint8_t type = pkt[0];
    if (type == 8) {
        uint8_t reply[128];
        if (len > sizeof(reply))
            len = sizeof(reply);
        memcpy(reply, pkt, len);
        reply[0] = 0;
        wr16(reply + 2, 0);
        wr16(reply + 2, net_checksum(reply, len));
        net_send_ip(src_ip, 1, reply, len);
    } else if (type == 0) {
        g_ping_received = 1;
        g_ping_seq = rd16(pkt + 6);
    }
}

static void dns_process(const uint8_t *payload, uint16_t len) {
    if (len < 12)
        return;
    uint16_t flags = rd16(payload + 2);
    uint16_t qdcount = rd16(payload + 4);
    uint16_t ancount = rd16(payload + 6);
    if ((flags & 0x8000) == 0 || ancount == 0)
        return;

    const uint8_t *p = payload + 12;
    const uint8_t *end = payload + len;
    for (int i = 0; i < qdcount; i++) {
        if (p >= end)
            return;
        while (*p != 0) {
            if ((*p & 0xC0) == 0xC0) { p += 2; break; }
            p += (*p) + 1;
            if (p >= end)
                return;
        }
        if (p < end && *p == 0)
            p++;
        p += 4;
    }

    for (int i = 0; i < ancount; i++) {
        if (p >= end)
            return;
        if ((*p & 0xC0) == 0xC0) {
            p += 2;
        } else {
            while (*p != 0) {
                p += (*p) + 1;
                if (p >= end)
                    return;
            }
            if (p < end && *p == 0)
                p++;
        }
        if (p + 10 > end)
            return;
        uint16_t type = rd16(p);
        uint16_t cls = rd16(p + 2);
        uint16_t rdlen = rd16(p + 8);
        p += 10;
        if (type == 1 && cls == 1 && rdlen == 4) {
            g_dns_resolved_ip = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                                ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            g_dns_received = 1;
            return;
        }
        p += rdlen;
    }
}

static void udp_process(const uint8_t *pkt, uint16_t len) {
    if (len < UDP_HDR)
        return;
    uint16_t src_port = rd16(pkt + 0);
    uint16_t dst_port = rd16(pkt + 2);
    uint16_t udp_len = rd16(pkt + 4);
    if (udp_len < UDP_HDR || udp_len > len)
        return;
    const uint8_t *payload = pkt + UDP_HDR;
    uint16_t payload_len = (uint16_t)(udp_len - UDP_HDR);

    if (dst_port == 68) {
        if (payload_len > 240 && rd32(payload + 236) == 0x63825363) {
            memcpy(&g_net.ip, payload + 16, 4);
            const uint8_t *opt = payload + 240;
            const uint8_t *end = payload + payload_len;
            while (opt < end && *opt != 255) {
                uint8_t type = *opt++;
                if (type == 0)
                    continue;
                if (opt >= end)
                    break;
                uint8_t opt_len = *opt++;
                if (opt + opt_len > end)
                    break;
                if (type == 53) {
                    uint8_t mt = opt[0];
                    if (mt == 2)
                        g_dhcp_state = 1;
                    else if (mt == 5)
                        g_dhcp_state = 2;
                } else if (type == 1 && opt_len >= 4) {
                    memcpy(&g_net.netmask, opt, 4);
                } else if (type == 3 && opt_len >= 4) {
                    memcpy(&g_net.gateway, opt, 4);
                } else if (type == 6 && opt_len >= 4) {
                    memcpy(&g_net.dns, opt, 4);
                }
                opt += opt_len;
            }
        }
    } else if (src_port == 53) {
        dns_process(payload, payload_len);
    }
}

static void tcp_process(const uint8_t *pkt, uint16_t len, uint32_t src_ip) {
    if (len < TCP_HDR)
        return;
    uint8_t hdr_len = (pkt[12] >> 4) * 4;
    if (hdr_len < TCP_HDR || hdr_len > len)
        return;
    uint32_t seq = rd32(pkt + 4);
    uint8_t flags = pkt[13];
    uint16_t src_port = rd16(pkt + 0);
    uint16_t dst_port = rd16(pkt + 2);
    const uint8_t *payload = pkt + hdr_len;
    uint16_t payload_len = (uint16_t)(len - hdr_len);

    if (flags & 0x02) {
        if (g_tcp_state == 1) {
            g_tcp_server_seq = seq + 1;
            g_tcp_state = 2;
        }
    }

    if (payload_len > 0 && (g_tcp_state == 2 || g_tcp_state == 3)) {
        if (g_tcp_rx_buf && g_tcp_rx_len < g_tcp_rx_max) {
            int copy_len = payload_len;
            if (g_tcp_rx_len + copy_len > g_tcp_rx_max)
                copy_len = g_tcp_rx_max - g_tcp_rx_len;
            memcpy(g_tcp_rx_buf + g_tcp_rx_len, payload, copy_len);
            g_tcp_rx_len += copy_len;
            g_tcp_rx_buf[g_tcp_rx_len] = '\0';
        }
        g_tcp_server_seq += payload_len;
        tcp_send(src_ip, dst_port, src_port, g_tcp_my_seq, g_tcp_server_seq, 0x10, NULL, 0);
    }

    if (flags & 0x01) {
        g_tcp_server_seq++;
        g_tcp_state = 3;
        tcp_send(src_ip, dst_port, src_port, g_tcp_my_seq, g_tcp_server_seq, 0x10, NULL, 0);
    }
}

void net_poll(void) {
    if (g_in_poll)
        return;
    g_in_poll = 1;
    for (int n = 0; n < 8; n++) {
        uint8_t frame[1600];
        int len = smc_recv(frame, sizeof(frame));
        if (len < ETH_HDR)
            break;
        uint16_t ethertype = rd16(frame + 12);

        if (ethertype == 0x0806) {
            if (len < ETH_HDR + ARP_HDR)
                continue;
            const uint8_t *a = frame + ETH_HDR;
            if (rd16(a + 0) != 1 || rd16(a + 2) != 0x0800)
                continue;
            uint32_t sender_ip;
            memcpy(&sender_ip, a + 14, 4);

            int found = 0;
            for (int i = 0; i < ARP_CACHE_SIZE; i++) {
                if (g_arp_cache[i].valid && g_arp_cache[i].ip == sender_ip) {
                    memcpy(g_arp_cache[i].mac, a + 8, 6);
                    found = 1;
                    break;
                }
            }
            if (!found) {
                for (int i = 0; i < ARP_CACHE_SIZE; i++) {
                    if (!g_arp_cache[i].valid) {
                        g_arp_cache[i].ip = sender_ip;
                        memcpy(g_arp_cache[i].mac, a + 8, 6);
                        g_arp_cache[i].valid = 1;
                        break;
                    }
                }
            }

            if (rd16(a + 6) == 1) {
                uint32_t target_ip;
                memcpy(&target_ip, a + 24, 4);
                if (target_ip == g_net.ip) {
                    uint8_t reply[ETH_HDR + ARP_HDR];
                    memcpy(reply, a + 8, 6);
                    memcpy(reply + 6, g_net.mac, 6);
                    wr16(reply + 12, 0x0806);
                    uint8_t *r = reply + ETH_HDR;
                    wr16(r + 0, 1);
                    wr16(r + 2, 0x0800);
                    r[4] = 6;
                    r[5] = 4;
                    wr16(r + 6, 2);
                    memcpy(r + 8, g_net.mac, 6);
                    memcpy(r + 14, &g_net.ip, 4);
                    memcpy(r + 18, a + 8, 6);
                    memcpy(r + 24, &sender_ip, 4);
                    smc_send(reply, ETH_HDR + ARP_HDR);
                }
            }
        } else if (ethertype == 0x0800) {
            if (len < ETH_HDR + IP_HDR)
                continue;
            const uint8_t *ip = frame + ETH_HDR;
            uint8_t ihl = (ip[0] & 0x0F) * 4;
            if (ihl < IP_HDR || ihl > len - ETH_HDR)
                continue;
            uint32_t dst_ip;
            memcpy(&dst_ip, ip + 16, 4);
            if (dst_ip != g_net.ip && dst_ip != 0xFFFFFFFF)
                continue;
            uint16_t ip_total = rd16(ip + 2);
            if (ip_total < ihl || ip_total > len - ETH_HDR)
                continue;
            uint32_t src_ip;
            memcpy(&src_ip, ip + 12, 4);
            const uint8_t *pl = ip + ihl;
            uint16_t plen = (uint16_t)(ip_total - ihl);
            if (ip[9] == 1)
                icmp_process(pl, plen, src_ip);
            else if (ip[9] == 17)
                udp_process(pl, plen);
            else if (ip[9] == 6)
                tcp_process(pl, plen, src_ip);
        }
    }
    g_in_poll = 0;
}

int net_dhcp(void) {
    printf("[NET] Starting DHCP discovery...\n");
    g_dhcp_state = 0;
    uint32_t xid = 0x12345678;

    uint8_t pkt[320];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 1;
    pkt[1] = 1;
    pkt[2] = 6;
    wr32(pkt + 4, xid);
    wr16(pkt + 10, 0x8000);
    memcpy(pkt + 28, g_net.mac, 6);
    wr32(pkt + 236, 0x63825363);
    uint8_t *opt = pkt + 240;
    *opt++ = 53; *opt++ = 1; *opt++ = 1;
    *opt++ = 55; *opt++ = 3; *opt++ = 1; *opt++ = 3; *opt++ = 6;
    *opt++ = 255;
    uint16_t len = (uint16_t)(opt - pkt);
    udp_send(0xFFFFFFFF, 68, 67, pkt, len);

    for (int i = 0; i < 400; i++) {
        net_poll();
        if (g_dhcp_state >= 1)
            break;
        delay_loop();
    }

    if (g_dhcp_state == 0) {
        printf("[NET] DHCP timeout, using default 10.0.2.15\n");
        return -1;
    }

    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 1;
    pkt[1] = 1;
    pkt[2] = 6;
    wr32(pkt + 4, xid);
    wr16(pkt + 10, 0x8000);
    memcpy(pkt + 28, g_net.mac, 6);
    wr32(pkt + 236, 0x63825363);
    opt = pkt + 240;
    *opt++ = 53; *opt++ = 1; *opt++ = 3;
    *opt++ = 50; *opt++ = 4; memcpy(opt, &g_net.ip, 4); opt += 4;
    *opt++ = 54; *opt++ = 4; memcpy(opt, &g_net.gateway, 4); opt += 4;
    *opt++ = 255;
    len = (uint16_t)(opt - pkt);
    udp_send(0xFFFFFFFF, 68, 67, pkt, len);

    for (int i = 0; i < 400; i++) {
        net_poll();
        if (g_dhcp_state >= 2)
            break;
        delay_loop();
    }

    if (g_dhcp_state >= 2) {
        g_net.dhcp_ok = 1;
        char ip_str[16], mask_str[16], gw_str[16], dns_str[16];
        ip_to_str(g_net.ip, ip_str);
        ip_to_str(g_net.netmask, mask_str);
        ip_to_str(g_net.gateway, gw_str);
        ip_to_str(g_net.dns, dns_str);
        printf("[NET] DHCP OK: IP %s, Mask %s, GW %s, DNS %s\n",
               ip_str, mask_str, gw_str, dns_str);
        return 0;
    }
    return -1;
}

uint32_t net_nslookup(const char *domain) {
    if (!domain || !*domain)
        return 0;

    uint32_t parsed = str_to_ip(domain);
    if (parsed != 0 && strcmp(domain, "0.0.0.0") != 0)
        return parsed;

    g_dns_received = 0;
    g_dns_resolved_ip = 0;

    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    wr16(buf + 0, 0x4321);
    wr16(buf + 2, 0x0100);
    wr16(buf + 4, 1);

    uint8_t *q = buf + 12;
    const char *p = domain;
    while (*p) {
        const char *elem = p;
        while (*p && *p != '.')
            p++;
        uint8_t l = (uint8_t)(p - elem);
        if (l == 0 || q - buf > 200)
            return 0;
        *q++ = l;
        memcpy(q, elem, l);
        q += l;
        if (*p == '.')
            p++;
    }
    *q++ = 0;
    wr16(q, 1);
    q += 2;
    wr16(q, 1);
    q += 2;

    uint16_t len = (uint16_t)(q - buf);
    uint32_t dns_server = g_net.dns ? g_net.dns : str_to_ip("10.0.2.3");

    for (int attempt = 0; attempt < 5 && g_dns_resolved_ip == 0; attempt++) {
        udp_send(dns_server, 53535, 53, buf, len);
        for (int i = 0; i < 2000; i++) {
            net_poll();
            if (g_dns_received && g_dns_resolved_ip != 0)
                break;
            delay_loop();
        }
    }

    if (g_dns_resolved_ip != 0) {
        char ip_str[16];
        ip_to_str(g_dns_resolved_ip, ip_str);
        printf("[DNS] %s -> %s\n", domain, ip_str);
        return g_dns_resolved_ip;
    }

    printf("[DNS] Resolution failed: %s\n", domain);
    return 0;
}

int net_ping(const char *target, int count) {
    if (!target || !*target)
        return -1;
    if (count <= 0)
        count = 4;

    uint32_t ip = net_nslookup(target);
    if (ip == 0) {
        printf("PING: unknown host %s\n", target);
        return -1;
    }

    char ip_str[16];
    ip_to_str(ip, ip_str);
    printf("PING %s (%s): 32 data bytes\n", target, ip_str);

    int received = 0;
    for (int seq = 1; seq <= count; seq++) {
        g_ping_received = 0;
        g_ping_seq = 0;

        uint8_t pkt[40];
        memset(pkt, 0, sizeof(pkt));
        pkt[0] = 8;
        wr16(pkt + 4, 0x1234);
        wr16(pkt + 6, (uint16_t)seq);
        memset(pkt + ICMP_HDR, 'A', 32);
        wr16(pkt + 2, net_checksum(pkt, sizeof(pkt)));

        net_send_ip(ip, 1, pkt, sizeof(pkt));

        for (int k = 0; k < 400; k++) {
            net_poll();
            if (g_ping_received)
                break;
            delay_loop();
        }

        if (g_ping_received) {
            received++;
            printf("32 bytes from %s: icmp_seq=%d ttl=64 time<1ms\n", ip_str, seq);
        } else {
            printf("Request timeout for icmp_seq %d\n", seq);
        }
    }

    printf("--- %s ping statistics ---\n", target);
    printf("%d packets transmitted, %d received, %d%% loss\n",
           count, received, udiv((unsigned)(count - received) * 100, (unsigned)count));

    return received > 0 ? 0 : -1;
}

int net_fetch(const char *host, int port, const char *path) {
    if (!host || !*host)
        return -1;
    if (port <= 0)
        port = 80;
    if (!path || !*path)
        path = "/";

    uint32_t ip = net_nslookup(host);
    if (ip == 0) {
        printf("[FETCH] Cannot resolve %s\n", host);
        return -1;
    }

    char ip_str[16];
    ip_to_str(ip, ip_str);
    printf("[FETCH] Connecting to %s (%s:%d)...\n", host, ip_str, port);

    uint16_t src_port = (uint16_t)(40000 + (rand() % 20000));
    uint32_t isn = 0x10002000;

    g_tcp_state = 1;
    g_tcp_server_seq = 0;
    g_tcp_my_seq = isn;

    char rx_buf[4096];
    memset(rx_buf, 0, sizeof(rx_buf));
    g_tcp_rx_buf = rx_buf;
    g_tcp_rx_len = 0;
    g_tcp_rx_max = sizeof(rx_buf) - 1;

    tcp_send(ip, src_port, (uint16_t)port, g_tcp_my_seq, 0, 0x02, NULL, 0);

    for (int attempt = 0; attempt < 5 && g_tcp_state < 2; attempt++) {
        if (attempt > 0)
            tcp_send(ip, src_port, (uint16_t)port, g_tcp_my_seq, 0, 0x02, NULL, 0);
        for (int i = 0; i < 3000 && g_tcp_state < 2; i++) {
            net_poll();
            delay_loop();
        }
    }

    if (g_tcp_state < 2) {
        printf("[FETCH] TCP connect timeout\n");
        g_tcp_rx_buf = NULL;
        return -1;
    }

    g_tcp_my_seq++;
    tcp_send(ip, src_port, (uint16_t)port, g_tcp_my_seq, g_tcp_server_seq, 0x10, NULL, 0);

    char req[512];
    snprintf(req, sizeof(req),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: Kresten/1.0\r\n"
             "Accept: */*\r\n"
             "Connection: close\r\n"
             "\r\n",
             path, host);
    uint32_t get_seq = g_tcp_my_seq;
    uint16_t req_len = (uint16_t)strlen(req);
    tcp_send(ip, src_port, (uint16_t)port, get_seq, g_tcp_server_seq, 0x18, req, req_len);
    g_tcp_my_seq += req_len;

    int idle = 0;
    int resent = 0;
    for (int i = 0; i < 12000; i++) {
        int prev_len = g_tcp_rx_len;
        int prev_state = g_tcp_state;
        net_poll();
        if (g_tcp_state == 3)
            break;
        if (g_tcp_rx_len > prev_len || g_tcp_state != prev_state)
            idle = 0;
        else
            idle++;
        if (i > 1000 && idle >= 500 && !resent) {
            resent = 1;
            tcp_send(ip, src_port, (uint16_t)port, get_seq, g_tcp_server_seq, 0x18, req, req_len);
        }
        delay_loop();
    }

    if (g_tcp_state == 2)
        tcp_send(ip, src_port, (uint16_t)port, g_tcp_my_seq, g_tcp_server_seq, 0x11, NULL, 0);

    g_tcp_rx_buf = NULL;

    printf("\n================ HTTP Response ================\n");
    if (g_tcp_rx_len > 0)
        printf("%s\n", rx_buf);
    else
        printf("(no data received)\n");
    printf("===============================================\n");

    return 0;
}

NetConfig *net_get_config(void) {
    return &g_net;
}

void net_ifconfig(void) {
    char ip_str[16], mask_str[16], gw_str[16], dns_str[16];
    ip_to_str(g_net.ip, ip_str);
    ip_to_str(g_net.netmask, mask_str);
    ip_to_str(g_net.gateway, gw_str);
    ip_to_str(g_net.dns, dns_str);

    printf("eth0: SMC91C111 Ethernet\n");
    printf("  MAC:  %02X:%02X:%02X:%02X:%02X:%02X\n",
           g_net.mac[0], g_net.mac[1], g_net.mac[2],
           g_net.mac[3], g_net.mac[4], g_net.mac[5]);
    printf("  IPv4: %s\n", ip_str);
    printf("  Mask: %s\n", mask_str);
    printf("  GW:   %s\n", gw_str);
    printf("  DNS:  %s\n", dns_str);
    printf("  DHCP: %s\n", g_net.dhcp_ok ? "Assigned" : "Static");
    printf("  RX:   %u  TX: %u\n", g_net.rx_packets, g_net.tx_packets);
}

int net_init(void) {
    memset(&g_net, 0, sizeof(g_net));
    memset(g_arp_cache, 0, sizeof(g_arp_cache));

    if (smc_init() != 0) {
        LOG_INFO("NET", "SMC91C111 not detected");
        return -1;
    }

    g_net.ip = str_to_ip("10.0.2.15");
    g_net.netmask = str_to_ip("255.255.255.0");
    g_net.gateway = str_to_ip("10.0.2.2");
    g_net.dns = str_to_ip("10.0.2.3");

    LOG_INFO("NET", "SMC91C111 NIC ready, MAC %02X:%02X:%02X:%02X:%02X:%02X",
             g_net.mac[0], g_net.mac[1], g_net.mac[2],
             g_net.mac[3], g_net.mac[4], g_net.mac[5]);

    net_dhcp();

    return 0;
}

