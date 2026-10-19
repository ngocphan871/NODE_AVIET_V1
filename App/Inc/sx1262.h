/*
 * LoRa SX1262 (module S62F, sheet MODULE_RF) tren SPI3:
 *   SCK PC10, MISO PC11, MOSI PC12, NSS PA15, NRST PD1,
 *   BUSY PA8, DIO1 PA9 (ngat TX/RX done), DIO2 PD0,
 *   RF_VCC1 PC9 / RF_VCC2 PA10 = chuyen mach anten TX/RX.
 * Module co cuon 15uH VREG-DCC_SW nen bat DC-DC.
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
