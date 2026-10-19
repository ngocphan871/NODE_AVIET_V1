#include "bq25798.h"
#include "config.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c1;

#define BQ_ADDR         (0x6Bu << 1)

/* Thanh ghi (datasheet BQ25798 SLUSDV2C muc 7.5); thanh ghi 16 bit: byte cao o dia chi thap */
#define REG_VSYSMIN     0x00    /* VSYSMIN[5:0], 2500 mV + 250 mV/bit */
#define REG_VREG        0x01    /* 16 bit, VREG[10:0], 10 mV/bit */
#define REG_ICHG        0x03    /* 16 bit, ICHG[8:0], 10 mA/bit */
#define REG_VINDPM      0x05    /* 100 mV/bit */
#define REG_IINDPM      0x06    /* 16 bit, IINDPM[8:0], 10 mA/bit */
#define REG_RECHG       0x0A    /* CELL[7:6] */
#define REG_TIMER       0x0E
#define REG_CTRL0       0x0F    /* EN_CHG bit5, EN_HIZ bit2 */
#define REG_CTRL1       0x10    /* WD_RST bit3, WATCHDOG[2:0] */
#define REG_CTRL5       0x14    /* EN_IBAT bit5 */
#define REG_MPPT        0x15    /* VOC_PCT[7:5], VOC_DLY[4:3], VOC_RATE[2:1], EN_MPPT bit0 */
#define REG_NTC1        0x18    /* TS_IGNORE bit0 */
#define REG_STAT0       0x1B    /* IINDPM b7, VINDPM b6, WD b5, PG b3, AC2 b2, AC1 b1, VBUS b0 */
#define REG_STAT1       0x1C    /* CHG_STAT[7:5], VBUS_STAT[4:1] */
#define REG_STAT2       0x1D    /* TREG b2, VBAT_PRESENT b0 */
#define REG_STAT3       0x1E    /* VSYS_STAT b4, CHG_TMR b3, TRICHG_TMR b2, PRECHG_TMR b1 */
#define REG_STAT4       0x1F    /* TS_COLD b3, TS_COOL b2, TS_WARM b1, TS_HOT b0 */
#define REG_FAULT0      0x20
#define REG_FAULT1      0x21
#define REG_FLAG0       0x22    /* 0x22..0x27: doc la xoa */
#define REG_ADC_CTRL    0x2E    /* ADC_EN bit7, ADC_RATE bit6 (0 = lien tuc), ADC_SAMPLE[5:4] */
#define REG_IBUS_ADC    0x31    /* bu 2, 1 mA */
#define REG_IBAT_ADC    0x33    /* bu 2, 1 mA */
#define REG_VBUS_ADC    0x35
#define REG_VAC1_ADC    0x37
#define REG_VAC2_ADC    0x39
#define REG_VBAT_ADC    0x3B
#define REG_VSYS_ADC    0x3D
#define REG_TS_ADC      0x3F    /* 0.0976563 %/bit = 100/1024 % */
#define REG_TDIE_ADC    0x41    /* bu 2, 0.5 do C/bit */
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

static uint16_t u16(const uint8_t *r, uint8_t reg)
{
    return (uint16_t)((r[reg] << 8) | r[reg + 1]);
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

    /* Tat watchdog: mac dinh 40 s khong "da" thi chip tra EN_CHG, ADC_EN, EN_IBAT,
     * TS_IGNORE, ICHG... ve mac dinh (cac bit "Reset by: WATCHDOG" trong datasheet) */
    update_bits(REG_CTRL1, 0x07u, 0x00u);

#if CFG_BQ_TS_IGNORE
    /* Khong co NTC: TS chi la cau chia R6/R9 (~85% REGN) roi vao vung "lanh" (> VT1 71.1%)
     * nen chip se khong sac. TS_IGNORE = 1 thi chip coi TS luon tot. */
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

    /* EN_IBAT: mac dinh chip khong do dong xa pin khi chay bang pin (IBAT ADC = 0) */
    update_bits(REG_CTRL5, 0x20u, 0x20u);

    /* ADC: bat, chuyen doi lien tuc, 15 bit, khong lay trung binh */
    bq_reg_write(REG_ADC_CTRL, 0x80u);

#if CFG_BQ_MPPT
    /* Thong so do VOC; EN_MPPT bat sau trong bq_mppt_task() khi co nguon vao.
     * EN_ICO, FORCE_VINDPM_DET va EN_MPPT chi duoc bat 1 cai -> tat EN_ICO (REG0F bit4). */
    update_bits(REG_CTRL0, 0x10u, 0x00u);
    update_bits(REG_MPPT, 0xFEu, (uint8_t)(((CFG_BQ_VOC_PCT & 7u) << 5) | ((CFG_BQ_VOC_DLY & 3u) << 3) |
                                           ((CFG_BQ_VOC_RATE & 3u) << 1)));
#else
    update_bits(REG_MPPT, 0x01u, 0x00u);
#endif

    bq_charge_enable(CFG_BQ_CHARGE_ENABLE != 0);
    return true;
}

