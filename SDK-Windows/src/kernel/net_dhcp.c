/* ===== x86/kernel/net_dhcp.c: DHCP client (RFC 2131) =====
 * Full client state machine: DISCOVER -> OFFER -> REQUEST -> ACK,
 * lease parsing, T1 (50%) automatic renewal, NAK restart, retries.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <net.h>

extern uint32_t sched_get_ticks(void);
void net_poll(void);
void dhcp_apply_lease(uint32_t ip, uint32_t mask, uint32_t gw,
                      uint32_t dns, uint32_t lease);

#define DHCP_OP_BOOTREPLY 2
#define DHCP_MSG_DISCOVER 1
#define DHCP_MSG_OFFER    2
#define DHCP_MSG_REQUEST  3
#define DHCP_MSG_DECLINE  4
#define DHCP_MSG_ACK      5
#define DHCP_MSG_NAK      6
#define DHCP_MSG_RELEASE  7

#define OPT_PAD         0
#define OPT_NETMASK     1
#define OPT_ROUTER      3
#define OPT_DNS         6
#define OPT_HOSTNAME    12
#define OPT_REQUESTED   50
#define OPT_LEASE       51
#define OPT_MSG_TYPE    53
#define OPT_SERVERID    54
#define OPT_PARAMLIST   55
#define OPT_END         255

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define BC_ADDR          0xFFFFFFFFu

#define DHCP_IDLE       0
#define DHCP_DISCOVER   1
#define DHCP_REQUEST    2
#define DHCP_BOUND      3
#define DHCP_RENEW      4

#define TICKS_PER_SEC   100
#define TRIES_MAX       3
#define WAIT_TICKS      20

static struct {
    int state;
    uint32_t xid;
    uint32_t server_id;
    uint32_t offered_ip;
    uint32_t lease;      /* seconds */
    uint32_t renew_at;   /* ticks */
    uint32_t deadline;   /* ticks */
    int tries;
    uint32_t result_ip;
    uint32_t result_mask;
    uint32_t result_gw;
    uint32_t result_dns;
    uint32_t result_lease;
    int result_ok;
    int bound_changed;
} dhc;

static void dhc_reset(void) {
    memset(&dhc, 0, sizeof(dhc));
    dhc.state = DHCP_IDLE;
}

/* Build a BOOTP/DHCP packet; returns length. */
static uint16_t dhcp_build(uint8_t msgtype, uint32_t ciaddr, uint32_t req_ip,
                           uint32_t server_id, int with_req, uint8_t *pkt) {
    memset(pkt, 0, 240);
    pkt[0] = 1;                    /* BOOTREQUEST */
    pkt[1] = 1;                    /* htype: ethernet */
    pkt[2] = 6;                    /* hlen */
    pkt[3] = 0;                    /* hops */
    pkt[4] = (uint8_t)(dhc.xid >> 24);
    pkt[5] = (uint8_t)(dhc.xid >> 16);
    pkt[6] = (uint8_t)(dhc.xid >> 8);
    pkt[7] = (uint8_t)(dhc.xid & 0xFF);
    pkt[10] = 0x80;                /* flags: broadcast */
    pkt[11] = 0x00;
    uint32_t c = htonl(ciaddr);
    memcpy(pkt + 12, &c, 4);
    const uint8_t *mac = net_get_config()->mac;
    memcpy(pkt + 28, mac, 6);
    pkt[236] = 0x63;               /* magic cookie */
    pkt[237] = 0x82;
    pkt[238] = 0x53;
    pkt[239] = 0x63;

    uint8_t *o = pkt + 240;
    *o++ = OPT_HOSTNAME;           /* option 12: hostname */
    *o++ = 7;
    memcpy(o, "kresten", 7);
    o += 7;
    *o++ = OPT_PARAMLIST;          /* option 55: parameter list */
    *o++ = 4;
    *o++ = OPT_NETMASK;
    *o++ = OPT_ROUTER;
    *o++ = OPT_DNS;
    *o++ = OPT_LEASE;
    if (with_req) {
        *o++ = OPT_REQUESTED;      /* option 50: requested IP */
        *o++ = 4;
        uint32_t r = htonl(req_ip);
        memcpy(o, &r, 4);
        o += 4;
        *o++ = OPT_SERVERID;       /* option 54: server identifier */
        *o++ = 4;
        uint32_t s = htonl(server_id);
        memcpy(o, &s, 4);
        o += 4;
    }
    *o++ = OPT_MSG_TYPE;           /* option 53: message type */
    *o++ = 1;
    *o++ = msgtype;
    *o++ = OPT_END;
    return (uint16_t)(o - pkt);
}

