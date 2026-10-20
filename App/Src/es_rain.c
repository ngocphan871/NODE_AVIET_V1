#include "es_rain.h"
#include "config.h"

mb_status_t rain_read(bool *raining)
{
    uint16_t v;
    mb_status_t st = mb_read_regs(CFG_RAIN_ADDR, 3, RAIN_REG_STATE, 1, &v);
    if (st == MB_OK) { *raining = (v != 0); }
    return st;
}

mb_status_t rain_read_cfg(rain_cfg_t *cfg)
{
    uint16_t v[5];
    mb_status_t st = mb_read_regs(CFG_RAIN_ADDR, 3, RAIN_REG_HEAT_MAX, 5, v);
    if (st != MB_OK) { return st; }
    cfg->heat_max_x10 = (int16_t)v[0];
    cfg->heat_min_x10 = (int16_t)v[1];      /* co the am (-30..70 C) */
    cfg->heat_hys_x10 = v[2];
    cfg->delay_s     = v[3];
    cfg->sensitivity = v[4];
    return MB_OK;
}

mb_status_t rain_write(uint16_t reg, int16_t value)
{
    if (reg < RAIN_REG_HEAT_MAX || reg > RAIN_REG_SENS) { return MB_ERR_PARAM; }
    return mb_write_reg(CFG_RAIN_ADDR, reg, (uint16_t)value);
}
