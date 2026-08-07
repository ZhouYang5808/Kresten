/* ===== x86/kernel/net_arp.c: ARP cache, requests and replies ===== */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <net.h>

extern uint32_t sched_get_ticks(void);
void net_poll(void);
int eth_send(const uint8_t *dst, uint16_t ethertype,
             const void *payload, uint16_t len);
const uint8_t *rtl8139_get_mac(void);
uint32_t rtl8139_rx_count(void);
uint32_t rtl8139_tx_count(void);

uint32_t net_get_local_ip(void);

typedef struct {
    uint32_t ip;          /* network byte order */
    uint8_t mac[6];
    int valid;
} ArpEntry;

static ArpEntry arp_table[8];

static const uint8_t eth_bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static ArpEntry *arp_find(uint32_t ip) {
    for (int i = 0; i < 8; i++)
        if (arp_table[i].valid && arp_table[i].ip == ip)
            return &arp_table[i];
    return 0;
}

static void arp_add(uint32_t ip, const uint8_t *mac) {
    ArpEntry *e = arp_find(ip);
    if (!e) {
        for (int i = 0; i < 8; i++) {
            if (!arp_table[i].valid) {
                e = &arp_table[i];
                break;
            }
        }
        if (!e) {
            static int next = 0;
            e = &arp_table[next];
            next = (next + 1) & 7;
        }
    }
    e->ip = ip;
    memcpy(e->mac, mac, 6);
    e->valid = 1;
}

void arp_rx(const uint8_t *frame, uint16_t len) {
    if (len < 42)
        return;
    const uint8_t *a = frame + 14; /* ARP payload */
    if (a[0] != 0 || a[1] != 1)    /* Ethernet */
        return;
    if (a[2] != 0x08 || a[3] != 0x00) /* IPv4 */
        return;
    uint16_t oper = (uint16_t)((a[6] << 8) | a[7]);
    const uint8_t *sha = a + 8;
    uint32_t spa = (uint32_t)((a[14] << 24) | (a[15] << 16) |
                              (a[16] << 8) | a[17]);
    uint32_t tpa = (uint32_t)((a[24] << 24) | (a[25] << 16) |
                              (a[26] << 8) | a[27]);
    if (spa != 0)
        arp_add(spa, sha);

    if (oper == 1 && tpa == net_get_local_ip()) {
        /* reply: who-has -> is-at */
        static uint8_t reply[28];
        memset(reply, 0, sizeof(reply));
        reply[0] = 0; reply[1] = 1;      /* htype */
        reply[2] = 0x08; reply[3] = 0x00; /* ptype */
        reply[4] = 6; reply[5] = 4;      /* hlen, plen */
        reply[6] = 0; reply[7] = 2;      /* oper: reply */
        memcpy(reply + 8, rtl8139_get_mac(), 6);
        uint32_t tpa_n = htonl(tpa);
        uint32_t spa_n = htonl(spa);
        memcpy(reply + 14, &tpa_n, 4);
        memcpy(reply + 18, sha, 6);
        memcpy(reply + 24, &spa_n, 4);
        eth_send(sha, 0x0806, reply, sizeof(reply));
    }
}

/* Resolve an IP to a MAC, sending requests and polling until answered. */
int arp_resolve(uint32_t ip, uint8_t *mac) {
    ArpEntry *e = arp_find(ip);
    if (e) {
        memcpy(mac, e->mac, 6);
        return 0;
    }
    for (int attempt = 0; attempt < 3; attempt++) {
        static uint8_t req[28];
        memset(req, 0, sizeof(req));
        req[0] = 0; req[1] = 1;
        req[2] = 0x08; req[3] = 0x00;
        req[4] = 6; req[5] = 4;
        req[6] = 0; req[7] = 1; /* request */
        memcpy(req + 8, rtl8139_get_mac(), 6);
        uint32_t lip = net_get_local_ip();
        uint32_t lip_n = htonl(lip);
        uint32_t ip_n = htonl(ip);
        memcpy(req + 14, &lip_n, 4);
        memset(req + 18, 0, 6);
        memcpy(req + 24, &ip_n, 4);
        eth_send(eth_bcast, 0x0806, req, sizeof(req));

        uint32_t deadline = sched_get_ticks() + 20;
        while ((int32_t)(sched_get_ticks() - deadline) < 0) {
            net_poll();
            e = arp_find(ip);
            if (e) {
                memcpy(mac, e->mac, 6);
                return 0;
            }
        }
    }
    printf("[ARP] resolve %u.%u.%u.%u failed (rx=%u tx=%u)\n",
           (unsigned)((ip >> 24) & 0xFF), (unsigned)((ip >> 16) & 0xFF),
           (unsigned)((ip >> 8) & 0xFF), (unsigned)(ip & 0xFF),
           (unsigned)rtl8139_rx_count(), (unsigned)rtl8139_tx_count());
    return -1;
}
