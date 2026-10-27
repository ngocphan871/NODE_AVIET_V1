#include "serial.h"
#include <string.h>

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart4;

#define RX_BUF_SIZE 1024u   /* luy thua cua 2 */

typedef struct {
    UART_HandleTypeDef *huart;
    volatile uint16_t head;
    volatile uint16_t tail;
    uint8_t byte;
    uint8_t buf[RX_BUF_SIZE];
} ser_t;

static ser_t ports[SER_COUNT] = {
    [SER_DBG] = { .huart = &huart1 },
    [SER_485] = { .huart = &huart4 },
};

static ser_t *find(UART_HandleTypeDef *h)
{
    for (int i = 0; i < SER_COUNT; i++) {
        if (ports[i].huart == h) { return &ports[i]; }
    }
    return NULL;
}

void ser_init(void)
{
    for (int i = 0; i < SER_COUNT; i++) {
        ports[i].head = ports[i].tail = 0;
        HAL_UART_Receive_IT(ports[i].huart, &ports[i].byte, 1);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    ser_t *s = find(huart);
    if (s == NULL) { return; }
    uint16_t next = (uint16_t)((s->head + 1u) & (RX_BUF_SIZE - 1u));
    if (next != s->tail) {              /* day thi bo byte moi */
        s->buf[s->head] = s->byte;
        s->head = next;
    }
    HAL_UART_Receive_IT(huart, &s->byte, 1);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    /* Overrun/noise: xoa co loi roi nhan tiep */
    ser_t *s = find(huart);
    if (s == NULL) { return; }
    __HAL_UART_CLEAR_OREFLAG(huart);
    HAL_UART_Receive_IT(huart, &s->byte, 1);
}

int ser_getc(ser_port_t p)
{
    ser_t *s = &ports[p];
    if (s->head == s->tail) { return -1; }
    uint8_t c = s->buf[s->tail];
    s->tail = (uint16_t)((s->tail + 1u) & (RX_BUF_SIZE - 1u));
    return c;
}

void ser_write(ser_port_t p, const void *data, size_t len)
{
    if (len == 0) { return; }
    HAL_UART_Transmit(ports[p].huart, (const uint8_t *)data, (uint16_t)len, 1000);
}

void ser_puts(ser_port_t p, const char *s)
{
    ser_write(p, s, strlen(s));
}

void ser_flush_rx(ser_port_t p)
{
    ports[p].tail = ports[p].head;
}

UART_HandleTypeDef *ser_handle(ser_port_t p)
{
    return ports[p].huart;
}

/* Ban sao log trong RAM: doc qua SWD khi khong co cap USB-TTL.
 * dbg_log_pos = tong so byte da ghi, byte i nam o dbg_log[i % DBG_LOG_SIZE]. */
#define DBG_LOG_SIZE 4096u
char dbg_log[DBG_LOG_SIZE];
volatile uint32_t dbg_log_pos;

/* printf -> USART1 (syscalls.c goi ham nay) */
int __io_putchar(int ch)
{
    uint8_t c = (uint8_t)ch;
    dbg_log[dbg_log_pos % DBG_LOG_SIZE] = (char)c;
    dbg_log_pos++;
    if (c == '\n') {
        uint8_t cr = '\r';
        HAL_UART_Transmit(&huart1, &cr, 1, 10);
    }
    HAL_UART_Transmit(&huart1, &c, 1, 10);
    return ch;
}
