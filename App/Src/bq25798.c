#include "bq25798.h"
#include "config.h"
#include "main.h"
#include <stdio.h>

extern I2C_HandleTypeDef hi2c1;

#define BQ_ADDR         (0x6Bu << 1)

/* Thanh ghi (datasheet BQ25798 muc 9.5); thanh ghi 16 bit: byte cao o dia chi thap */
#define REG_VREG        0x01    /* 16 bit, 10 mV/bit */
#define REG_ICHG        0x03    /* 16 bit, 10 mA/bit */
#define REG_IINDPM      0x06    /* 16 bit, 10 mA/bit */
#define REG_RECHG       0x0A    /* CELL[7:6] */
#define REG_CTRL0       0x0F    /* EN_CHG bit5, EN_HIZ bit2 */
#define REG_CTRL1       0x10    /* WD_RST bit3, WATCHDOG[2:0] */
#define REG_NTC1        0x18    /* TS_IGNORE bit0 */
#define REG_STAT0       0x1B    /* PG_STAT bit3, VBUS_PRESENT bit0 */
#define REG_STAT1       0x1C    /* CHG_STAT[7:5] */
#define REG_STAT4       0x1F    /* TS_COLD/COOL/WARM/HOT */
#define REG_FAULT0      0x20
#define REG_FAULT1      0x21
#define REG_ADC_CTRL    0x2E    /* ADC_EN bit7, ADC_RATE bit6 (0 = lien tuc) */
#define REG_IBUS_ADC    0x31
#define REG_IBAT_ADC    0x33
#define REG_VBUS_ADC    0x35
#define REG_VBAT_ADC    0x3B
#define REG_VSYS_ADC    0x3D
#define REG_TDIE_ADC    0x41    /* 0.5 do C/bit */
#define REG_PART_INFO   0x48    /* PN[5:3] = 011 la BQ25798 */

static bool present;

bool bq_reg_read(uint8_t reg, uint8_t *val)
{
    return HAL_I2C_Mem_Read(&hi2c1, BQ_ADDR, reg, I2C_MEMADD_SIZE_8BIT, val, 1, 20) == HAL_OK;
}

bool bq_reg_write(uint8_t reg, uint8_t val)
{
    return HAL_I2C_Mem_Write(&hi2c1, BQ_ADDR, reg, I2C_MEMADD_SIZE_8BIT, &val, 1, 20) == HAL_OK;
}

static bool read16(uint8_t reg, uint16_t *val)
{
    uint8_t b[2];
    if (HAL_I2C_Mem_Read(&hi2c1, BQ_ADDR, reg, I2C_MEMADD_SIZE_8BIT, b, 2, 20) != HAL_OK) {
        return false;
    }
    *val = (uint16_t)((b[0] << 8) | b[1]);
    return true;
}

__attribute__((unused))     /* chi dung khi dat CFG_BQ_VREG_MV / ICHG / IINDPM */
static bool write16(uint8_t reg, uint16_t val)
{
    uint8_t b[2] = { (uint8_t)(val >> 8), (uint8_t)val };
    return HAL_I2C_Mem_Write(&hi2c1, BQ_ADDR, reg, I2C_MEMADD_SIZE_8BIT, b, 2, 20) == HAL_OK;
}

static bool update_bits(uint8_t reg, uint8_t mask, uint8_t val)
{
    uint8_t v;
    if (!bq_reg_read(reg, &v)) { return false; }
    v = (uint8_t)((v & ~mask) | (val & mask));
    return bq_reg_write(reg, v);
}

bool bq_present(void)
{
    return present;
}

