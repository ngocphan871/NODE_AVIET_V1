/*
 * Man hinh TFT SPI qua CN8 (ZH1.5-8P), SPI2 chi phat:
 *   SCK PB13, MOSI PB15, CS PD8, DC PB14, RESET PB12, LED PD9 (den nen).
 * Chon chip dieu khien trong config.h (CFG_TFT_CONTROLLER).
 */
#ifndef TFT_H
#define TFT_H

#include <stdint.h>

#define TFT_ST7789   1
#define TFT_ILI9341  2
#define TFT_ST7735   3

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define TFT_BLACK   0x0000
#define TFT_WHITE   0xFFFF
#define TFT_RED     0xF800
#define TFT_GREEN   0x07E0
#define TFT_BLUE    0x001F
#define TFT_YELLOW  0xFFE0
#define TFT_CYAN    0x07FF
#define TFT_GRAY    RGB565(128, 128, 128)

void     tft_init(void);
uint16_t tft_width(void);
uint16_t tft_height(void);
void     tft_backlight(int on);
void     tft_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void     tft_fill(uint16_t color);
/* Chu 5x7, scale = 1..4. Tra ve toa do x sau ky tu cuoi. */
uint16_t tft_text(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);

#endif
