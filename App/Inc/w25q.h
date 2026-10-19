/*
 * Flash SPI W25Q32 (4 MB) tren SPI1: SCK PA5, MISO PA6, MOSI PA7, CS PA4.
 *
 * LUU Y: tren so do top-level, cong FLASH-CS cua sheet W25Q32 KHONG noi
 * vao chan MCU nao (chi co R44 keo len 3.3V). Firmware gia dinh CS = PA4
 * (SPI1_NSS). Neu mach that chua noi thi phai cau day PA4 -> chan 1 IC8,
 * neu khong w25q_init() se bao khong tim thay chip.
 */
#ifndef W25Q_H
#define W25Q_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define W25Q_PAGE_SIZE    256u
#define W25Q_SECTOR_SIZE  4096u

bool     w25q_init(void);               /* true neu doc duoc JEDEC ID hop le */
uint32_t w25q_jedec_id(void);           /* W25Q32: 0xEF4016 */
uint32_t w25q_size(void);               /* byte, suy ra tu JEDEC ID */
void     w25q_read(uint32_t addr, void *buf, size_t len);
void     w25q_write(uint32_t addr, const void *buf, size_t len);   /* vung phai da xoa */
void     w25q_erase_sector(uint32_t addr);                         /* 4 KB */

#endif
