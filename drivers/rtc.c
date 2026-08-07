#include <rtc.h>
#include <io.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

#define CMOS_SEC    0x00
#define CMOS_MIN    0x02
#define CMOS_HOUR   0x04
#define CMOS_DAY    0x07
#define CMOS_MONTH  0x08
#define CMOS_YEAR   0x09
#define CMOS_STAT_A 0x0A
#define CMOS_STAT_B 0x0B

static int bcd_mode = 0;

static unsigned char cmos_read(unsigned char reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static int cmos_is_updating(void) {
    outb(CMOS_ADDR, CMOS_STAT_A);
    return inb(CMOS_DATA) & 0x80;
}

static unsigned char rtc_convert(unsigned char val) {
    if (bcd_mode) return (val & 0x0F) + ((val >> 4) * 10);
    return val;
}

void rtc_init(void) {
    unsigned char stb = cmos_read(CMOS_STAT_B);
    bcd_mode = !(stb & 0x04);
}

void rtc_read(RTC_Time *t) {
    while (cmos_is_updating());
    unsigned char sec   = cmos_read(CMOS_SEC);
    unsigned char min   = cmos_read(CMOS_MIN);
    unsigned char hour  = cmos_read(CMOS_HOUR);
    unsigned char day   = cmos_read(CMOS_DAY);
    unsigned char month = cmos_read(CMOS_MONTH);
    unsigned char year  = cmos_read(CMOS_YEAR);
    while (cmos_is_updating());
    sec   = cmos_read(CMOS_SEC);
    min   = cmos_read(CMOS_MIN);
    hour  = cmos_read(CMOS_HOUR);
    day   = cmos_read(CMOS_DAY);
    month = cmos_read(CMOS_MONTH);
    year  = cmos_read(CMOS_YEAR);

    t->second = rtc_convert(sec);
    t->minute = rtc_convert(min);
    t->hour   = rtc_convert(hour);
    t->day    = rtc_convert(day);
    t->month  = rtc_convert(month);
    t->year   = rtc_convert(year) + 2000;
}
