#include "i2c_diag.h"
#include "main.h"
#include <stdbool.h>
#include <stdio.h>

extern I2C_HandleTypeDef hi2c2;

#define HALF_US 20u                     /* ~25 kHz: du cham cho tu 1 nF tren SS1/SS2 */

typedef struct {
    uint16_t pin;
    const char *name;
} cn5_pin_t;

static const cn5_pin_t pins[4] = {
    { GPIO_PIN_0,  "chan 3 (SS1, PB0)" },
    { GPIO_PIN_1,  "chan 4 (SS2, PB1)" },
    { GPIO_PIN_10, "chan 5 (SCL, PB10)" },
    { GPIO_PIN_11, "chan 6 (SDA, PB11)" },
};

static void delay_us(uint32_t us)
{
    uint32_t t0 = DWT->CYCCNT;
    uint32_t n = us * (SystemCoreClock / 1000000u);
    while ((DWT->CYCCNT - t0) < n) { }
}

static void rel(uint16_t p) { GPIOB->BSRR = p; }                 /* open-drain: tha ra = muc 1 */
static void low(uint16_t p) { GPIOB->BSRR = (uint32_t)p << 16; }
static bool rd(uint16_t p)  { return (GPIOB->IDR & p) != 0; }

/* SCL len muc 1, cho cam bien keo dai xung clock toi da 1 ms */
static bool scl_high(uint16_t scl)
{
    rel(scl);
    for (int i = 0; i < 50 && !rd(scl); i++) { delay_us(HALF_US); }
    return rd(scl);
}

/* Gui START + dia chi ghi, tra ve true neu co ACK */
static bool probe(uint16_t scl, uint16_t sda, uint8_t addr)
{
    uint8_t b = (uint8_t)(addr << 1);
    bool ack;

    rel(sda); scl_high(scl); delay_us(HALF_US);
    low(sda); delay_us(HALF_US);                                /* START */
    low(scl); delay_us(HALF_US);
    for (int i = 7; i >= 0; i--) {
        if (b & (1u << i)) { rel(sda); } else { low(sda); }
        delay_us(HALF_US);
        scl_high(scl); delay_us(HALF_US);
        low(scl);
    }
    rel(sda); delay_us(HALF_US);                                /* bit ACK */
    scl_high(scl); delay_us(HALF_US);
    ack = !rd(sda);
    low(scl); delay_us(HALF_US);
    low(sda); delay_us(HALF_US);                                /* STOP */
    scl_high(scl); delay_us(HALF_US);
    rel(sda); delay_us(HALF_US);
    return ack;
}

/* 9 xung clock de giai phong cam bien dang giu SDA (neu co) */
static void bus_clear(uint16_t scl, uint16_t sda)
{
    rel(sda);
    for (int i = 0; i < 9 && !rd(sda); i++) {
        low(scl); delay_us(HALF_US);
        scl_high(scl); delay_us(HALF_US);
    }
}

void i2c_diag_find(void)
{
    GPIO_InitTypeDef g = { 0 };
    const uint16_t all = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_10 | GPIO_PIN_11;
    int found = 0;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* Tam ngung dem luu luong va I2C2, chuyen 4 chan sang open-drain */
    HAL_NVIC_DisableIRQ(EXTI0_IRQn);
    HAL_NVIC_DisableIRQ(EXTI1_IRQn);
    HAL_I2C_DeInit(&hi2c2);
    GPIOB->BSRR = all;
    g.Pin = all;
    g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);
    delay_us(200);

    printf("Muc chan khi tha noi:");
    for (int i = 0; i < 4; i++) {
        printf(" %s=%d", pins[i].name, rd(pins[i].pin));
    }
    printf("\n");

    for (int c = 0; c < 4; c++) {
        for (int d = 0; d < 4; d++) {
            if (c == d) { continue; }
            uint16_t scl = pins[c].pin, sda = pins[d].pin;
            bus_clear(scl, sda);
            for (uint8_t a = 0x44; a <= 0x45; a++) {
                if (probe(scl, sda, a)) {
                    printf("  THAY 0x%02X: CLK (xanh duong) o %s, DATA (vang) o %s\n",
                           a, pins[c].name, pins[d].name);
                    found++;
                }
            }
        }
    }
    if (!found) {
        printf("  Khong cap chan nao co ACK o 0x44/0x45 -> kiem tra nguon cam bien: "
               "nau -> chan 1 (5V), den -> chan 2 (GND)\n");
    } else if (found && !(probe(GPIO_PIN_10, GPIO_PIN_11, 0x44) || probe(GPIO_PIN_10, GPIO_PIN_11, 0x45))) {
        printf("  -> Dau lai: xanh duong (CLK) vao chan 5, vang (DATA) vao chan 6\n");
    }

    /* Tra lai I2C2 va EXTI dem luu luong */
    GPIOB->BSRR = all;
    HAL_I2C_Init(&hi2c2);
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    g.Mode = GPIO_MODE_IT_FALLING;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &g);
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0 | GPIO_PIN_1);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
    HAL_NVIC_EnableIRQ(EXTI1_IRQn);
}
