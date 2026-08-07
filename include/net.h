#ifndef _NET_H
#define _NET_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t mac[6];
    uint32_t ip;         /* Network byte order */
    uint32_t netmask;    /* Network byte order */
    uint32_t gateway;    /* Network byte order */
    uint32_t dns;        /* Network byte order */
    int dhcp_ok;
    uint32_t rx_packets;
    uint32_t tx_packets;
} NetConfig;

int net_init(void);
void net_poll(void);
void net_ifconfig(void);
int net_ping(const char *target, int count);
uint32_t net_nslookup(const char *domain);
int net_fetch(const char *host, int port, const char *path);
int net_dhcp(void);
NetConfig *net_get_config(void);

static inline uint16_t htons(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}
static inline uint16_t ntohs(uint16_t v) {
    return htons(v);
}
static inline uint32_t htonl(uint32_t v) {
    return ((v & 0x000000FF) << 24) |
           ((v & 0x0000FF00) << 8)  |
           ((v & 0x00FF0000) >> 8)  |
           ((v & 0xFF000000) >> 24);
}
static inline uint32_t ntohl(uint32_t v) {
    return htonl(v);
}

void ip_to_str(uint32_t ip, char *buf);
uint32_t str_to_ip(const char *str);

#endif