bool bq_read(bq_info_t *bq)
{
    uint8_t *r = bq->reg;

    if (HAL_I2C_Mem_Read(&hi2c1, BQ_ADDR, 0x00, I2C_MEMADD_SIZE_8BIT, r, BQ_REG_COUNT, 50) != HAL_OK) {
        present = false;
        return false;
    }
    present = true;

    bq->vsysmin_mv = (uint16_t)(2500u + (r[REG_VSYSMIN] & 0x3Fu) * 250u);
    bq->vreg_mv    = (uint16_t)((u16(r, REG_VREG) & 0x07FFu) * 10u);
    bq->ichg_ma    = (uint16_t)((u16(r, REG_ICHG) & 0x01FFu) * 10u);
    bq->vindpm_mv  = (uint16_t)(r[REG_VINDPM] * 100u);
    bq->iindpm_ma  = (uint16_t)((u16(r, REG_IINDPM) & 0x01FFu) * 10u);
    bq->cells      = (uint8_t)((r[REG_RECHG] >> 6) + 1u);
    bq->en_chg     = (r[REG_CTRL0] & 0x20u) != 0;
    bq->en_hiz     = (r[REG_CTRL0] & 0x04u) != 0;
    bq->mppt_on    = (r[REG_MPPT] & 0x01u) != 0;

    uint8_t s0 = r[REG_STAT0], s1 = r[REG_STAT1];
    bq->iindpm_reg   = (s0 & 0x80u) != 0;
    bq->vindpm_reg   = (s0 & 0x40u) != 0;
    bq->power_good   = (s0 & 0x08u) != 0;
    bq->ac2_present  = (s0 & 0x04u) != 0;
    bq->ac1_present  = (s0 & 0x02u) != 0;
    bq->vbus_present = (s0 & 0x01u) != 0;
    bq->chg_stat     = (uint8_t)(s1 >> 5);
    bq->vbus_stat    = (uint8_t)((s1 >> 1) & 0x0Fu);
    bq->treg         = (r[REG_STAT2] & 0x04u) != 0;
    bq->vbat_present = (r[REG_STAT2] & 0x01u) != 0;
    bq->vsys_min_reg = (r[REG_STAT3] & 0x10u) != 0;

    bq->ibus_ma  = (int16_t)u16(r, REG_IBUS_ADC);
    bq->ibat_ma  = (int16_t)u16(r, REG_IBAT_ADC);
    bq->vbus_mv  = u16(r, REG_VBUS_ADC);
    bq->vac1_mv  = u16(r, REG_VAC1_ADC);
    bq->vac2_mv  = u16(r, REG_VAC2_ADC);
    bq->vbat_mv  = u16(r, REG_VBAT_ADC);
    bq->vsys_mv  = u16(r, REG_VSYS_ADC);
    bq->ts_pct_x10 = (uint16_t)(((uint32_t)u16(r, REG_TS_ADC) * 1000u + 512u) / 1024u);
    bq->tdie_x10 = (int16_t)((int16_t)u16(r, REG_TDIE_ADC) * 5);

    memcpy(bq->flags, &r[REG_FLAG0], sizeof(bq->flags));
    return true;
}

