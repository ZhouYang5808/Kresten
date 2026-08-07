/* ===== kernel/drivers/timer.c: SP804 free-running timer -> sched ticks =====
 * versatilepb SP804 timer0 @ 0x101e2000 counts down from 0xFFFFFFFF at 1 MHz.
 * sched_get_ticks() returns centiseconds (100 Hz), matching the x86 PIT tick
 * rate the shared desktop's double-click window expects (50 ticks = 0.5 s).
 */
#include <driver.h>
#include <stdint.h>

#define TIMER_BASE    0x101e2000
#define TIMER_LOAD    0x00
#define TIMER_VALUE   0x04
#define TIMER_CTRL    0x08

#define TIMER_CTRL_32BIT 0x02
#define TIMER_CTRL_EN    0x80

static int timer_init(void) {
    *(volatile uint32_t *)(TIMER_BASE + TIMER_LOAD) = 0xFFFFFFFFu;
    *(volatile uint32_t *)(TIMER_BASE + TIMER_CTRL) = TIMER_CTRL_EN | TIMER_CTRL_32BIT;
    return 0;
}

uint32_t sched_get_ticks(void) {
    uint32_t v = *(volatile uint32_t *)(TIMER_BASE + TIMER_VALUE);
    return (0xFFFFFFFFu - v) / 10000u;
}

static Driver timer_driver = {
    .name = "timer",
    .type = DRV_TYPE_SCHEDULER,
    .init = timer_init,
};

int timer_driver_register(void) {
    return driver_register(&timer_driver);
}
