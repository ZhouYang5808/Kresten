/* ===== riscv/drivers/kbd.c (RISC-V): 键盘硬件层 = 无 (UART 键盘) =====
 * 可加载驱动模块 (.kdrv), 由 drv_load_all() 加载注册; kbd_* 委托给
 * keyboard.c 的合成扫描码 (uart_drain)。kbd_test 汇报说明。
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

/* .kdrv 驱动模块: 加载器(plugin.c drv_load_all)解析 kdrv_driver 注册 */
REGISTER_DRIVER("kbd", DRV_TYPE_KEYBOARD, kbd_init);