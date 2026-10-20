#include "sht3x.h"
#include "main.h"

extern I2C_HandleTypeDef hi2c2;

/* Lenh (sample Sensirion sht3x.h) */
#define CMD_MEAS_POLLING_H  0x2400      /* do 1 lan, khong keo clock, lap lai cao */
#define CMD_READ_STATUS     0xF32D
#define CMD_CLEAR_STATUS    0x3041
#define CMD_SOFT_RESET      0x30A2
#define CMD_HEATER_ENABLE   0x306D
#define CMD_HEATER_DISABLE  0x3066

#define MEAS_TIME_MS        16u         /* toi da 15.5 ms o che do lap lai cao */

static uint8_t addr;                    /* 7 bit, 0 = chua thay */

static uint8_t crc8(const uint8_t *d, int n)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static bool cmd(uint8_t a, uint16_t c)
{
    uint8_t b[2] = { (uint8_t)(c >> 8), (uint8_t)c };
    return HAL_I2C_Master_Transmit(&hi2c2, (uint16_t)(a << 1), b, 2, 10) == HAL_OK;
}

/* Doc n tu 16 bit, moi tu kem 1 byte CRC */
static bool read_words(uint8_t a, uint16_t *w, int n)
{
    uint8_t b[6];
    if (n > 2) { return false; }
    if (HAL_I2C_Master_Receive(&hi2c2, (uint16_t)(a << 1), b, (uint16_t)(3 * n), 10) != HAL_OK) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (crc8(&b[3 * i], 2) != b[3 * i + 2]) { return false; }
        w[i] = (uint16_t)((b[3 * i] << 8) | b[3 * i + 1]);
    }
    return true;
}

bool sht3x_init(void)
{
    static const uint8_t cand[2] = { 0x44, 0x45 };
    addr = 0;
    for (int i = 0; i < 2; i++) {
        uint16_t st;
        if (!cmd(cand[i], CMD_SOFT_RESET)) { continue; }
        HAL_Delay(2);                                   /* soft reset toi da 1.5 ms */
        if (cmd(cand[i], CMD_READ_STATUS) && read_words(cand[i], &st, 1)) {
            addr = cand[i];
            cmd(addr, CMD_CLEAR_STATUS);
            return true;
        }
    }
    return false;
}

bool sht3x_present(void)
{
    return addr != 0;
}

uint8_t sht3x_addr(void)
{
    return addr;
}

bool sht3x_read(int16_t *t_x10, uint16_t *rh_x10)
{
    uint16_t w[2];
    if (addr == 0 || !cmd(addr, CMD_MEAS_POLLING_H)) { return false; }
    HAL_Delay(MEAS_TIME_MS);
    if (!read_words(addr, w, 2)) { return false; }
    /* T = -45 + 175 * raw / 65535 ; RH = 100 * raw / 65535 (lam tron, don vi 0.1) */
    *t_x10  = (int16_t)(-450 + (int32_t)((1750u * (uint32_t)w[0] + 32767u) / 65535u));
    *rh_x10 = (uint16_t)((1000u * (uint32_t)w[1] + 32767u) / 65535u);
    return true;
}

bool sht3x_read_status(uint16_t *status)
{
    return addr != 0 && cmd(addr, CMD_READ_STATUS) && read_words(addr, status, 1);
}

bool sht3x_heater(bool on)
{
    return addr != 0 && cmd(addr, on ? CMD_HEATER_ENABLE : CMD_HEATER_DISABLE);
}
