/*
 * Modbus RTU master tren RS485 (UART4 + MAX3485, DE/RE# = PA2).
 */
#ifndef MODBUS_H
#define MODBUS_H

#include <stdint.h>

#define MB_MAX_REGS 32

typedef enum {
    MB_OK = 0,
    MB_ERR_TIMEOUT,     /* slave khong tra loi */
    MB_ERR_CRC,
    MB_ERR_FRAME,       /* sai dia chi / ma lenh / do dai */
    MB_ERR_EXCEPTION,   /* slave tra ma loi, xem mb_last_exception() */
    MB_ERR_PARAM,
} mb_status_t;

/* Dat toc do UART4 (2400/4800/9600...). Goi TRUOC ser_init() vi phai khoi tao lai UART. */
void        mb_init(uint32_t baud);
uint32_t    mb_baud(void);

/* func = 3 (holding) hoac 4 (input) */
mb_status_t mb_read_regs(uint8_t slave, uint8_t func, uint16_t addr,
                         uint16_t count, uint16_t *out);
mb_status_t mb_write_reg(uint8_t slave, uint16_t addr, uint16_t value);
uint8_t     mb_last_exception(void);
const char *mb_status_str(mb_status_t s);

#endif
