/*
 * Dong ho thoi gian thuc dung RTC trong chip (LSE 32.768 kHz, nuoi bang VBAT/C70).
 * Gio duoc gateway gui xuong qua LoRa (lenh "time") hoac dat tay tren console.
 */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t year;      /* 2000..2099 */
    uint8_t  mon;       /* 1..12 */
    uint8_t  day;       /* 1..31 */
    uint8_t  hour;
    uint8_t  min;
    uint8_t  sec;
} clk_time_t;

bool clk_valid(void);                       /* gio da tung duoc dat (gateway/tay), khong phai 00:00 1/1/2000 */
void clk_get(clk_time_t *t);
bool clk_set(const clk_time_t *t);          /* dat gio + danh dau hop le */
bool clk_parse(const char *s, clk_time_t *t);   /* "YYYY-MM-DD HH:MM:SS" */
/* "2026-10-19 11:30:45"; tra ve "" neu gio chua hop le */
void clk_format(char *buf, size_t len);

#endif
