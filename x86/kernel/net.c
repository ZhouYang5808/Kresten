/* ===== x86/kernel/net.c: network stack core =====
 * Static SLIRP configuration (QEMU user networking):
 *   IP 10.0.2.15/24, gateway 10.0.2.2, DNS 10.0.2.3.
 * A background cooperative thread polls the NIC and dispatches frames.
 */
#include <net.h>
#include <process.h>
#include <fs.h>
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
extern uint32_t sched_get_ticks(void);
int tcp_connect(uint32_t ip, uint16_t port);
int tcp_send(const void *data, uint16_t len);
void tcp_close(void);
int tcp_rxlen(void);
const uint8_t *tcp_rxdata(void);
void tcp_reset_rx(void);
int tcp_connected(void);

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
    net_ready = 1;
    printf("[NET] NIC ready (RTL8139 %02x:%02x:%02x:%02x:%02x:%02x)\n",
           nc.mac[0], nc.mac[1], nc.mac[2],
           nc.mac[3], nc.mac[4], nc.mac[5]);
    return 0;
}

void net_poll(void) {
    static uint8_t buf[1600];
    if (!net_ready)
        return;
    /* The net thread and the boot/main thread can both reach here
     * (DHCP/ping poll loops): keep RX-ring advancement atomic. */
    __asm__ volatile ("cli");
    dhcp_tick();
    int len;
    while ((len = rtl8139_rx(buf, (uint16_t)sizeof(buf))) > 0)
        eth_rx(buf, (uint16_t)len);
    nc.rx_packets = rtl8139_rx_count();
    nc.tx_packets = rtl8139_tx_count();
    __asm__ volatile ("sti");
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
    printf("  DHCP     %s\n", nc.dhcp_ok ? "bound" : "static");
    printf("  Packets  rx=%u tx=%u\n", (unsigned)nc.rx_packets,
           (unsigned)nc.tx_packets);
}

/* Apply a DHCP lease to the live configuration. */
void dhcp_apply_lease(uint32_t ip, uint32_t mask, uint32_t gw,
                      uint32_t dns, uint32_t lease) {
    nc.ip = ip;
    if (mask)
        nc.netmask = mask;
    if (gw)
        nc.gateway = gw;
    if (dns)
        nc.dns = dns;
    nc.dhcp_ok = 1;
    printf("[DHCP] lease ip %u.%u.%u.%u mask %u.%u.%u.%u gw %u.%u.%u.%u "
           "dns %u.%u.%u.%u lease %u s\n",
           (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF,
           (nc.netmask >> 24) & 0xFF, (nc.netmask >> 16) & 0xFF,
           (nc.netmask >> 8) & 0xFF, nc.netmask & 0xFF,
           (nc.gateway >> 24) & 0xFF, (nc.gateway >> 16) & 0xFF,
           (nc.gateway >> 8) & 0xFF, nc.gateway & 0xFF,
           (nc.dns >> 24) & 0xFF, (nc.dns >> 16) & 0xFF,
           (nc.dns >> 8) & 0xFF, nc.dns & 0xFF,
           (unsigned)lease);
}

/* DHCP client: DORA acquisition, static SLIRP fallback on failure. */
int net_dhcp(void) {
    if (!net_ready)
        return -1;
    DhcpLease l;
    puts("[NET] DHCP: discovering ...\n");
    if (dhcp_run(&l) == 0) {
        dhcp_apply_lease(l.ip, l.mask, l.gw, l.dns, l.lease);
        return 0;
    }
    nc.ip = str_to_ip("10.0.2.15");
    nc.netmask = str_to_ip("255.255.255.0");
    nc.gateway = str_to_ip("10.0.2.2");
    nc.dns = str_to_ip("10.0.2.3");
    nc.dhcp_ok = 0;
    puts("[NET] DHCP failed, using static fallback 10.0.2.15/24\n");
    return -1;
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

/* HTTP GET over TCP: prints response, saves body to fetch.txt */
int net_fetch(const char *host, int port, const char *path) {
    if (!net_ready) {
        puts("Network down\n");
        return -1;
    }
    uint32_t ip = str_to_ip(host);
    if (ip == 0) {
        ip = net_nslookup(host);
        if (ip == 0) {
            printf("fetch: cannot resolve '%s'\n", host);
            return -1;
        }
        printf("  resolved %s -> %u.%u.%u.%u\n", host,
               (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
               (ip >> 8) & 0xFF, ip & 0xFF);
    }
    printf("  connecting to %u.%u.%u.%u:%d ...\n",
           (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
           (ip >> 8) & 0xFF, ip & 0xFF, port);
    if (tcp_connect(ip, (uint16_t)port) != 0) {
        puts("  connect: failed\n");
        return -1;
    }
    tcp_reset_rx();
    char req[512];
    int n = 0;
    const char *parts[6];
    int plen[6];
    parts[0] = "GET ";
    plen[0] = 4;
    parts[1] = path;
    plen[1] = (int)strlen(path);
    parts[2] = " HTTP/1.0\r\nHost: ";
    plen[2] = 15;
    parts[3] = host;
    plen[3] = (int)strlen(host);
    parts[4] = ":\0:";
    plen[4] = 1;
    {
        char portbuf[8];
        int pn = 0;
        int v = port;
        char tmp[8];
        if (v == 0)
            tmp[pn++] = '0';
        while (v > 0) {
            tmp[pn++] = (char)('0' + v % 10);
            v /= 10;
        }
        while (pn > 0)
            portbuf[plen[4]++] = tmp[--pn];
        portbuf[plen[4]] = '\0';
        parts[4] = portbuf;
    }
    parts[5] = "\r\nConnection: close\r\n\r\n";
    plen[5] = 24;
    for (int i = 0; i < 6 && n < 500; i++) {
        for (int j = 0; j < plen[i] && n < 500; j++)
            req[n++] = parts[i][j];
    }
    req[n] = '\0';
    if (tcp_send(req, (uint16_t)n) != 0) {
        tcp_close();
        puts("  send: failed\n");
        return -1;
    }
    puts("  waiting for response ...\n");
    uint32_t deadline = sched_get_ticks() + 200;
    while ((int32_t)(sched_get_ticks() - deadline) < 0 &&
           !(tcp_rxlen() > 0 && !tcp_connected()))
        net_poll();
    int rxlen = tcp_rxlen();
    const uint8_t *rxdata = tcp_rxdata();
    if (rxlen <= 0) {
        tcp_close();
        puts("  no response\n");
        return -1;
    }
    printf("  got %d bytes\n\n", rxlen);
    for (int i = 0; i < rxlen && i < 1024; i++)
        putchar(rxdata[i]);
    if (rxlen > 1024)
        printf("\n  ... (%d more bytes)\n", rxlen - 1024);
    putchar('\n');
    if (fs_update_file("\\fetch.txt", (const char *)rxdata) != 0 &&
        fs_create_file("\\fetch.txt", (const char *)rxdata) != 0)
        puts("  fetch: could not save \\fetch.txt\n");
    tcp_close();
    return 0;
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
