/* ===== kernel/drivers/kbd_drv.c: keyboard driver (PL050 @ 0x10006000) ===== */
#include <driver.h>
#include <stdio.h>
#include <stdint.h>

#define KBD_BASE    0x10006000
#define KMI_CR      0x00
#define KMI_STAT    0x04
#define KMI_DATA    0x08
#define KMI_CLKDIV  0x0C
#define KMI_IR      0x10

#define KMI_STAT_TXEMPTY 0x40
#define KMI_STAT_RXFULL  0x10

int kbd_init(void) {
    /* EN + RX interrupt enable */
    *(volatile uint32_t *)(KBD_BASE + KMI_CR) = 0x11;
    *(volatile uint32_t *)(KBD_BASE + KMI_CLKDIV) = 0x0A;
    while (*(volatile uint32_t *)(KBD_BASE + KMI_STAT) & KMI_STAT_RXFULL)
        (void)*(volatile uint32_t *)(KBD_BASE + KMI_DATA);
    return 0;
}

int kbd_data_available(void) {
    return (*(volatile uint32_t *)(KBD_BASE + KMI_STAT) & KMI_STAT_RXFULL) ? 1 : 0;
}

int kbd_read_scancode(void) {
    return (int)(*(volatile uint32_t *)(KBD_BASE + KMI_DATA) & 0xFF);
}

int kbd_test(void) {
    kbd_init();
    uint32_t stat = *(volatile uint32_t *)(KBD_BASE + KMI_STAT);
    uint32_t ir = *(volatile uint32_t *)(KBD_BASE + KMI_IR);
    printf("KBD test: base=0x%08X\n", (unsigned)KBD_BASE);
    printf("  STAT=0x%X (TXEMPTY=bit6, RXFULL=bit4)\n", (unsigned)stat);
    printf("  IR=0x%X (bit0=RX pending, bit1 always 1)\n", (unsigned)ir);
    if (stat & KMI_STAT_RXFULL)
        printf("  RX data: 0x%02X\n", (unsigned)(kbd_read_scancode() & 0xFF));
    if (stat & KMI_STAT_TXEMPTY)
        puts("  Keyboard status: OK (idle, TX ready)\n");
    else
        puts("  Keyboard status: BUSY\n");
    return 0;
}

static Driver kbd_driver = {
    .name = "kbd",
    .type = DRV_TYPE_KEYBOARD,
    .init = kbd_init,
};

int kbd_driver_register(void) {
    return driver_register(&kbd_driver);
}
