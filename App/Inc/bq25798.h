/*
 * IC sac BQ25798 (sheet BQ25798RQMR) tren I2C1: SCL PB8, SDA PB9, dia chi 0x6B.
 *   INT -> PE0 (keo len 3.3V, muc 0 = co su kien)
 *   CE  -> PE1 (R16 keo xuong GND, muc 0 = cho phep sac)
 *   STAT -> LED-Y2 (phan cung tu sang khi dang sac)
 *   QON khong noi MCU.
 * Nguon vao: jack DC (VIN -> VBUS/VAC1/VAC2), pin o CN1, he thong lay tu SYS.
 * So cell va tan so dong cat do dien tro PROG (R5 13.7k) chon luc bat nguon.
 */
#ifndef BQ25798_H
#define BQ25798_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     vbus_present;      /* co nguon vao */
    bool     power_good;
    uint8_t  chg_stat;          /* 0 khong sac, 1 trickle, 2 pre, 3 CC, 4 CV, 6 top-off, 7 xong */
    uint8_t  cells;             /* 1..4 theo PROG */
    uint16_t vbus_mv;
    uint16_t vbat_mv;
    uint16_t vsys_mv;
    int16_t  ibus_ma;
    int16_t  ibat_ma;           /* duong = dang sac, am = pin dang xa */
    int16_t  tdie_c;            /* do C */
    uint8_t  fault0, fault1;    /* REG20, REG21 */
    uint8_t  ts_stat;           /* REG1F: bit3 cold, bit2 cool, bit1 warm, bit0 hot */
} bq_status_t;

bool bq_init(void);                     /* true neu chip tra loi, da cau hinh theo config.h */
bool bq_present(void);
bool bq_read(bq_status_t *st);          /* doc trang thai + ADC */
bool bq_charge_enable(bool on);         /* bit EN_CHG + chan CE */
bool bq_reg_read(uint8_t reg, uint8_t *val);
bool bq_reg_write(uint8_t reg, uint8_t val);
const char *bq_chg_str(uint8_t chg_stat);
void bq_dump(void);                     /* in toan bo trang thai ra console */

#endif
