/* ===== kernel/drivers/kbd.c (RISC-V): 键盘硬件层 = 无 (UART 键盘) =====
 * kbd_* 委托给 keyboard.c 的合成扫描码 (uart_drain)。kbd_test 汇报说明。
 */
#include <driver.h>
#include <stdio.h>
#include <stdint.h>
#include <keyboard.h>

int kbd_init(void) {
    return 0;
}

int kbd_data_available(void) {
    return keyboard_scancode_pending();
}

int kbd_read_scancode(void) {
    return keyboard_poll_scancode();
}

int kbd_test(void) {
    printf("KBD test: riscv virt has no PS/2; input = UART 16550 ASCII\n");
    printf("  scancodes synthesized from UART chars (set-1 layout)\n");
    puts("  Keyboard status: OK (UART)\n");
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