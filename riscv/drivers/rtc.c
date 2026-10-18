/* ===== riscv/drivers/rtc.c: 软件 RTC (RISC-V virt 无 RTC 硬件) =====
 * 可加载驱动模块 (.kdrv): rtc_init() 由模块的 Driver.init 在
 * drv_load_all() 时调用(内核不再直接调用); 纪元 = 2026-01-01 00:00:00
 * + mtime 运行秒数。符号 rtc_read/rtc_init 供 shell/linux 插件解析。 */
#include <rtc.h>
#include <driver.h>
#include <stdint.h>

#define MTIME 0x0200BFF8UL
#define BASE_EPOCH 1767225600UL     /* 2026-01-01 00:00:00 UTC */

void rtc_init(void) {
}

static int rtc_drv_init(void) {
    rtc_init();
    return 0;
}

static int is_leap(unsigned y) {
    return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0;
}

void rtc_read(RTC_Time *t) {
    uint64_t now = *(volatile uint64_t *)MTIME;
    uint32_t secs = (uint32_t)(BASE_EPOCH + now / 10000000u);
    uint32_t days = secs / 86400;
    uint32_t rem = secs % 86400;

    t->hour = rem / 3600;
    t->minute = (rem % 3600) / 60;
    t->second = rem % 60;

    unsigned y = 2000;
    while (days >= (365u + is_leap(y))) {
        days -= 365u + is_leap(y);
        y++;
    }
    static const uint8_t mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    unsigned m = 0;
    while (m < 12) {
        uint32_t dom = mdays[m] + ((m == 1 && is_leap(y)) ? 1 : 0);
        if (days < dom) break;
        days -= dom;
        m++;
    }
    t->year = (uint16_t)y;
    t->month = (uint8_t)(m + 1);
    t->day = (uint8_t)(days + 1);
}

/* .kdrv 驱动模块: 加载器(plugin.c drv_load_all)解析 kdrv_driver 注册 */
REGISTER_DRIVER("rtc", DRV_TYPE_RTC, rtc_drv_init);