/*
 * IC sac BQ25798 (sheet BQ25798RQMR, datasheet SLUSDV2C) tren I2C1: SCL PB8, SDA PB9, dia chi 0x6B.
 *   INT -> PE0 (keo len 3.3V, xung muc 0 khi co su kien; doc thanh ghi Flag 0x22..0x27 de xoa)
 *   CE  -> PE1 (R16 keo xuong GND, muc 0 = cho phep sac)
 *   STAT -> LED-Y2 (phan cung tu sang khi dang sac)
 *   QON khong noi MCU.
 * Nguon vao: jack DC (VIN -> VBUS/VAC1/VAC2), pin o CN1, he thong lay tu SYS.
 * So cell do dien tro PROG (R5 13.7k) chon luc bat nguon (board nay: 3 cell).
 * ILIM_HIZ: cau chia R8/R10 tu REGN ~3.7 V -> gioi han dong vao ~3.3 A (khong vao HIZ).
 * TS: chi co cau chia R6/R9 (khong gan NTC) -> firmware bat TS_IGNORE.
 */
#ifndef BQ25798_H
#define BQ25798_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BQ_REG_COUNT 0x49       /* 0x00..0x48, doc 1 lan (datasheet 7.3.14.7 multi-read) */

typedef struct {
    uint8_t  reg[BQ_REG_COUNT]; /* ban sao tho toan bo thanh ghi */

    /* Gia tri dat (setpoint) */
    uint16_t vsysmin_mv;
    uint16_t vreg_mv;           /* dien ap sac day */
    uint16_t ichg_ma;           /* dong sac */
    uint16_t vindpm_mv;         /* nguong ap vao toi thieu */
    uint16_t iindpm_ma;         /* gioi han dong vao */
    uint8_t  cells;             /* 1..4 */
    bool     en_chg;
    bool     en_hiz;
    bool     mppt_on;           /* EN_MPPT (REG15 bit0) dang bat */

    /* Trang thai */
    bool     vbus_present;
    bool     ac1_present, ac2_present;
    bool     power_good;
    bool     vbat_present;
    bool     iindpm_reg, vindpm_reg, treg;   /* dang bi gioi han dong vao / ap vao / nhiet */
    bool     vsys_min_reg;      /* pin thap hon VSYSMIN, SYS dang duoc giu o VSYSMIN */
    uint8_t  chg_stat;          /* 0 khong sac, 1 trickle, 2 pre, 3 CC, 4 CV, 6 top-off, 7 xong */
    uint8_t  vbus_stat;         /* loai nguon vao, xem bq_vbus_str() */

    /* ADC */
    uint16_t vbus_mv, vac1_mv, vac2_mv, vbat_mv, vsys_mv;
    int16_t  ibus_ma;           /* duong = tu nguon vao */
    int16_t  ibat_ma;           /* duong = dang sac, am = pin dang xa */
    uint16_t ts_pct_x10;        /* TS / REGN, don vi 0.1 % */
    int16_t  tdie_x10;          /* nhiet do chip, 0.1 do C */

    /* Su kien: cac thanh ghi Flag tu xoa khi doc, nen gom lai o day cho app xu ly */
    uint8_t  flags[6];          /* REG22..REG27 cua lan doc nay */
} bq_info_t;

bool bq_init(void);                     /* true neu chip tra loi, da cau hinh theo config.h */
bool bq_present(void);
bool bq_read(bq_info_t *bq);            /* doc toan bo thanh ghi + giai ma */
bool bq_charge_enable(bool on);         /* bit EN_CHG + chan CE */
/* Goi sau moi bq_read(): bat lai EN_MPPT khi co nguon vao (chip tu tat khi mat VBUS).
 * Tra ve true neu vua bat. Khong lam gi neu CFG_BQ_MPPT = 0. */
bool bq_mppt_task(const bq_info_t *bq);
/* Goi sau moi bq_read(): nguon vao yeu bi chip dua vao HIZ thi moi CFG_BQ_HIZ_RETRY_MS xoa EN_HIZ
 * de chip kiem tra lai nguon (mac dinh chip 7 phut moi thu). Tra ve true neu vua xoa. */
bool bq_hiz_retry_task(const bq_info_t *bq);
bool bq_reg_read(uint8_t reg, uint8_t *val);
bool bq_reg_write(uint8_t reg, uint8_t val);

const char *bq_chg_str(uint8_t chg_stat);
const char *bq_vbus_str(uint8_t vbus_stat);
/* Liet ke cac loi dang co ("VBAT OVP,TSHUT"...), "" neu khong co loi. Tra ve so loi. */
int  bq_fault_str(const bq_info_t *bq, char *buf, size_t len);
/* Phan tram pin uoc luong theo dien ap moi cell (khong chinh xac khi dang sac/xa manh) */
uint8_t bq_battery_percent(const bq_info_t *bq);
void bq_dump(void);                     /* in toan bo trang thai ra console */

#endif