bool bq_mppt_task(const bq_info_t *bq)
{
#if CFG_BQ_MPPT
    static uint32_t t_try;
    /* Dieu kien cua chip: co VBUS va VBAT >= VSYSMIN (VSYS_STAT = 0), neu khong bit tu ve 0 */
    if (bq->mppt_on || !bq->vbus_present || bq->vsys_min_reg) { return false; }
    if (t_try != 0 && (HAL_GetTick() - t_try) < 10000u) { return false; }   /* chip tu choi: 10 s thu lai */
    t_try = HAL_GetTick();
    return update_bits(REG_MPPT, 0x01u, 0x01u);
#else
    (void)bq;
    return false;
#endif
}

const char *bq_chg_str(uint8_t chg_stat)
{
    static const char *const s[8] = {
        "KHONG SAC", "TRICKLE", "PRE-CHARGE", "SAC CC", "SAC CV", "?", "TOP-OFF", "DA DAY",
    };
    return s[chg_stat & 7u];
}

const char *bq_vbus_str(uint8_t vbus_stat)
{
    switch (vbus_stat) {
    case 0x0: return "khong co";
    case 0x1: return "USB SDP";
    case 0x2: return "USB CDP";
    case 0x3: return "USB DCP";
    case 0x4: return "HVDCP";
    case 0x5: return "adapter";
    case 0x6: return "adapter NS";
    case 0x7: return "OTG";
    case 0x8: return "khong dat";
    case 0xB: return "VBUS truc tiep";
    case 0xC: return "backup";
    default:  return "?";
    }
}

int bq_fault_str(const bq_info_t *bq, char *buf, size_t len)
{
    static const struct { uint8_t reg, mask; const char *name; } faults[] = {
        { REG_FAULT0, 0x40u, "VBUS OVP" },  { REG_FAULT0, 0x20u, "VBAT OVP" },
        { REG_FAULT0, 0x10u, "IBUS OCP" },  { REG_FAULT0, 0x08u, "IBAT OCP" },
        { REG_FAULT0, 0x04u, "CONV OCP" },  { REG_FAULT0, 0x02u, "VAC2 OVP" },
        { REG_FAULT0, 0x01u, "VAC1 OVP" },  { REG_FAULT1, 0x80u, "SYS NGAN MACH" },
        { REG_FAULT1, 0x40u, "SYS OVP" },   { REG_FAULT1, 0x20u, "OTG OVP" },
        { REG_FAULT1, 0x10u, "OTG UVP" },   { REG_FAULT1, 0x04u, "QUA NHIET" },
        { REG_STAT3,  0x08u, "HET GIO SAC" }, { REG_STAT3, 0x04u, "HET GIO TRICKLE" },
        { REG_STAT3,  0x02u, "HET GIO PRE" }, { REG_STAT4, 0x08u, "TS LANH" },
        { REG_STAT4,  0x01u, "TS NONG" },   { REG_STAT0, 0x20u, "WATCHDOG" },
    };
    int n = 0;
    size_t pos = 0;
    if (len) { buf[0] = '\0'; }
    for (unsigned i = 0; i < sizeof(faults) / sizeof(faults[0]); i++) {
        if ((bq->reg[faults[i].reg] & faults[i].mask) == 0) { continue; }
        int w = snprintf(buf + pos, len > pos ? len - pos : 0, "%s%s", n ? "," : "", faults[i].name);
        if (w > 0) { pos += (size_t)w; }
        if (pos >= len) { pos = len ? len - 1 : 0; }
        n++;
    }
    return n;
}

uint8_t bq_battery_percent(const bq_info_t *bq)
{
    /* Dien ap mo mach moi cell -> % (gan dung) */
#if CFG_BAT_CHEMISTRY == 1
    static const uint16_t mv[]  = { 2800, 3000, 3200, 3250, 3300, 3330, 3350, 3400, 3600 };
    static const uint8_t  pct[] = {    0,    5,   15,   30,   60,   80,   90,   97,  100 };
#else
    static const uint16_t mv[]  = { 3300, 3500, 3600, 3700, 3750, 3800, 3850, 3900, 4000, 4100, 4200 };
    static const uint8_t  pct[] = {    0,    5,   10,   25,   40,   50,   60,   68,   80,   90,  100 };
#endif
    const unsigned n = sizeof(mv) / sizeof(mv[0]);
    if (!bq->vbat_present || bq->cells == 0) { return 0; }
    uint16_t c = (uint16_t)(bq->vbat_mv / bq->cells);
    if (c <= mv[0]) { return 0; }
    if (c >= mv[n - 1]) { return 100; }
    for (unsigned i = 1; i < n; i++) {
        if (c <= mv[i]) {
            return (uint8_t)(pct[i - 1] + (uint32_t)(c - mv[i - 1]) * (pct[i] - pct[i - 1]) / (mv[i] - mv[i - 1]));
        }
    }
    return 100;
}

