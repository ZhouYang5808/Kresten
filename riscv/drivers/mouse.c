/* ===== riscv/drivers/mouse.c (RISC-V): 无鼠标 (virt 无 PS/2) =====
 * 可加载驱动模块 (.kdrv), 由 drv_load_all() 加载注册; 桌面在无鼠标时
 * 进入键盘导航模式; mouse_poll 恒返回无事件。
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

/* .kdrv 驱动模块: 加载器(plugin.c drv_load_all)解析 kdrv_driver 注册 */
REGISTER_DRIVER("mouse", DRV_TYPE_KEYBOARD, mouse_init);