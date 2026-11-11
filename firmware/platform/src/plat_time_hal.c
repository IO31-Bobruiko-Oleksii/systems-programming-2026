#include "stm32c0xx_hal.h"
#include "plat_time.h"

uint32_t plat_time_hz(void)
{
    return SystemCoreClock;
}

uint32_t plat_time_cycles(void)
{
    uint32_t tick;
    uint32_t tick_again;
    uint32_t pend;
    uint32_t pend_again;
    uint32_t val;

    do {
        tick = uwTick;
        pend = SCB->ICSR & SCB_ICSR_PENDSTSET_Msk;
        val = SysTick->VAL;
        pend_again = SCB->ICSR & SCB_ICSR_PENDSTSET_Msk;
        tick_again = uwTick;
    } while (tick != tick_again || pend != pend_again);

    return plat_time_compose(tick, SysTick->LOAD + 1U, val, pend != 0U);
}
