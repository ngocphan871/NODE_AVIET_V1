#include "sx1262.h"
#include "config.h"
#include "main.h"
#include <string.h>

extern SPI_HandleTypeDef hspi3;

/* Ma lenh (SX1261/2 datasheet muc 11) */
#define OP_SET_STANDBY          0x80
#define OP_SET_RX               0x82
#define OP_SET_TX               0x83
#define OP_SET_RF_FREQ          0x86
#define OP_CALIBRATE            0x89
#define OP_SET_PACKET_TYPE      0x8A
#define OP_SET_MOD_PARAMS       0x8B
#define OP_SET_PKT_PARAMS       0x8C
#define OP_SET_TX_PARAMS        0x8E
#define OP_SET_BUF_BASE         0x8F
#define OP_SET_PA_CONFIG        0x95
#define OP_SET_REGULATOR        0x96
#define OP_SET_DIO3_TCXO        0x97
#define OP_CALIBRATE_IMAGE      0x98
#define OP_SET_DIO_IRQ          0x08
#define OP_CLR_IRQ              0x02
#define OP_CLR_DEV_ERRORS       0x07
#define OP_WRITE_REG            0x0D
#define OP_READ_REG             0x1D
#define OP_WRITE_BUF            0x0E
#define OP_READ_BUF             0x1E
#define OP_GET_IRQ              0x12
#define OP_GET_RX_BUF_STATUS    0x13
#define OP_GET_PKT_STATUS       0x14
#define OP_GET_DEV_ERRORS       0x17

#define REG_SYNC_WORD           0x0740
#define REG_TX_CLAMP            0x08D8
#define REG_OCP                 0x08E7

#define IRQ_TX_DONE             0x0001
#define IRQ_RX_DONE             0x0002
#define IRQ_HEADER_ERR          0x0020
#define IRQ_CRC_ERR             0x0040
#define IRQ_TIMEOUT             0x0200
#define IRQ_ALL                 0x03FF

static bool ready;

