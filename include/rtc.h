#ifndef _RTC_H
#define _RTC_H

#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} RTC_Time;

void rtc_init(void);
void rtc_read(RTC_Time *t);

#endif
