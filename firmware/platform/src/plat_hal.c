#include "plat_pwm.h"
#include "main.h"

extern TIM_HandleTypeDef htim3;

#define PWM_TICK_HZ  1000000U

static plat_status_t pwm_hal_channel(uint8_t channel, uint32_t *hal_channel)
{
    switch (channel) {
    case PLAT_PWM_CH_SERVO1:
        *hal_channel = TIM_CHANNEL_1;
        return PLAT_OK;
    case PLAT_PWM_CH_SERVO2:
        *hal_channel = TIM_CHANNEL_2;
        return PLAT_OK;
    default:
        return PLAT_ERR_ARG;
    }
}

plat_status_t plat_pwm_init(void)
{
    uint32_t tim_clk = HAL_RCC_GetPCLK1Freq();
    uint32_t prescaler = tim_clk / PWM_TICK_HZ;

    if (prescaler == 0U) {
        return PLAT_ERR_ARG;
    }
    __HAL_TIM_SET_PRESCALER(&htim3, prescaler - 1U);
    __HAL_TIM_SET_AUTORELOAD(&htim3, PLAT_PWM_PERIOD_US - 1U);
    if (HAL_TIM_GenerateEvent(&htim3, TIM_EVENTSOURCE_UPDATE) != HAL_OK) {
        return PLAT_ERR_BUS;
    }
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0U);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0U);
    if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1) != HAL_OK) {
        return PLAT_ERR_BUS;
    }
    if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2) != HAL_OK) {
        return PLAT_ERR_BUS;
    }
    return PLAT_OK;
}

plat_status_t plat_pwm_set_pulse_us(uint8_t channel, uint16_t pulse_us)
{
    uint32_t hal_channel;
    plat_status_t st = pwm_hal_channel(channel, &hal_channel);

    if (st != PLAT_OK) {
        return st;
    }
    if (pulse_us > PLAT_PWM_PERIOD_US) {
        return PLAT_ERR_ARG;
    }
    __HAL_TIM_SET_COMPARE(&htim3, hal_channel, pulse_us);
    return PLAT_OK;
}
