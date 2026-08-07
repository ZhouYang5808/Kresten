/* ===== x86/drivers/rtl8139.c: Realtek RTL8139 Fast Ethernet driver =====
 * Polled I/O-mode driver for the QEMU adapter (-net nic,model=rtl8139).
 * No interrupts: the network thread polls the RX ring (CBR vs CAPR) and
 * the TX descriptors are written and completed synchronously in QEMU.
 */
#include <driver.h>
#include <io.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define RTL_VENDOR 0x10EC
#define RTL_DEVICE 0x8139

#define RTL_IDR0   0x00   /* MAC address (6 bytes) */
#define RTL_TSD0   0x10   /* TX status registers, 4 dwords: 0x10,14,18,1C */
#define RTL_TSAD0  0x20   /* TX descriptor addresses, 4 dwords: 0x20,24,28,2C */
#define RTL_RBSTART 0x30  /* RX ring physical address (8K aligned) */
#define RTL_CMD    0x37
#define RTL_CAPR   0x38   /* RX current read pointer (write) */
#define RTL_CBR    0x3A   /* RX current write pointer (read only) */
#define RTL_IMR    0x3C
#define RTL_ISR    0x3E
#define RTL_TCR    0x40
#define RTL_RCR    0x44
#define RTL_CONFIG0 0x51
#define RTL_CONFIG1 0x52

#define CMD_RST     0x10
#define CMD_RE      0x08
#define CMD_TE      0x04

#define RCR_AB      0x0001 /* accept broadcast */
#define RCR_AM      0x0002 /* accept multicast */
#define RCR_APM     0x0004 /* accept physical match */
#define RCR_AER     0x0008 /* accept runt packets */

/* QEMU models the ring as fixed 8K and appends a 4-byte CRC per frame. */
#define RX_BUF_SIZE 8192
#define RX_ALIGN(x) (((x) + 3) & ~3)
#define TX_DONE     0x2000 /* TxHostOwns: descriptor completed by chip */

static uint16_t io_base;
static uint8_t rtl_mac[6];
static uint8_t *rx_ring;
static uint32_t rx_off = 0;   /* next unread offset in the RX ring */
static int tx_cur = 0;
static uint32_t rx_count = 0;
static uint32_t tx_count = 0;

static uint32_t pci_read_cfg(uint8_t bus, uint8_t dev, uint8_t func,
                             uint8_t reg) {
    outl(0xCF8, 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                     ((uint32_t)func << 8) | (reg & 0xFC));
    return inl(0xCFC);
}

static void pci_write_cfg(uint8_t bus, uint8_t dev, uint8_t func,
                          uint8_t reg, uint32_t val) {
    outl(0xCF8, 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                     ((uint32_t)func << 8) | (reg & 0xFC));
    outl(0xCFC, val);
}

static void rtl_reset(void) {
    outb(io_base + RTL_CMD, CMD_RST);
    for (int i = 0; i < 10000; i++) {
        if (!(inb(io_base + RTL_CMD) & CMD_RST))
            return;
        for (volatile int j = 0; j < 100; j++)
            ;
    }
}

int rtl8139_init(void) {
    int found = -1;
    for (int dev = 0; dev < 32; dev++) {
        uint32_t id = pci_read_cfg(0, (uint8_t)dev, 0, 0x00);
        if (id == 0xFFFFFFFF || id == 0)
            continue;
        if ((id & 0xFFFF) == RTL_VENDOR &&
            ((id >> 16) & 0xFFFF) == RTL_DEVICE) {
            found = dev;
            break;
        }
    }
    if (found < 0)
        return -1;

    uint32_t bar0 = pci_read_cfg(0, (uint8_t)found, 0, 0x10);
    io_base = (uint16_t)(bar0 & 0xFFFC);
    pci_write_cfg(0, (uint8_t)found, 0, 0x04, 0x05); /* IO + bus master */

    rtl_reset();

    for (int i = 0; i < 6; i++)
        rtl_mac[i] = inb(io_base + RTL_IDR0 + i);

    rx_ring = (uint8_t *)malloc(RX_BUF_SIZE + 4095);
    if (!rx_ring)
        return -1;
    rx_ring = (uint8_t *)(((uint32_t)rx_ring + 4095) & ~(uint32_t)4095);
    memset(rx_ring, 0, RX_BUF_SIZE);

    outl(io_base + RTL_RBSTART, (uint32_t)rx_ring);
    outw(io_base + RTL_CAPR, 0);
    outb(io_base + RTL_CONFIG1, inb(io_base + RTL_CONFIG1) & 0xFB);
    outl(io_base + RTL_TCR, 0x00);
    outl(io_base + RTL_RCR, RCR_AB | RCR_AM | RCR_APM | RCR_AER);
    outb(io_base + RTL_CMD, CMD_RE | CMD_TE);
    /* QEMU keeps a 16-byte headroom on the write pointer: report the read
     * pointer 16 bytes ahead of the ring start so it sees an empty ring. */
    outw(io_base + RTL_CAPR, (uint16_t)((RX_BUF_SIZE - 16) & (RX_BUF_SIZE - 1)));

    printf("[RTL8139] io=0x%x mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
           io_base, rtl_mac[0], rtl_mac[1], rtl_mac[2],
           rtl_mac[3], rtl_mac[4], rtl_mac[5]);
    return 0;
}

const uint8_t *rtl8139_get_mac(void) {
    return rtl_mac;
}

int rtl8139_send(const uint8_t *frame, uint16_t len) {
    if (len < 14 || len > 1514)
        return -1;
    outl(io_base + RTL_TSAD0 + tx_cur * 4, (uint32_t)frame);
    outl(io_base + RTL_TSD0 + tx_cur * 4, len);
    tx_cur = (tx_cur + 1) & 3;
    tx_count++;
    return 0;
}

/* Pull one frame from the RX ring.  Returns frame length, or 0 if empty. */
int rtl8139_rx(uint8_t *buf, uint16_t maxlen) {
    uint16_t cbr = inw(io_base + RTL_CBR);
    if (cbr == rx_off)
        return 0;

    int off = rx_off;
    uint16_t status = (uint16_t)(rx_ring[off] | (rx_ring[off + 1] << 8));
    uint16_t len = (uint16_t)(rx_ring[off + 2] | (rx_ring[off + 3] << 8));

    if ((status & 0x0001) && len > 4 && len - 4 <= maxlen) {
        for (int i = 0; i < len - 4; i++)
            buf[i] = rx_ring[(off + 4 + i) & (RX_BUF_SIZE - 1)];
        rx_off = (uint32_t)RX_ALIGN(off + 4 + len) & (RX_BUF_SIZE - 1);
        rx_count++;
        /* QEMU stores its internal read pointer as (CAPR + 16) */
        outw(io_base + RTL_CAPR, (uint16_t)((rx_off - 16) & (RX_BUF_SIZE - 1)));
        return len - 4;
    }
    /* bad/oversized frame: skip to the next aligned slot */
    rx_off = (uint32_t)RX_ALIGN(off + 4 + len) & (RX_BUF_SIZE - 1);
    outw(io_base + RTL_CAPR, (uint16_t)((rx_off - 16) & (RX_BUF_SIZE - 1)));
    return 0;
}

uint32_t rtl8139_rx_count(void) {
    return rx_count;
}

uint32_t rtl8139_tx_count(void) {
    return tx_count;
}

static Driver rtl8139_driver = {
    .name = "rtl8139",
    .type = DRV_TYPE_NET,
    .init = rtl8139_init,
};

int rtl8139_driver_register(void) {
    return driver_register(&rtl8139_driver);
}
