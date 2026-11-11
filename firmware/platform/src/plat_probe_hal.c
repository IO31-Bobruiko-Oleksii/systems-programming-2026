#include "plat_probe.h"
#include "probe_pins.h"

static const uint32_t pin_mask[PLAT_PROBE_COUNT] = {
    PROBE_T2_PIN, PROBE_EVENT_PIN
};

void plat_probe_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    PROBE_PORT->BSRR = ((uint32_t)(PROBE_T2_PIN | PROBE_EVENT_PIN) << 16U)
                     | (uint32_t)PROBE_BUSY_PIN;
    gpio.Pin = PROBE_T2_PIN | PROBE_BUSY_PIN | PROBE_EVENT_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(PROBE_PORT, &gpio);
}

void plat_probe_set(plat_probe_t probe)
{
    if ((unsigned)probe < (unsigned)PLAT_PROBE_COUNT) {
        PROBE_PORT->BSRR = pin_mask[probe];
    }
}

void plat_probe_clear(plat_probe_t probe)
{
    if ((unsigned)probe < (unsigned)PLAT_PROBE_COUNT) {
        PROBE_PORT->BSRR = pin_mask[probe] << 16U;
    }
}