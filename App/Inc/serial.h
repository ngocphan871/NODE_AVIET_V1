/*
 * Hai cong noi tiep cua board, nhan bang ngat vao bo dem vong:
 *   SER_DBG  USART1 PB6/PB7  115200  - debug/console, CN6 chan 6/7
 *   SER_485  UART4  PA0/PA1  9600    - RS485 (MAX3485), DE = PA2
 * printf() di ra SER_DBG.
 */
#ifndef SERIAL_H
#define SERIAL_H

#include <stddef.h>
#include <stdint.h>
#include "main.h"

typedef enum { SER_DBG = 0, SER_485, SER_COUNT } ser_port_t;

void ser_init(void);
int  ser_getc(ser_port_t p);                 /* -1 neu bo dem rong */
void ser_write(ser_port_t p, const void *data, size_t len);
void ser_puts(ser_port_t p, const char *s);
void ser_flush_rx(ser_port_t p);
UART_HandleTypeDef *ser_handle(ser_port_t p);

#endif
