/*
 * I/O tren board (sheet I_O + LED_STATUS):
 *   RELAY1 PE2, RELAY2 PE3   - qua NPN Q5/Q6, muc 1 = relay hut
 *   INPUT1..3 PE4..PE6       - opto PC817, co 12V vao IN -> chan MCU = 0
 *   FLOW1 PB0, FLOW2 PB1     - YF-S201C, dem xung canh xuong bang EXTI
 *   LED_Y4 PB3, LED_Y3 PB4   - LED noi 3.3V qua 2k2, muc 0 = sang
 *   (LED_Y2 = chan STAT cua BQ25798, phan cung tu sang khi dang sac)
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

void board_relay_set(int idx, bool on);      /* idx = 1..2 */
bool board_relay_get(int idx);
bool board_input_get(int idx);               /* idx = 1..3, true = co tin hieu */
void board_led_set(bool on);
void board_led_toggle(void);
void board_led3_set(bool on);                /* LED_Y3: bao co goi LoRa */

/* Luu luong: goi board_flow_update() moi ~1 s */
void     board_flow_update(void);
uint32_t board_flow_pulses(int idx);         /* tong so xung tu luc bat */
uint32_t board_flow_ml_per_min(int idx);     /* luu luong tuc thoi, mL/phut */
uint32_t board_flow_total_ml(int idx);       /* tong the tich, mL */

#endif