static int dhcp_send_msg(uint8_t msgtype, uint32_t ciaddr, uint32_t req_ip,
                         uint32_t server_id, int with_req, uint32_t dst) {
    static uint8_t pkt[300];
    uint16_t len = dhcp_build(msgtype, ciaddr, req_ip, server_id, with_req, pkt);
    return udp_send(dst, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, pkt, len);
}

/* Parse options, calling the callback for each (code, data, len). */
static void dhcp_parse_opts(const uint8_t *p, uint16_t len) {
    uint16_t off = 240;
    dhc.result_lease = 0;
    while (off + 2 <= len) {
        uint8_t code = p[off];
        if (code == OPT_END)
            break;
        if (code == OPT_PAD) {
            off++;
            continue;
        }
        if (off + 2 > len)
            break;
        uint8_t olen = p[off + 1];
        if (off + 2 + olen > len)
            break;
        const uint8_t *d = p + off + 2;
        if (code == OPT_NETMASK && olen >= 4) {
            dhc.result_mask = (uint32_t)((d[0] << 24) | (d[1] << 16) |
                                         (d[2] << 8) | d[3]);
        } else if (code == OPT_ROUTER && olen >= 4) {
            if (dhc.result_gw == 0)
                dhc.result_gw = (uint32_t)((d[0] << 24) | (d[1] << 16) |
                                           (d[2] << 8) | d[3]);
        } else if (code == OPT_DNS && olen >= 4) {
            if (dhc.result_dns == 0)
                dhc.result_dns = (uint32_t)((d[0] << 24) | (d[1] << 16) |
                                            (d[2] << 8) | d[3]);
        } else if (code == OPT_LEASE && olen >= 4) {
            dhc.result_lease = (uint32_t)((d[0] << 24) | (d[1] << 16) |
                                          (d[2] << 8) | d[3]);
        } else if (code == OPT_SERVERID && olen >= 4) {
            dhc.server_id = (uint32_t)((d[0] << 24) | (d[1] << 16) |
                                       (d[2] << 8) | d[3]);
        }
        off = (uint16_t)(off + 2 + olen);
    }
}

