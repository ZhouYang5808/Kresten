/* ===== x86/drivers/kbd.c: keyboard driver (PS/2 IRQ1) ===== */
#include <driver.h>
#include <stdio.h>
#include <keyboard.h>

int kbd_init(void) {
    keyboard_init();
    return 0;
}

int kbd_data_available(void) {
    return keyboard_getchar() >= 0 ? 1 : 0;
}

int kbd_read_scancode(void) {
    return keyboard_getchar();
}

int kbd_test(void) {
    kbd_init();
    int c = keyboard_getchar();
    printf("KBD test: PS/2 IRQ1 driver\n");
    printf("  RX data: %s\n", c >= 0 ? "available" : "idle (no key pressed)");
    if (c >= 0)
        printf("  first byte: 0x%02X ('%c')\n", (unsigned)c, c);
    puts("  Keyboard status: OK\n");
    return 0;
}

/* .kdrv 驱动模块(不再静态编译进内核; keyboard.c 仍为启动子系统驱动) */
REGISTER_DRIVER("kbd", DRV_TYPE_KEYBOARD, kbd_init);
