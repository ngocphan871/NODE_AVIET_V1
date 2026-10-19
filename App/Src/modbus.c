#include "modbus.h"
#include "config.h"
#include "serial.h"
#include "main.h"

static uint8_t last_exc;

static uint16_t crc16(const uint8_t *p, uint16_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 1u) ? (uint16_t)((crc >> 1) ^ 0xA001u) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static void send_frame(uint8_t *f, uint16_t len)
{
    uint16_t crc = crc16(f, len);
    f[len++] = (uint8_t)(crc & 0xFF);
    f[len++] = (uint8_t)(crc >> 8);

    ser_flush_rx(SER_485);
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_SET);
    ser_write(SER_485, f, len);
    /* Doi byte cuoi ra khoi thanh ghi dich roi moi nha bus */
    UART_HandleTypeDef *h = ser_handle(SER_485);
    uint32_t t0 = HAL_GetTick();
    while (!__HAL_UART_GET_FLAG(h, UART_FLAG_TC) && (HAL_GetTick() - t0) < 20) { }
    HAL_GPIO_WritePin(RS485_DE_GPIO_Port, RS485_DE_Pin, GPIO_PIN_RESET);
}

/* Nhan dung 'want' byte, hoac dung som neu la khung exception (5 byte) */
static mb_status_t recv_frame(uint8_t *buf, uint16_t want, uint8_t slave, uint8_t func)
{
    uint16_t n = 0;
    uint32_t t0 = HAL_GetTick();
    while (n < want) {
        int c = ser_getc(SER_485);
        if (c < 0) {
            if ((HAL_GetTick() - t0) > CFG_MB_TIMEOUT_MS) {
                return n ? MB_ERR_FRAME : MB_ERR_TIMEOUT;
            }
            continue;
        }
        buf[n++] = (uint8_t)c;
        if (n == 2 && buf[1] == (uint8_t)(func | 0x80u)) {
            want = 5;
        }
    }
    if (crc16(buf, (uint16_t)(n - 2)) != (uint16_t)(buf[n - 2] | (buf[n - 1] << 8))) {
        return MB_ERR_CRC;
    }
    if (buf[0] != slave) { return MB_ERR_FRAME; }
    if (buf[1] == (uint8_t)(func | 0x80u)) {
        last_exc = buf[2];
        return MB_ERR_EXCEPTION;
    }
    if (buf[1] != func) { return MB_ERR_FRAME; }
    return MB_OK;
}

mb_status_t mb_read_regs(uint8_t slave, uint8_t func, uint16_t addr,
                         uint16_t count, uint16_t *out)
{
    uint8_t f[8];
    uint8_t r[5 + 2 * MB_MAX_REGS];

    if ((func != 3 && func != 4) || count == 0 || count > MB_MAX_REGS) {
        return MB_ERR_PARAM;
    }
    f[0] = slave;
    f[1] = func;
    f[2] = (uint8_t)(addr >> 8);
    f[3] = (uint8_t)addr;
    f[4] = (uint8_t)(count >> 8);
    f[5] = (uint8_t)count;
    send_frame(f, 6);

    mb_status_t st = recv_frame(r, (uint16_t)(5 + 2 * count), slave, func);
    if (st != MB_OK) { return st; }
    if (r[2] != 2 * count) { return MB_ERR_FRAME; }
    for (uint16_t i = 0; i < count; i++) {
        out[i] = (uint16_t)((r[3 + 2 * i] << 8) | r[4 + 2 * i]);
    }
    return MB_OK;
}

mb_status_t mb_write_reg(uint8_t slave, uint16_t addr, uint16_t value)
{
    uint8_t f[8];
    uint8_t r[8];

    f[0] = slave;
    f[1] = 6;
    f[2] = (uint8_t)(addr >> 8);
    f[3] = (uint8_t)addr;
    f[4] = (uint8_t)(value >> 8);
    f[5] = (uint8_t)value;
    send_frame(f, 6);
    return recv_frame(r, 8, slave, 6);   /* slave tra lai nguyen khung */
}

uint8_t mb_last_exception(void)
{
    return last_exc;
}

const char *mb_status_str(mb_status_t s)
{
    switch (s) {
    case MB_OK:            return "OK";
    case MB_ERR_TIMEOUT:   return "timeout";
    case MB_ERR_CRC:       return "sai CRC";
    case MB_ERR_FRAME:     return "sai khung";
    case MB_ERR_EXCEPTION: return "exception";
    default:               return "sai tham so";
    }
}
