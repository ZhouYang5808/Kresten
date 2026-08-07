/* ===== kernel/drivers/mouse.c: PS/2 mouse via PL050 KMI1 (versatilepb) =====
 * KMI1 @ 0x10007000 is the mouse controller. Bytes are drained from its RX
 * FIFO and fed to the shared PS/2 packet parser; mouse_poll() then consumes
 * the decoded movement/button state (the desktop polls in its main loop).
 */
#include <driver.h>
#include <mouse.h>
#include <ps2packet.h>
#include <stdint.h>

#define MOUSE_BASE    0x10007000
#define KMI_CR        0x00
#define KMI_STAT      0x04
#define KMI_DATA      0x08
#define KMI_CLKDIV    0x0C

#define KMI_STAT_RXFULL  0x10
#define KMI_STAT_TXEMPTY 0x40

#define PS2_MOUSE_ENABLE 0xF4

static void mouse_flush(void) {
    while (*(volatile uint32_t *)(MOUSE_BASE + KMI_STAT) & KMI_STAT_RXFULL)
        (void)*(volatile uint32_t *)(MOUSE_BASE + KMI_DATA);
}

int mouse_init(void) {
    *(volatile uint32_t *)(MOUSE_BASE + KMI_CR) = 0x10;
    *(volatile uint32_t *)(MOUSE_BASE + KMI_CLKDIV) = 0x0A;
    mouse_flush();
    for (int i = 0; i < 100000; i++) {
        if (*(volatile uint32_t *)(MOUSE_BASE + KMI_STAT) & KMI_STAT_TXEMPTY)
            break;
    }
    *(volatile uint32_t *)(MOUSE_BASE + KMI_DATA) = PS2_MOUSE_ENABLE;
    for (int i = 0; i < 100000; i++) {
        if (*(volatile uint32_t *)(MOUSE_BASE + KMI_STAT) & KMI_STAT_RXFULL)
            break;
    }
    mouse_flush();
    return 0;
}

int mouse_poll(int *dx, int *dy, int *buttons) {
    while (*(volatile uint32_t *)(MOUSE_BASE + KMI_STAT) & KMI_STAT_RXFULL) {
        uint32_t b = *(volatile uint32_t *)(MOUSE_BASE + KMI_DATA);
        ps2_packet_feed((uint8_t)(b & 0xFF));
    }
    return ps2_consume(dx, dy, buttons);
}

static Driver mouse_driver = {
    .name = "mouse",
    .type = DRV_TYPE_KEYBOARD,
    .init = mouse_init,
};

int mouse_driver_register(void) {
    return driver_register(&mouse_driver);
}