void bq_dump(void)
{
    bq_info_t b;
    char f[96];

    if (!bq_read(&b)) {
        printf("BQ25798: khong tra loi I2C1 (0x6B)\n");
        return;
    }
    uint8_t part = b.reg[REG_PART_INFO];
    printf("BQ25798: part 0x%02X (PN %u%s, rev %u), %u cell, %s, EN_CHG %u CE %u HIZ %u TS_IGNORE %u WD %u\n",
           part, (part >> 3) & 7u, ((part >> 3) & 7u) == 3u ? " = BQ25798" : " ?", part & 7u, b.cells,
           bq_chg_str(b.chg_stat), b.en_chg,
           HAL_GPIO_ReadPin(BQ_CE_GPIO_Port, BQ_CE_Pin) == GPIO_PIN_SET, b.en_hiz,
           b.reg[REG_NTC1] & 1u, b.reg[REG_CTRL1] & 7u);
    printf("  Dat : VREG %u mV, ICHG %u mA, VSYSMIN %u mV, VINDPM %u mV, IINDPM %u mA\n",
           b.vreg_mv, b.ichg_ma, b.vsysmin_mv, b.vindpm_mv, b.iindpm_ma);
    {
        static const char *const pct[8] = { "56.25", "62.5", "68.75", "75", "81.25", "87.5", "93.75", "100" };
        static const char *const dly[4] = { "50ms", "300ms", "2s", "5s" };
        static const char *const rate[4] = { "30s", "2ph", "10ph", "30ph" };
        uint8_t m = b.reg[REG_MPPT];
        printf("  MPPT: %s (REG15 0x%02X: VINDPM = %s%% VOC, do sau %s, moi %s), ICO %s\n",
               b.mppt_on ? "BAT" : "tat", m, pct[m >> 5], dly[(m >> 3) & 3u], rate[(m >> 1) & 3u],
               (b.reg[REG_CTRL0] & 0x10u) ? "bat" : "tat");
    }
    printf("  Vao : VBUS %u mV (%s, %s%s), VAC1 %u VAC2 %u mV, IBUS %d mA%s%s\n",
           b.vbus_mv, b.vbus_present ? "co" : "khong", bq_vbus_str(b.vbus_stat),
           b.power_good ? ", PG" : "", b.vac1_mv, b.vac2_mv, b.ibus_ma,
           b.iindpm_reg ? " [IINDPM]" : "", b.vindpm_reg ? " [VINDPM]" : "");
    printf("  Pin : VBAT %u mV (~%u%%%s), IBAT %d mA, VSYS %u mV%s\n",
           b.vbat_mv, bq_battery_percent(&b), b.vbat_present ? "" : ", KHONG CO PIN", b.ibat_ma,
           b.vsys_mv, b.vsys_min_reg ? " [VSYSMIN]" : "");
    printf("  Nhiet: TDIE %d.%d C%s, TS %u.%u%% REGN\n", b.tdie_x10 / 10, (b.tdie_x10 < 0 ? -b.tdie_x10 : b.tdie_x10) % 10,
           b.treg ? " [TREG]" : "", b.ts_pct_x10 / 10u, b.ts_pct_x10 % 10u);
    bq_fault_str(&b, f, sizeof(f));
    printf("  Loi : %s | STAT 1B-1F %02X %02X %02X %02X %02X, FAULT %02X %02X, FLAG %02X %02X %02X %02X %02X %02X\n",
           f[0] ? f : "khong", b.reg[0x1B], b.reg[0x1C], b.reg[0x1D], b.reg[0x1E], b.reg[0x1F],
           b.reg[0x20], b.reg[0x21], b.flags[0], b.flags[1], b.flags[2], b.flags[3], b.flags[4], b.flags[5]);
}
