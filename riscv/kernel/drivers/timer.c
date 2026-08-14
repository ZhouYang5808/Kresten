/* ===== kernel/drivers/timer.c: RISC-V CLINT mtime -> sched ticks =====
 * QEMU virt CLINT @ 0x02000000, mtime 寄存器 @ 0x0200BFF8, 10 MHz 自由计数。
 * sched_get_ticks() 返回厘秒 (100 Hz), 与 x86 PIT 滴答率一致
 * (桌面双击窗口 50 ticks = 0.5 s)。
 */
#include <driver.h>
#include <stdint.h>

#define MTIME 0x0200BFF8UL

static int timer_init(void) {
    return 0;                       /* CLINT 免费运行, 无需初始化 */
}

uint32_t sched_get_ticks(void) {
    uint64_t v = *(volatile uint64_t *)MTIME;
    return (uint32_t)(v / 100000u); /* 10 MHz / 100 = 100 Hz */
}

static Driver timer_driver = {
    .name = "timer",
    .type = DRV_TYPE_SCHEDULER,
    .init = timer_init,
};

int timer_driver_register(void) {
    return driver_register(&timer_driver);
}