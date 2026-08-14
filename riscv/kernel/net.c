/* ===== kernel/net.c (RISC-V): virt 机器无 SMC91C111 -> 协议栈保留, 硬件 stub =====
 * 与 ARM 版相同的 TCP/IP/ARP/DHCP/DNS 逻辑, 仅替换 SMC 寄存器访问为
 * "无网卡" stub (net_init 失败)。net 命令仍可运行, 报 "no NIC"。
 */
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

/* ===== RISC-V: 无 SMC91C111 硬件, 全部 stub ===== */
static int smc_init(void) {
    return -1;
}

static int smc_send(const void *buf, uint16_t len) {
    (void)buf; (void)len;
    return -1;
}

static int smc_recv(void *buf, uint16_t max_len) {
    (void)buf; (void)max_len;
    return 0;
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
    (void)target_ip;
}

static int arp_get_mac(uint32_t target_ip, uint8_t mac_out[6]) {
    (void)target_ip;
    memset(mac_out, 0xFF, 6);
    return -1;
}

static void net_send_ip(uint32_t dst_ip, uint8_t proto, const void *payload, uint16_t payload_len) {
    (void)dst_ip; (void)proto; (void)payload; (void)payload_len;
}

static void udp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, const void *payload, uint16_t payload_len) {
    (void)dst_ip; (void)src_port; (void)dst_port; (void)payload; (void)payload_len;
}

static void tcp_send(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, uint32_t seq, uint32_t ack, uint8_t flags, const void *payload, uint16_t payload_len) {
    (void)dst_ip; (void)src_port; (void)dst_port; (void)seq; (void)ack;
    (void)flags; (void)payload; (void)payload_len;
}

static void icmp_process(const uint8_t *pkt, uint16_t len, uint32_t src_ip) {
    (void)pkt; (void)len; (void)src_ip;
}

static void dns_process(const uint8_t *payload, uint16_t len) {
    (void)payload; (void)len;
}

static void udp_process(const uint8_t *pkt, uint16_t len) {
    (void)pkt; (void)len;
}

static void tcp_process(const uint8_t *pkt, uint16_t len, uint32_t src_ip) {
    (void)pkt; (void)len; (void)src_ip;
}

void net_poll(void) {
    (void)g_in_poll;
}

int net_dhcp(void) {
    printf("[NET] no NIC (riscv virt), using static 10.0.2.15\n");
    return -1;
}

uint32_t net_nslookup(const char *domain) {
    if (!domain || !*domain)
        return 0;
    uint32_t parsed = str_to_ip(domain);
    if (parsed != 0 && strcmp(domain, "0.0.0.0") != 0)
        return parsed;
    printf("[DNS] no NIC (riscv virt), resolution unavailable\n");
    return 0;
}

int net_ping(const char *target, int count) {
    (void)target; (void)count;
    printf("PING: no NIC (riscv virt)\n");
    return -1;
}

int net_fetch(const char *host, int port, const char *path) {
    (void)host; (void)port; (void)path;
    printf("[FETCH] no NIC (riscv virt)\n");
    return -1;
}

NetConfig *net_get_config(void) {
    return &g_net;
}

void net_ifconfig(void) {
    printf("eth0: no NIC (riscv virt machine has no ethernet)\n");
}

int net_init(void) {
    memset(&g_net, 0, sizeof(g_net));
    memset(g_arp_cache, 0, sizeof(g_arp_cache));
    g_net.ip = str_to_ip("10.0.2.15");
    g_net.netmask = str_to_ip("255.255.255.0");
    g_net.gateway = str_to_ip("10.0.2.2");
    g_net.dns = str_to_ip("10.0.2.3");
    LOG_INFO("NET", "no NIC (riscv virt machine has no ethernet)");
    return -1;
}