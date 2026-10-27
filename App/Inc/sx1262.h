/*
 * LoRa module AcSiP S68F (chip Semtech SX1268, 433..510 MHz; so do ghi "S62F") tren SPI3:
 *   SCK PC10, MISO PC11, MOSI PC12, NSS PA15, NRST PD1,
 *   BUSY PA8, DIO1 PA9 (ngat TX/RX done), DIO2 PD0 (khong dung),
 *   RF_VCC1 PC9 / RF_VCC2 PA10 = chuyen mach anten, MCU dieu khien ca 2 chan ("Mode A"
 *   trong S68F Application Note 3.6): TX = VC1 1 / VC2 0, RX = VC1 0 / VC2 1.
 * TCXO 32 MHz trong module, cap boi DIO3 o 3.0 V (AN 4.4: tcxoVoltage = 0x06).
 * Module co cuon 15uH VREG-DCC_SW (AN muc 2) nen bat DC-DC.
 * SX1268 cung bo lenh voi SX1262 nen ten ham giu sx1262_*.
 */
#ifndef SX1262_H
#define SX1262_H

#include <stdbool.h>
#include <stdint.h>

#define SX1262_MAX_PAYLOAD 255

typedef struct {
    uint8_t len;
    int16_t rssi;       /* dBm */
    int8_t  snr;        /* dB */
    uint8_t data[SX1262_MAX_PAYLOAD];
} sx1262_packet_t;

bool sx1262_init(void);                 /* true neu chip tra loi va cau hinh xong */
uint16_t sx1262_device_errors(void);    /* GetDeviceErrors, 0 = khong loi */
void sx1262_start_rx(void);             /* nhan lien tuc */
bool sx1262_poll_rx(sx1262_packet_t *pkt);  /* true neu co goi moi (CRC dung) */
bool sx1262_send(const uint8_t *data, uint8_t len, uint32_t timeout_ms);  /* blocking, xong quay ve RX */

#endif
