#include <stddef.h>
#include "stm32c0xx_hal.h"
#include "plat_button.h"
#include "plat_time.h"

#define BUTTON_PORT  GPIOB
#define BUTTON_PIN   GPIO_PIN_5
#define BUTTON_IRQ   EXTI4_15_IRQn

static volatile uint32_t edge_count;
static volatile uint32_t edge_first;

plat_status_t plat_button_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = BUTTON_PIN;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BUTTON_PORT, &gpio);
    __HAL_GPIO_EXTI_CLEAR_FALLING_IT(BUTTON_PIN);
    HAL_NVIC_SetPriority(BUTTON_IRQ, 0U, 0U);
    HAL_NVIC_EnableIRQ(BUTTON_IRQ);
    return PLAT_OK;
}

void EXTI4_15_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_FALLING_IT(BUTTON_PIN) != 0U) {
        __HAL_GPIO_EXTI_CLEAR_FALLING_IT(BUTTON_PIN);
        if (edge_count == 0U) {
            edge_first = plat_time_cycles();
        }
        edge_count++;
    }
}

uint32_t plat_button_take(uint32_t *first_edge_cycles)
{
    uint32_t primask;
    uint32_t count;
    uint32_t first;

    if (edge_count == 0U) {
        return 0U;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    count = edge_count;
    first = edge_first;
    edge_count = 0U;
    __set_PRIMASK(primask);
    if (count != 0U && first_edge_cycles != NULL) {
        *first_edge_cycles = first;
    }
    return count;
}
