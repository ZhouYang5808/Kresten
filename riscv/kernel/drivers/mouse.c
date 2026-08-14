/* ===== kernel/drivers/mouse.c (RISC-V): 无鼠标 (virt 无 PS/2) =====
 * 桌面在无鼠标时进入键盘导航模式; mouse_poll 恒返回无事件。
 */
#include <driver.h>
#include <mouse.h>
#include <stdint.h>

int mouse_init(void) {
    return 0;
}

int mouse_poll(int *dx, int *dy, int *buttons) {
    if (dx) *dx = 0;
    if (dy) *dy = 0;
    if (buttons) *buttons = 0;
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