#include "clock.h"
#include "main.h"
#include <stdio.h>

extern RTC_HandleTypeDef hrtc;

/* RTC_BKP_DR2 = CLK_VALID_MAGIC: gio da duoc dat dung (DR1 do main.c dung) */
#define CLK_VALID_MAGIC 0x5A17u

bool clk_valid(void)
{
    return HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR2) == CLK_VALID_MAGIC;
}

void clk_get(clk_time_t *t)
{
    RTC_TimeTypeDef tm;
    RTC_DateTypeDef dt;
    /* Phai doc Time truoc roi Date de mo khoa thanh ghi bong */
    HAL_RTC_GetTime(&hrtc, &tm, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &dt, RTC_FORMAT_BIN);
    t->year = (uint16_t)(2000u + dt.Year);
    t->mon  = dt.Month;
    t->day  = dt.Date;
    t->hour = tm.Hours;
    t->min  = tm.Minutes;
    t->sec  = tm.Seconds;
}

/* Thu trong tuan, 1 = thu Hai .. 7 = Chu nhat (thuat toan Sakamoto) */
static uint8_t weekday(int y, int m, int d)
{
    static const int k[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) { y -= 1; }
    int w = (y + y / 4 - y / 100 + y / 400 + k[m - 1] + d) % 7;   /* 0 = CN */
    return (uint8_t)(w == 0 ? 7 : w);
}

bool clk_set(const clk_time_t *t)
{
    if (t->year < 2000 || t->year > 2099 || t->mon < 1 || t->mon > 12 ||
        t->day < 1 || t->day > 31 || t->hour > 23 || t->min > 59 || t->sec > 59) {
        return false;
    }
    RTC_TimeTypeDef tm = { 0 };
    RTC_DateTypeDef dt = { 0 };
    tm.Hours = t->hour;
    tm.Minutes = t->min;
    tm.Seconds = t->sec;
    tm.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    tm.StoreOperation = RTC_STOREOPERATION_RESET;
    dt.Year = (uint8_t)(t->year - 2000u);
    dt.Month = t->mon;
    dt.Date = t->day;
    dt.WeekDay = weekday(t->year, t->mon, t->day);
    HAL_PWR_EnableBkUpAccess();                 /* mien backup co the dang bi khoa ghi */
    if (HAL_RTC_SetTime(&hrtc, &tm, RTC_FORMAT_BIN) != HAL_OK ||
        HAL_RTC_SetDate(&hrtc, &dt, RTC_FORMAT_BIN) != HAL_OK) {
        return false;
    }
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR2, CLK_VALID_MAGIC);
    return true;
}

bool clk_parse(const char *s, clk_time_t *t)
{
    int y, mo, d, h, mi, se;
    if (sscanf(s, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) { return false; }
    t->year = (uint16_t)y;
    t->mon = (uint8_t)mo;
    t->day = (uint8_t)d;
    t->hour = (uint8_t)h;
    t->min = (uint8_t)mi;
    t->sec = (uint8_t)se;
    return true;
}

void clk_format(char *buf, size_t len)
{
    clk_time_t t;
    if (!clk_valid()) {
        if (len) { buf[0] = '\0'; }
        return;
    }
    clk_get(&t);
    snprintf(buf, len, "%04u-%02u-%02u %02u:%02u:%02u",
             t.year, t.mon, t.day, t.hour, t.min, t.sec);
}
