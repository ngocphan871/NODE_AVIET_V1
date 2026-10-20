/*
 * Cam bien nhiet do / do am SHT30 (dau do SHT65) tren I2C2 qua CN5:
 *   1 5V, 2 GND, 5 SCL (PB10), 6 SDA (PB11); R13/R15 4.7k keo len 3.3V tren board.
 * Dia chi 0x44 (ADDR thap) hoac 0x45 (ADDR cao) - driver tu do ca hai.
 * Lenh, CRC va cong thuc theo sample code Sensirion SHT3x (sht3x.c):
 *   do 1 lan khong keo dai xung clock 0x2400 (do lap lai cao, toi da 15.5 ms),
 *   CRC-8 da thuc 0x31 khoi tao 0xFF, T = -45 + 175*raw/65535, RH = 100*raw/65535.
 */
#ifndef SHT3X_H
#define SHT3X_H

#include <stdbool.h>
#include <stdint.h>

bool    sht3x_init(void);                           /* tim cam bien, soft reset; true neu thay */
bool    sht3x_present(void);
uint8_t sht3x_addr(void);                           /* 0x44 / 0x45, 0 = chua thay */
/* Do 1 lan (chan ~16 ms). t_x10: 0.1 do C, rh_x10: 0.1 %RH. false neu loi I2C/CRC. */
bool    sht3x_read(int16_t *t_x10, uint16_t *rh_x10);
bool    sht3x_read_status(uint16_t *status);        /* thanh ghi trang thai 0xF32D */
bool    sht3x_heater(bool on);                      /* bo suoi trong chip (lam kho cam bien) */

#endif
