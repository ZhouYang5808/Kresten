/* ===== x86/drivers/mouse.c: PS/2 mouse driver (IRQ12) =====
 * Feeds raw bytes to the shared packet parser (shared/drivers/ps2packet.c).
 * The desktop polls mouse_poll() with interrupts masked so the IRQ12
 * handler can not race the state read.
 */
#include <driver.h>
#include <mouse.h>
#include <idt.h>
#include <io.h>
#include <stdio.h>
#include <ps2packet.h>

#define KBD_STATUS 0x64
#define KBD_DATA   0x60

#define PS2_CMD_READ_CMD  0x20
#define PS2_CMD_WRITE_CMD 0x60
#define PS2_CMD_ENABLE_AUX 0xA8
#define PS2_CMD_SEND_TO_AUX 0xD4
#define MOUSE_CMD_ENABLE  0xF4

static void ps2_wait_input(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(KBD_STATUS) & 0x02))
            return;
    }
}

static void ps2_wait_output(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(KBD_STATUS) & 0x01)
            return;
    }
}

static void mouse_handler(registers_t *r) {
    (void)r;
    while (inb(KBD_STATUS) & 0x01) {
        if (!(inb(KBD_STATUS) & 0x20)) /* pending byte is from the keyboard */
            break;                     /* leave it for the kbd IRQ handler */
        ps2_packet_feed(inb(KBD_DATA));
    }
}

int mouse_poll(int *dx, int *dy, int *buttons) {
    int r;
    __asm__ volatile("cli");
    r = ps2_consume(dx, dy, buttons);
    __asm__ volatile("sti");
    return r;
}

int mouse_init(void) {
    uint8_t cmd;
    ps2_wait_input();
    outb(KBD_STATUS, PS2_CMD_ENABLE_AUX);
    ps2_wait_input();
    outb(KBD_STATUS, PS2_CMD_READ_CMD);
    ps2_wait_output();
    cmd = inb(KBD_DATA);
    ps2_wait_input();
    outb(KBD_STATUS, PS2_CMD_WRITE_CMD);
    ps2_wait_input();
    outb(KBD_DATA, cmd | 0x02); /* enable IRQ12 for aux device */
    ps2_wait_input();
    outb(KBD_STATUS, PS2_CMD_SEND_TO_AUX);
    ps2_wait_input();
    outb(KBD_DATA, MOUSE_CMD_ENABLE);
    ps2_wait_output();
    inb(KBD_DATA); /* discard ACK */
    irq_install_handler(12, mouse_handler);
    outb(0x21, inb(0x21) & ~0x04); /* unmask cascade line (master IRQ2) */
    outb(0xA1, inb(0xA1) & ~0x10); /* unmask IRQ12 on the slave PIC */
    return 0;
}

static Driver mouse_driver = {
    .name = "mouse",
    .type = DRV_TYPE_KEYBOARD,
    .init = mouse_init,
};

int mouse_driver_register(void) {
    return driver_register(&mouse_driver);
}