bool bq_charge_enable(bool on)
{
    /* CE tich cuc muc thap; ca chan CE va bit EN_CHG phai cho phep thi moi sac */
    HAL_GPIO_WritePin(BQ_CE_GPIO_Port, BQ_CE_Pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
    return update_bits(REG_CTRL0, 0x20u, on ? 0x20u : 0x00u);
}

bool bq_init(void)
{
    uint8_t part;
    present = bq_reg_read(REG_PART_INFO, &part);
    if (!present) { return false; }

    /* Tat watchdog: mac dinh 40 s khong "da" thi chip tu tra cac thanh ghi ve mac dinh */
    update_bits(REG_CTRL1, 0x07u, 0x00u);

#if CFG_BQ_TS_IGNORE
    /* So do khong co NTC: TS chi la cau chia R6/R9 (~85% REGN) roi vao vung "lanh"
     * nen chip se khong sac. Bo qua TS. */
    update_bits(REG_NTC1, 0x01u, 0x01u);
#endif
#if CFG_BQ_VREG_MV
    write16(REG_VREG, (uint16_t)(CFG_BQ_VREG_MV / 10u));
#endif
#if CFG_BQ_ICHG_MA
    write16(REG_ICHG, (uint16_t)(CFG_BQ_ICHG_MA / 10u));
#endif
#if CFG_BQ_IINDPM_MA
    write16(REG_IINDPM, (uint16_t)(CFG_BQ_IINDPM_MA / 10u));
#endif

    /* ADC chay lien tuc, 15 bit */
    bq_reg_write(REG_ADC_CTRL, 0x80u);

    bq_charge_enable(CFG_BQ_CHARGE_ENABLE != 0);
    return true;
}

bool bq_read(bq_status_t *st)
{
    uint8_t s0, s1, rc;
    uint16_t v;

    if (!bq_reg_read(REG_STAT0, &s0) || !bq_reg_read(REG_STAT1, &s1)) {
        present = false;
        return false;
    }
    present = true;
    st->vbus_present = (s0 & 0x01u) != 0;
    st->power_good   = (s0 & 0x08u) != 0;
    st->chg_stat     = (uint8_t)(s1 >> 5);
    st->cells        = bq_reg_read(REG_RECHG, &rc) ? (uint8_t)((rc >> 6) + 1u) : 0;
    bq_reg_read(REG_STAT4, &st->ts_stat);
    st->ts_stat &= 0x0Fu;
    bq_reg_read(REG_FAULT0, &st->fault0);
    bq_reg_read(REG_FAULT1, &st->fault1);

    st->vbus_mv = read16(REG_VBUS_ADC, &v) ? v : 0;
    st->vbat_mv = read16(REG_VBAT_ADC, &v) ? v : 0;
    st->vsys_mv = read16(REG_VSYS_ADC, &v) ? v : 0;
    st->ibus_ma = read16(REG_IBUS_ADC, &v) ? (int16_t)v : 0;
    st->ibat_ma = read16(REG_IBAT_ADC, &v) ? (int16_t)v : 0;
    st->tdie_c  = read16(REG_TDIE_ADC, &v) ? (int16_t)((int16_t)v / 2) : 0;
    return true;
}

const char *bq_chg_str(uint8_t chg_stat)
{
    static const char *const s[8] = {
        "khong sac", "trickle", "pre-charge", "CC", "CV", "?", "top-off", "da day",
    };
    return s[chg_stat & 7u];
}

void bq_dump(void)
{
    bq_status_t st;
    uint8_t part = 0, ctrl0 = 0, ntc = 0;
    uint16_t vreg = 0, ichg = 0, iin = 0;

    if (!bq_read(&st)) {
        printf("BQ25798: khong tra loi I2C1 (0x6B)\n");
        return;
    }
    bq_reg_read(REG_PART_INFO, &part);
    bq_reg_read(REG_CTRL0, &ctrl0);
    bq_reg_read(REG_NTC1, &ntc);
    read16(REG_VREG, &vreg);
    read16(REG_ICHG, &ichg);
    read16(REG_IINDPM, &iin);

    printf("BQ25798: part 0x%02X, %u cell, sac %s (EN_CHG %u, CE %u, TS_IGNORE %u)\n",
           part, st.cells, bq_chg_str(st.chg_stat), (ctrl0 >> 5) & 1u,
           HAL_GPIO_ReadPin(BQ_CE_GPIO_Port, BQ_CE_Pin) == GPIO_PIN_SET, ntc & 1u);
    printf("  Dat: VREG %u mV, ICHG %u mA, IINDPM %u mA\n",
           (unsigned)((vreg & 0x07FFu) * 10u), (unsigned)((ichg & 0x01FFu) * 10u),
           (unsigned)((iin & 0x01FFu) * 10u));
    printf("  VBUS %u mV (%s%s), IBUS %d mA\n", st.vbus_mv, st.vbus_present ? "co nguon" : "khong nguon",
           st.power_good ? ", PG" : "", st.ibus_ma);
    printf("  VBAT %u mV, IBAT %d mA, VSYS %u mV, TDIE %d C\n",
           st.vbat_mv, st.ibat_ma, st.vsys_mv, st.tdie_c);
    printf("  TS 0x%X, FAULT 0x%02X 0x%02X, INT %s\n", st.ts_stat, st.fault0, st.fault1,
           HAL_GPIO_ReadPin(BQ_INT_GPIO_Port, BQ_INT_Pin) == GPIO_PIN_RESET ? "0 (co su kien)" : "1");
}