static void nss(int level)
{
    HAL_GPIO_WritePin(RF_NSS_GPIO_Port, RF_NSS_Pin, level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static bool wait_busy(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_GPIO_ReadPin(RF_BUSY_GPIO_Port, RF_BUSY_Pin) == GPIO_PIN_SET) {
        if ((HAL_GetTick() - t0) > timeout_ms) { return false; }
    }
    return true;
}

static void cmd(uint8_t op, const uint8_t *p, uint8_t n)
{
    wait_busy(100);
    nss(0);
    HAL_SPI_Transmit(&hspi3, &op, 1, 10);
    if (n) { HAL_SPI_Transmit(&hspi3, (uint8_t *)p, n, 10); }
    nss(1);
}

/* Lenh doc: op, [tham so], 1 byte status, roi n byte du lieu */
static void cmd_read(uint8_t op, const uint8_t *args, uint8_t nargs, uint8_t *out, uint8_t n)
{
    uint8_t nop = 0;
    wait_busy(100);
    nss(0);
    HAL_SPI_Transmit(&hspi3, &op, 1, 10);
    if (nargs) { HAL_SPI_Transmit(&hspi3, (uint8_t *)args, nargs, 10); }
    HAL_SPI_Transmit(&hspi3, &nop, 1, 10);       /* byte status */
    memset(out, 0, n);
    HAL_SPI_Receive(&hspi3, out, n, 10);
    nss(1);
}

static void write_reg(uint16_t addr, const uint8_t *d, uint8_t n)
{
    uint8_t b[2 + 8];
    b[0] = (uint8_t)(addr >> 8);
    b[1] = (uint8_t)addr;
    memcpy(&b[2], d, n);
    cmd(OP_WRITE_REG, b, (uint8_t)(2 + n));
}

static uint8_t read_reg(uint16_t addr)
{
    uint8_t a[2] = { (uint8_t)(addr >> 8), (uint8_t)addr }, v;
    cmd_read(OP_READ_REG, a, 2, &v, 1);
    return v;
}

static void cmd1(uint8_t op, uint8_t v)
{
    cmd(op, &v, 1);
}

static void rf_switch_tx(bool tx)
{
    HAL_GPIO_WritePin(RF_VCC1_GPIO_Port, RF_VCC1_Pin,
                      (tx ? CFG_LORA_TX_VC1 : CFG_LORA_RX_VC1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(RF_VCC2_GPIO_Port, RF_VCC2_Pin,
                      (tx ? CFG_LORA_TX_VC2 : CFG_LORA_RX_VC2) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint16_t get_irq(void)
{
    uint8_t r[2];
    cmd_read(OP_GET_IRQ, NULL, 0, r, 2);
    return (uint16_t)((r[0] << 8) | r[1]);
}

static void clear_irq(uint16_t m)
{
    uint8_t b[2] = { (uint8_t)(m >> 8), (uint8_t)m };
    cmd(OP_CLR_IRQ, b, 2);
}

uint16_t sx1262_device_errors(void)
{
    uint8_t r[2];
    cmd_read(OP_GET_DEV_ERRORS, NULL, 0, r, 2);
    return (uint16_t)((r[0] << 8) | r[1]);
}

static void set_packet_params(uint8_t payload_len)
{
    uint8_t p[6] = {
        (uint8_t)(CFG_LORA_PREAMBLE >> 8), (uint8_t)CFG_LORA_PREAMBLE,
        0x00,               /* header tuong minh */
        payload_len,
        0x01,               /* CRC bat */
        0x00,               /* IQ chuan */
    };
    cmd(OP_SET_PKT_PARAMS, p, 6);
}

static void calibrate_image(uint32_t f)
{
    uint8_t b[2];
    if      (f > 900000000u) { b[0] = 0xE1; b[1] = 0xE9; }
    else if (f > 850000000u) { b[0] = 0xD7; b[1] = 0xDB; }
    else if (f > 770000000u) { b[0] = 0xC1; b[1] = 0xC5; }
    else if (f > 460000000u) { b[0] = 0x75; b[1] = 0x81; }
    else                     { b[0] = 0x6B; b[1] = 0x6F; }
    cmd(OP_CALIBRATE_IMAGE, b, 2);
}

bool sx1262_init(void)
{
    ready = false;
    rf_switch_tx(false);

    /* Reset cung */
    HAL_GPIO_WritePin(RF_NRST_GPIO_Port, RF_NRST_Pin, GPIO_PIN_RESET);
    HAL_Delay(2);
    HAL_GPIO_WritePin(RF_NRST_GPIO_Port, RF_NRST_Pin, GPIO_PIN_SET);
    HAL_Delay(10);
    if (!wait_busy(100)) { return false; }

    cmd1(OP_SET_STANDBY, 0x00);                  /* STDBY_RC */

    /* Kiem tra SPI: ghi/doc lai thanh ghi sync word */
    uint8_t probe[2] = { 0x12, 0x34 };
    write_reg(REG_SYNC_WORD, probe, 2);
    if (read_reg(REG_SYNC_WORD) != 0x12 || read_reg(REG_SYNC_WORD + 1) != 0x34) {
        return false;
    }

#if CFG_LORA_USE_TCXO
    {   /* DIO3 cap 1.8 V cho TCXO, cho 5 ms */
        uint8_t t[4] = { 0x02, 0x00, 0x01, 0x40 };
        cmd(OP_SET_DIO3_TCXO, t, 4);
    }
#endif
    cmd1(OP_SET_REGULATOR, 0x01);                /* DC-DC */
    cmd1(OP_CALIBRATE, 0x7F);
    HAL_Delay(5);
    wait_busy(100);
    cmd(OP_CLR_DEV_ERRORS, (const uint8_t[]){ 0x00, 0x00 }, 2);

    cmd1(OP_SET_PACKET_TYPE, 0x01);              /* LoRa */

    uint32_t frf = (uint32_t)(((uint64_t)CFG_LORA_FREQ_HZ << 25) / 32000000u);
    calibrate_image(CFG_LORA_FREQ_HZ);
    uint8_t f[4] = { (uint8_t)(frf >> 24), (uint8_t)(frf >> 16), (uint8_t)(frf >> 8), (uint8_t)frf };
    cmd(OP_SET_RF_FREQ, f, 4);

    /* PA cho +22 dBm (SX1262) */
    cmd(OP_SET_PA_CONFIG, (const uint8_t[]){ 0x04, 0x07, 0x00, 0x01 }, 4);
    uint8_t ocp = 0x38;                          /* 140 mA */
    write_reg(REG_OCP, &ocp, 1);
    uint8_t txp[2] = { (uint8_t)(int8_t)CFG_LORA_TX_DBM, 0x04 };   /* ramp 200 us */
    cmd(OP_SET_TX_PARAMS, txp, 2);

    /* Errata 15.2: anten lech tro khang -> tang TxClamp */
    uint8_t clamp = read_reg(REG_TX_CLAMP) | 0x1E;
    write_reg(REG_TX_CLAMP, &clamp, 1);

    cmd(OP_SET_BUF_BASE, (const uint8_t[]){ 0x00, 0x00 }, 2);

    uint8_t bw = (CFG_LORA_BW >= 500) ? 0x06 : (CFG_LORA_BW >= 250) ? 0x05 : 0x04;
    /* LDRO bat khi thoi gian 1 symbol >= 16 ms */
    uint32_t sym_us = (1000u << CFG_LORA_SF) / CFG_LORA_BW;
    uint8_t mod[4] = { CFG_LORA_SF, bw, (uint8_t)(CFG_LORA_CR - 4), (uint8_t)(sym_us >= 16000u) };
    cmd(OP_SET_MOD_PARAMS, mod, 4);

    set_packet_params(SX1262_MAX_PAYLOAD);

    uint8_t sw[2];
    sw[0] = CFG_LORA_SYNC_PUBLIC ? 0x34 : 0x14;
    sw[1] = CFG_LORA_SYNC_PUBLIC ? 0x44 : 0x24;
    write_reg(REG_SYNC_WORD, sw, 2);

    /* Tat ca IRQ quan tam deu ra DIO1 */
    uint16_t m = IRQ_TX_DONE | IRQ_RX_DONE | IRQ_HEADER_ERR | IRQ_CRC_ERR | IRQ_TIMEOUT;
    uint8_t irq[8] = { (uint8_t)(m >> 8), (uint8_t)m, (uint8_t)(m >> 8), (uint8_t)m, 0, 0, 0, 0 };
    cmd(OP_SET_DIO_IRQ, irq, 8);
    clear_irq(IRQ_ALL);

    ready = true;
    return true;
}

void sx1262_start_rx(void)
{
    if (!ready) { return; }
    set_packet_params(SX1262_MAX_PAYLOAD);
    clear_irq(IRQ_ALL);
    rf_switch_tx(false);
    cmd(OP_SET_RX, (const uint8_t[]){ 0xFF, 0xFF, 0xFF }, 3);   /* lien tuc */
}

bool sx1262_poll_rx(sx1262_packet_t *pkt)
{
    if (!ready) { return false; }
    if (HAL_GPIO_ReadPin(RF_DIO1_GPIO_Port, RF_DIO1_Pin) == GPIO_PIN_RESET) { return false; }

    uint16_t irq = get_irq();
    clear_irq(IRQ_ALL);
    if (!(irq & IRQ_RX_DONE) || (irq & (IRQ_CRC_ERR | IRQ_HEADER_ERR))) {
        return false;                            /* van o che do RX lien tuc */
    }

    uint8_t st[2];
    cmd_read(OP_GET_RX_BUF_STATUS, NULL, 0, st, 2);
    pkt->len = st[0];
    cmd_read(OP_READ_BUF, &st[1], 1, pkt->data, st[0]);

    uint8_t ps[3];
    cmd_read(OP_GET_PKT_STATUS, NULL, 0, ps, 3);
    pkt->rssi = (int16_t)(-(int16_t)ps[0] / 2);
    pkt->snr = (int8_t)((int8_t)ps[1] / 4);
    return true;
}

bool sx1262_send(const uint8_t *data, uint8_t len, uint32_t timeout_ms)
{
    if (!ready) { return false; }
    cmd1(OP_SET_STANDBY, 0x00);
    set_packet_params(len);

    uint8_t off = 0;
    wait_busy(100);
    nss(0);
    uint8_t h[2] = { OP_WRITE_BUF, off };
    HAL_SPI_Transmit(&hspi3, h, 2, 10);
    HAL_SPI_Transmit(&hspi3, (uint8_t *)data, len, 100);
    nss(1);

    clear_irq(IRQ_ALL);
    rf_switch_tx(true);
    cmd(OP_SET_TX, (const uint8_t[]){ 0x00, 0x00, 0x00 }, 3);

    bool ok = false;
    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < timeout_ms) {
        if (HAL_GPIO_ReadPin(RF_DIO1_GPIO_Port, RF_DIO1_Pin) == GPIO_PIN_SET) {
            ok = (get_irq() & IRQ_TX_DONE) != 0;
            break;
        }
    }
    sx1262_start_rx();
    return ok;
}
