/* ===== x86/kernel/net.c: network stack core =====
 * Static SLIRP configuration (QEMU user networking):
 *   IP 10.0.2.15/24, gateway 10.0.2.2, DNS 10.0.2.3.
 * A background cooperative thread polls the NIC and dispatches frames.
 */
#include <net.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int rtl8139_init(void);
int rtl8139_rx(uint8_t *buf, uint16_t maxlen);
const uint8_t *rtl8139_get_mac(void);
uint32_t rtl8139_rx_count(void);
uint32_t rtl8139_tx_count(void);

void eth_rx(const uint8_t *frame, uint16_t len);
int icmp_ping(uint32_t ip, int count);

static NetConfig nc;
static int net_ready = 0;

NetConfig *net_get_config(void) {
    return &nc;
}

uint32_t net_get_local_ip(void) {
    return nc.ip;
}

int net_init(void) {
    memset(&nc, 0, sizeof(nc));
    if (rtl8139_init() != 0) {
        puts("[NET] RTL8139 not found, networking disabled\n");
        return -1;
    }
    memcpy(nc.mac, rtl8139_get_mac(), 6);
    nc.ip = str_to_ip("10.0.2.15");
    nc.netmask = str_to_ip("255.255.255.0");
    nc.gateway = str_to_ip("10.0.2.2");
    nc.dns = str_to_ip("10.0.2.3");
    nc.dhcp_ok = 0;
    nc.rx_packets = 0;
    nc.tx_packets = 0;
    net_ready = 1;
    printf("[NET] up: %u.%u.%u.%u/24 gw %u.%u.%u.%u dns %u.%u.%u.%u\n",           (nc.ip >> 24) & 0xFF, (nc.ip >> 16) & 0xFF,
           (nc.ip >> 8) & 0xFF, nc.ip & 0xFF,
           (nc.gateway >> 24) & 0xFF, (nc.gateway >> 16) & 0xFF,
           (nc.gateway >> 8) & 0xFF, nc.gateway & 0xFF,
           (nc.dns >> 24) & 0xFF, (nc.dns >> 16) & 0xFF,
           (nc.dns >> 8) & 0xFF, nc.dns & 0xFF);
    return 0;
}

void net_poll(void) {
    static uint8_t buf[1600];
    if (!net_ready)
        return;
    int len;
    while ((len = rtl8139_rx(buf, (uint16_t)sizeof(buf))) > 0)
        eth_rx(buf, (uint16_t)len);
    nc.rx_packets = rtl8139_rx_count();
    nc.tx_packets = rtl8139_tx_count();
}

void net_ifconfig(void) {
    if (!net_ready) {
        puts("Network down\n");
        return;
    }
    printf("  NIC      RTL8139\n");
    printf("  MAC      %02x:%02x:%02x:%02x:%02x:%02x\n",
           nc.mac[0], nc.mac[1], nc.mac[2], nc.mac[3], nc.mac[4], nc.mac[5]);
    printf("  IP       %u.%u.%u.%u\n",
           (nc.ip >> 24) & 0xFF, (nc.ip >> 16) & 0xFF,
           (nc.ip >> 8) & 0xFF, nc.ip & 0xFF);
    printf("  Netmask  %u.%u.%u.%u\n",
           (nc.netmask >> 24) & 0xFF, (nc.netmask >> 16) & 0xFF,
           (nc.netmask >> 8) & 0xFF, nc.netmask & 0xFF);
    printf("  Gateway  %u.%u.%u.%u\n",
           (nc.gateway >> 24) & 0xFF, (nc.gateway >> 16) & 0xFF,
           (nc.gateway >> 8) & 0xFF, nc.gateway & 0xFF);
    printf("  DNS      %u.%u.%u.%u\n",
           (nc.dns >> 24) & 0xFF, (nc.dns >> 16) & 0xFF,
           (nc.dns >> 8) & 0xFF, nc.dns & 0xFF);
    printf("  Packets  rx=%u tx=%u\n", (unsigned)nc.rx_packets,
           (unsigned)nc.tx_packets);
}

/* DHCP: SLIRP serves a fixed subnet; static config above is equivalent. */
int net_dhcp(void) {
    if (net_ready)
        printf("Static SLIRP config in use (10.0.2.15); DHCP skipped\n");
    return net_ready ? 0 : -1;
}

int net_ping(const char *target, int count) {
    if (!net_ready) {
        puts("Network down\n");
        return -1;
    }
    uint32_t ip = str_to_ip(target);
    if (ip == 0 && strcmp(target, "0.0.0.0") != 0) {
        printf("ping: cannot resolve '%s' (use an IP)\n", target);
        return -1;
    }
    printf("Pinging %u.%u.%u.%u\n",
           (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
           (ip >> 8) & 0xFF, ip & 0xFF);
    return icmp_ping(ip, count);
}

static void net_thread(void) {
    for (;;) {
        net_poll();
        proc_yield();
    }
}

void net_thread_start(void) {
    proc_create("net", (uint32_t)net_thread);
}

void ip_to_str(uint32_t ip, char *buf) {
    unsigned o[4];
    o[0] = (ip >> 24) & 0xFF;
    o[1] = (ip >> 16) & 0xFF;
    o[2] = (ip >> 8) & 0xFF;
    o[3] = ip & 0xFF;
    char *p = buf;
    for (int i = 0; i < 4; i++) {
        if (i)
            *p++ = '.';
        if (o[i] >= 100)
            *p++ = (char)('0' + o[i] / 100);
        if (o[i] >= 10)
            *p++ = (char)('0' + (o[i] / 10) % 10);
        *p++ = (char)('0' + o[i] % 10);
    }
    *p = '\0';
}

uint32_t str_to_ip(const char *str) {
    uint32_t ip = 0;
    int shift = 24;
    const char *p = str;
    for (int i = 0; i < 4; i++) {
        unsigned v = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (unsigned)(*p - '0');
            p++;
        }
        if (v > 255)
            return 0;
        ip |= v << shift;
        shift -= 8;
        if (i < 3) {
            if (*p != '.')
                return 0;
            p++;
        }
    }
    return ip;
}
