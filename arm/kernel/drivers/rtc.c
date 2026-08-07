/* ===== kernel/drivers/rtc.c: Versatile PB PL031 RTC driver ===== */
#include <rtc.h>
#include <stdint.h>

#define RTC_BASE 0x101E8000UL
#define RTC_DR   0x00 /* data register: seconds since 2000-01-01 00:00:00 */
#define RTC_MR   0x04 /* match register */
#define RTC_LR   0x08 /* load register */
#define RTC_CR   0x0C /* control register: bit0 RTCEN, bit1 RTCSTART */
#define RTC_IMSC 0x10
#define RTC_IS   0x14

void rtc_init(void) {
    volatile uint32_t *cr = (volatile uint32_t *)(RTC_BASE + RTC_CR);
    *cr |= 0x3; /* RTCEN | RTCSTART */
}

static int is_leap(unsigned y) {
    return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0;
}

void rtc_read(RTC_Time *t) {
    uint32_t secs = *(volatile uint32_t *)(RTC_BASE + RTC_DR);
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
