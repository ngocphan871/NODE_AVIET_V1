/*
 * Cam bien mua/tuyet ES-RAIN-02, Modbus RTU qua RS485 (CN2: 1 SYS, 2 B, 3 A, 4 GND).
 *   Day: nau V+ (10..30 V, lay tu SYS), den GND, vang A (Data+), xanh duong B (Data-),
 *        xanh la/trang = tiep diem relay bao mua (khong noi vao board).
 *   Mac dinh: dia chi 1, 4800 baud 8N1, chi ho tro ma lenh 03 / 06.
 *   Thanh ghi: 0x0000 trang thai (0 binh thuong, 1 co mua/tuyet)
 *              0x0030 nhiet do ngung suoi (mac dinh 35 C, 0..70)
 *              0x0031 nhiet do bat suoi   (mac dinh 15 C, -30..70)
 *              0x0032 tre nhiet suoi      (mac dinh 5 C)
 *              -> 3 thanh ghi nhiet do tren don vi 0.1 C (doc thuc te 2026-10-20: 350/150/50,
 *                 manual chi ghi "35 C")
 *              0x0033 thoi gian xac nhan bao mua, giay (mac dinh 1, 0..60000)
 *              0x0034 do nhay (mac dinh 800, 500..3500; cao qua de bao nham)
 * Luu y: khi suoi cam bien an ~3.4 W tu SYS (pin).
 */
#ifndef ES_RAIN_H
#define ES_RAIN_H

#include <stdbool.h>
#include <stdint.h>
#include "modbus.h"

#define RAIN_REG_STATE      0x0000
#define RAIN_REG_HEAT_MAX   0x0030
#define RAIN_REG_HEAT_MIN   0x0031
#define RAIN_REG_HEAT_HYS   0x0032
#define RAIN_REG_DELAY      0x0033
#define RAIN_REG_SENS       0x0034

typedef struct {
    int16_t  heat_max_x10;      /* 0.1 do C */
    int16_t  heat_min_x10;
    uint16_t heat_hys_x10;
    uint16_t delay_s;
    uint16_t sensitivity;
} rain_cfg_t;

mb_status_t rain_read(bool *raining);           /* doc trang thai mua */
mb_status_t rain_read_cfg(rain_cfg_t *cfg);     /* doc 5 thanh ghi cau hinh */
/* Ghi 1 thanh ghi cau hinh (ma 06), gia tri tho: nhiet do tinh bang 0.1 C, delay giay, do nhay */
mb_status_t rain_write(uint16_t reg, int16_t value);

#endif
