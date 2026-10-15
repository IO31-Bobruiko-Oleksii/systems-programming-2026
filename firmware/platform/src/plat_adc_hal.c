#include <stddef.h>
#include "plat_adc.h"
#include "main.h"

extern ADC_HandleTypeDef hadc1;

#define ADC_POLL_TIMEOUT_MS  100U

#define ADC_MARKER_PORT  GPIOB
#define ADC_MARKER_PIN   GPIO_PIN_3

static const uint32_t adc_channel[PLAT_ADC_CHANNELS] = {
    ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_18, ADC_CHANNEL_19
};

static void marker_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(ADC_MARKER_PORT, ADC_MARKER_PIN, GPIO_PIN_RESET);
    gpio.Pin = ADC_MARKER_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ADC_MARKER_PORT, &gpio);
}

plat_status_t plat_adc_init(void)
{
    marker_init();
    return (HAL_ADCEx_Calibration_Start(&hadc1) == HAL_OK) ? PLAT_OK : PLAT_ERR_BUS;
}

static plat_status_t convert(uint8_t channel, uint16_t *raw)
{
    ADC_ChannelConfTypeDef cfg = {0};
    HAL_StatusTypeDef st;
    uint32_t i;

    if (raw == NULL || channel >= PLAT_ADC_CHANNELS) {
        return PLAT_ERR_ARG;
    }

    cfg.SamplingTime = ADC_SAMPLINGTIME_COMMON_1;
    for (i = 0U; i < PLAT_ADC_CHANNELS; i++) {
        cfg.Channel = adc_channel[i];
        cfg.Rank = (i == channel) ? ADC_RANK_CHANNEL_NUMBER : ADC_RANK_NONE;
        if (HAL_ADC_ConfigChannel(&hadc1, &cfg) != HAL_OK) {
            return PLAT_ERR_BUS;
        }
    }

    st = HAL_ADC_Start(&hadc1);
    if (st == HAL_OK) {
        st = HAL_ADC_PollForConversion(&hadc1, ADC_POLL_TIMEOUT_MS);
    }
    if (st == HAL_OK) {
        *raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
    }
    (void)HAL_ADC_Stop(&hadc1);

    if (st == HAL_OK) {
        return PLAT_OK;
    }
    return (st == HAL_TIMEOUT) ? PLAT_ERR_TIMEOUT : PLAT_ERR_BUS;
}

plat_status_t plat_adc_read(uint8_t channel, uint16_t *raw)
{
    plat_status_t status;

    HAL_GPIO_WritePin(ADC_MARKER_PORT, ADC_MARKER_PIN, GPIO_PIN_SET);
    status = convert(channel, raw);
    HAL_GPIO_WritePin(ADC_MARKER_PORT, ADC_MARKER_PIN, GPIO_PIN_RESET);
    return status;
}