void dhcp_rx(const uint8_t *p, uint16_t len, uint32_t src) {
    (void)src;
    if (dhc.state != DHCP_DISCOVER && dhc.state != DHCP_REQUEST &&
        dhc.state != DHCP_RENEW)
        return;
    if (len < 244)
        return;
    uint32_t xid = (uint32_t)((p[4] << 24) | (p[5] << 16) |
                              (p[6] << 8) | p[7]);
    if (xid != dhc.xid)
        return;
    if (p[0] != DHCP_OP_BOOTREPLY)
        return;
    if (p[236] != 0x63 || p[237] != 0x82 || p[238] != 0x53 || p[239] != 0x63)
        return;
    dhc.result_ip = (uint32_t)((p[16] << 24) | (p[17] << 16) |
                               (p[18] << 8) | p[19]);
    dhc.result_mask = 0;
    dhc.result_gw = 0;
    dhc.result_dns = 0;
    dhc.result_lease = 0;
    dhc.result_ok = 0;

    uint8_t msgtype = 0;
    uint16_t off = 240;
    while (off + 2 <= len) {
        uint8_t code = p[off];
        if (code == OPT_END)
            break;
        if (code == OPT_PAD) {
            off++;
            continue;
        }
        if (off + 2 > len)
            break;
        uint8_t olen = p[off + 1];
        if (off + 2 + olen > len)
            break;
        const uint8_t *d = p + off + 2;
        if (code == OPT_MSG_TYPE && olen >= 1)
            msgtype = d[0];
        off = (uint16_t)(off + 2 + olen);
    }

    if (dhc.state == DHCP_DISCOVER && msgtype == DHCP_MSG_OFFER) {
        dhcp_parse_opts(p, len);
        dhc.offered_ip = dhc.result_ip;
        dhc.state = DHCP_REQUEST;
        dhc.tries = 0;
        dhcp_send_msg(DHCP_MSG_REQUEST, 0, dhc.offered_ip,
                      dhc.server_id, 1, BC_ADDR);
        dhc.deadline = sched_get_ticks() + WAIT_TICKS;
        return;
    }
    if (dhc.state == DHCP_REQUEST && msgtype == DHCP_MSG_ACK) {
        dhcp_parse_opts(p, len);
        if (dhc.result_lease == 0)
            dhc.result_lease = 3600;
        dhc.state = DHCP_BOUND;
        dhc.lease = dhc.result_lease;
        dhc.renew_at = sched_get_ticks() +
                       (dhc.lease / 2) * TICKS_PER_SEC;
        dhc.result_ok = 1;
        dhc.bound_changed = 1;
        return;
    }
    if (dhc.state == DHCP_REQUEST && msgtype == DHCP_MSG_NAK) {
        /* NAK: restart the whole process with a new xid */
        dhc.xid = (uint32_t)(sched_get_ticks() * 2654435761u ^ 0x9E3779B9u);
        dhc.state = DHCP_DISCOVER;
        dhc.tries = 0;
        dhcp_send_msg(DHCP_MSG_DISCOVER, 0, 0, 0, 0, BC_ADDR);
        dhc.deadline = sched_get_ticks() + WAIT_TICKS;
        return;
    }
    if (dhc.state == DHCP_RENEW && msgtype == DHCP_MSG_ACK) {
        dhcp_parse_opts(p, len);
        if (dhc.result_lease == 0)
            dhc.result_lease = dhc.lease;
        dhc.lease = dhc.result_lease;
        dhc.renew_at = sched_get_ticks() +
                       (dhc.lease / 2) * TICKS_PER_SEC;
        dhc.result_ok = 1;
        dhc.bound_changed = 0;
        dhc.state = DHCP_BOUND;
        return;
    }
}

static int dhcp_wait(int *ok, int advance_state) {
    dhc.tries++;
    while ((int32_t)(sched_get_ticks() - dhc.deadline) < 0) {
        net_poll();
        if (*ok)
            return 0;
        if (advance_state && dhc.state != advance_state)
            return 0; /* OFFER moved us to REQUEST (or NAK restarted) */
    }
    return -1;
}

/* Blocking acquisition: DISCOVER -> OFFER -> REQUEST -> ACK. */
int dhcp_run(DhcpLease *out) {
    static int bound_cb = 0;
    if (!bound_cb) {
        udp_bind(DHCP_CLIENT_PORT, dhcp_rx);
        bound_cb = 1;
    }
    dhc_reset();
    dhc.xid = (uint32_t)((sched_get_ticks() ^ 0x12345678) |
                         ((uint32_t)net_get_config()->mac[4] << 24) |
                         ((uint32_t)net_get_config()->mac[5] << 16));

    /* DISCOVER phase: wait until an OFFER moves us forward */
    for (int attempt = 0; attempt < TRIES_MAX; attempt++) {
        dhc.state = DHCP_DISCOVER;
        dhc.tries = 0;
        dhc.result_ok = 0;
        dhc.deadline = sched_get_ticks() + WAIT_TICKS;
        dhcp_send_msg(DHCP_MSG_DISCOVER, 0, 0, 0, 0, BC_ADDR);
        if (dhcp_wait(&dhc.result_ok, DHCP_DISCOVER) == 0 &&
            dhc.state != DHCP_DISCOVER)
            break;
    }
    /* REQUEST phase: skip it if OFFER+ACK already landed in one poll */
    if (dhc.state != DHCP_REQUEST && dhc.state != DHCP_BOUND) {
        puts("[DHCP] no OFFER, giving up\n");
        return -1;
    }
    for (int attempt = 0; attempt < TRIES_MAX; attempt++) {
        if (dhc.state == DHCP_BOUND)
            break;
        dhc.tries = 0;
        dhc.result_ok = 0;
        dhc.deadline = sched_get_ticks() + WAIT_TICKS;
        dhcp_send_msg(DHCP_MSG_REQUEST, 0, dhc.offered_ip,
                      dhc.server_id, 1, BC_ADDR);
        if (dhcp_wait(&dhc.result_ok, DHCP_REQUEST) == 0)
            break;
        if (dhc.state != DHCP_REQUEST) {
            /* NAK restarted us: wait for the new OFFER, then REQUEST */
            dhc.deadline = sched_get_ticks() + WAIT_TICKS;
            if (dhcp_wait(&dhc.result_ok, DHCP_DISCOVER) == 0 &&
                dhc.state != DHCP_DISCOVER)
                break;
        }
    }
    if (!dhc.result_ok) {
        puts("[DHCP] no ACK, giving up\n");
        return -1;
    }
    out->ip = dhc.result_ip;
    out->mask = dhc.result_mask;
    out->gw = dhc.result_gw;
    out->dns = dhc.result_dns;
    out->lease = dhc.result_lease;
    dhc.bound_changed = 0;
    return 0;
}

