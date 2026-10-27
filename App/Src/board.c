#include "board.h"
#include "config.h"
#include "main.h"

static volatile uint32_t flow_cnt[2];
static uint32_t flow_last_cnt[2];
static uint32_t flow_last_tick;
static uint32_t flow_rate_ml_min[2];

void board_relay_set(int idx, bool on)
{
    GPIO_PinState s = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
    if (idx == 1) { HAL_GPIO_WritePin(RELAY1_GPIO_Port, RELAY1_Pin, s); }
    if (idx == 2) { HAL_GPIO_WritePin(RELAY2_GPIO_Port, RELAY2_Pin, s); }
}

bool board_relay_get(int idx)
{
    if (idx == 1) { return HAL_GPIO_ReadPin(RELAY1_GPIO_Port, RELAY1_Pin) == GPIO_PIN_SET; }
    if (idx == 2) { return HAL_GPIO_ReadPin(RELAY2_GPIO_Port, RELAY2_Pin) == GPIO_PIN_SET; }
    return false;
}

bool board_input_get(int idx)
{
    /* Opto dan khi co 12V -> keo chan MCU xuong 0 */
    static const uint16_t pins[3] = { INPUT1_Pin, INPUT2_Pin, INPUT3_Pin };
    if (idx < 1 || idx > 3) { return false; }
    return HAL_GPIO_ReadPin(INPUT1_GPIO_Port, pins[idx - 1]) == GPIO_PIN_RESET;
}

void board_led_set(bool on)
{
    HAL_GPIO_WritePin(LED_Y4_GPIO_Port, LED_Y4_Pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void board_led_toggle(void)
{
    HAL_GPIO_TogglePin(LED_Y4_GPIO_Port, LED_Y4_Pin);
}

void board_led3_set(bool on)
{
    HAL_GPIO_WritePin(LED_Y3_GPIO_Port, LED_Y3_Pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    if (pin == FLOW1_Pin) { flow_cnt[0]++; }
    if (pin == FLOW2_Pin) { flow_cnt[1]++; }
}

void board_flow_update(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t dt = now - flow_last_tick;
    if (dt == 0) { return; }
    for (int i = 0; i < 2; i++) {
        uint32_t c = flow_cnt[i];
        uint32_t d = c - flow_last_cnt[i];
        flow_last_cnt[i] = c;
        /* mL/phut = xung * 1000 mL/L * 60000 ms/phut / (xung/L * dt ms) */
        flow_rate_ml_min[i] = (uint32_t)(((uint64_t)d * 1000u * 60000u) /
                                         ((uint64_t)CFG_FLOW_PULSES_PER_L * dt));
    }
    flow_last_tick = now;
}

uint32_t board_flow_pulses(int idx)
{
    return (idx == 1 || idx == 2) ? flow_cnt[idx - 1] : 0;
}

uint32_t board_flow_ml_per_min(int idx)
{
    return (idx == 1 || idx == 2) ? flow_rate_ml_min[idx - 1] : 0;
}

uint32_t board_flow_total_ml(int idx)
{
    if (idx != 1 && idx != 2) { return 0; }
    return (uint32_t)(((uint64_t)flow_cnt[idx - 1] * 1000u) / CFG_FLOW_PULSES_PER_L);
}