/* Called from net_poll: renew at T1, retry, re-DISCOVER on failure. */
void dhcp_tick(void) {
    if (dhc.state != DHCP_BOUND)
        return;
    if ((int32_t)(sched_get_ticks() - dhc.renew_at) < 0)
        return;

    /* T1 reached: renew (REQUEST with ciaddr) to the DHCP server */
    dhc.state = DHCP_RENEW;
    dhc.tries = 0;
    dhc.deadline = sched_get_ticks() + WAIT_TICKS;
    dhc.result_ok = 0;
    uint32_t server = dhc.server_id;
    if (server == 0)
        server = net_get_config()->gateway;
    dhcp_send_msg(DHCP_MSG_REQUEST, net_get_config()->ip, 0, 0, 0, server);
    uint32_t t0 = sched_get_ticks();
    int renewed = 0;
    while ((int32_t)(sched_get_ticks() - t0) <
           (int32_t)(WAIT_TICKS * (TRIES_MAX + 1))) {
        net_poll();
        if (dhc.state == DHCP_BOUND) {
            renewed = 1;
            break;
        }
        if ((int32_t)(sched_get_ticks() - dhc.deadline) >= 0) {
            dhc.deadline = sched_get_ticks() + WAIT_TICKS;
            dhcp_send_msg(DHCP_MSG_REQUEST, net_get_config()->ip,
                          0, 0, 0, server);
        }
    }
    if (renewed) {
        dhc.bound_changed = 0;
        dhcp_apply_lease(dhc.result_ip, dhc.result_mask, dhc.result_gw,
                         dhc.result_dns, dhc.lease);
        return;
    }
    /* Renewal failed: fresh DISCOVER -> REQUEST -> ACK cycle */
    dhc.state = DHCP_DISCOVER;
    for (int attempt = 0; attempt < TRIES_MAX; attempt++) {
        dhc.tries = 0;
        dhc.result_ok = 0;
        dhc.deadline = sched_get_ticks() + WAIT_TICKS;
        dhcp_send_msg(DHCP_MSG_DISCOVER, 0, 0, 0, 0, BC_ADDR);
        if (dhcp_wait(&dhc.result_ok, DHCP_DISCOVER) == 0 &&
            dhc.state != DHCP_DISCOVER)
            break;
    }
    if (dhc.state == DHCP_REQUEST || dhc.state == DHCP_BOUND) {
        for (int attempt = 0; attempt < TRIES_MAX; attempt++) {
            if (dhc.state == DHCP_BOUND)
                break;
            dhc.tries = 0;
            dhc.result_ok = 0;
            dhc.deadline = sched_get_ticks() + WAIT_TICKS;
            dhcp_send_msg(DHCP_MSG_REQUEST, 0, dhc.offered_ip,
                          dhc.server_id, 1, BC_ADDR);
            if (dhcp_wait(&dhc.result_ok, DHCP_REQUEST) == 0)
                break;
        }
        if (dhc.result_ok) {
            dhcp_apply_lease(dhc.result_ip, dhc.result_mask, dhc.result_gw,
                             dhc.result_dns, dhc.lease);
            return;
        }
    }
    /* Give up on re-DISCOVER: keep old lease, retry later */
    dhc.state = DHCP_BOUND;
    dhc.renew_at = sched_get_ticks() + WAIT_TICKS;
    dhc.bound_changed = 0;
    puts("[DHCP] renewal failed, will retry\n");
}
