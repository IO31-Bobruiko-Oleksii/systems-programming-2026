#include "stm32c0xx_hal.h"
#include "plat_uart.h"
#include "plat_ring.h"
#include "probe_pins.h"

extern UART_HandleTypeDef huart2;

#define TX_TIMEOUT_MS  1000U
#define RX_IRQ         USART2_IRQn
#define RX_IRQ_PRIO    1U
#define RX_ERRORS      (UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE)

static plat_ring_t rx_ring;
static volatile uint32_t rx_errors;
static volatile uint32_t rx_error_pending;
static volatile uint32_t isr_n;
static volatile uint32_t isr_sum;
static volatile uint32_t isr_max;

plat_status_t plat_uart_write(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0U || len > UINT16_MAX) {
        return PLAT_ERR_ARG;
    }
    switch (HAL_UART_Transmit(&huart2, (uint8_t *)data, (uint16_t)len, TX_TIMEOUT_MS)) {
    case HAL_OK:      return PLAT_OK;
    case HAL_TIMEOUT: return PLAT_ERR_TIMEOUT;
    default:          return PLAT_ERR_BUS;
    }
}

void USART2_IRQHandler(void)
{
    uint32_t start = SysTick->VAL;
    uint32_t isr = huart2.Instance->ISR;
    uint32_t end;
    uint32_t spent;

    if ((isr & UART_FLAG_RXNE) != 0U) {
        uint8_t byte = (uint8_t)huart2.Instance->RDR;

        if ((isr & (UART_FLAG_FE | UART_FLAG_NE)) == 0U) {
            (void)plat_ring_put(&rx_ring, byte);
        }
    }
    if ((isr & RX_ERRORS) != 0U) {
        __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF);
        rx_errors++;
        rx_error_pending = 1U;
    }
    end = SysTick->VAL;
    spent = start - end;
    if (end > start) {
        spent += SysTick->LOAD + 1U;
    }
    isr_n++;
    isr_sum += spent;
    if (spent > isr_max) {
        isr_max = spent;
    }
}

plat_status_t plat_uart_rx_start(void)
{
    plat_ring_init(&rx_ring);
    rx_errors = 0U;
    rx_error_pending = 0U;
    isr_n = 0U;
    isr_sum = 0U;
    isr_max = 0U;
    __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF);
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_RXNE);
    HAL_NVIC_SetPriority(RX_IRQ, RX_IRQ_PRIO, 0U);
    HAL_NVIC_EnableIRQ(RX_IRQ);
    return PLAT_OK;
}

plat_status_t plat_uart_rx_info(plat_uart_rx_info_t *info)
{
    uint32_t primask;

    if (info == NULL) {
        return PLAT_ERR_ARG;
    }
    primask = __get_PRIMASK();
    __disable_irq();
    info->dropped = rx_ring.dropped;
    info->high = rx_ring.high;
    info->errors = rx_errors;
    info->isr_n = isr_n;
    info->isr_sum = isr_sum;
    info->isr_max = isr_max;
    __set_PRIMASK(primask);
    return PLAT_OK;
}

void plat_uart_rx_reset_stats(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    plat_ring_reset_stats(&rx_ring);
    rx_errors = 0U;
    isr_n = 0U;
    isr_sum = 0U;
    isr_max = 0U;
    __set_PRIMASK(primask);
}

plat_status_t plat_uart_read_byte(uint8_t *byte, uint32_t timeout_ms)
{
    uint32_t t0;

    if (byte == NULL) {
        return PLAT_ERR_ARG;
    }
    if (rx_error_pending != 0U) {
        rx_error_pending = 0U;
        return PLAT_ERR_BUS;
    }
    PROBE_BUSY_LOW();
    t0 = HAL_GetTick();
    while (plat_ring_get(&rx_ring, byte) != 0) {
        if (timeout_ms == 0U || (HAL_GetTick() - t0) > timeout_ms) {
            PROBE_BUSY_HIGH();
            return PLAT_ERR_TIMEOUT;
        }
    }
    PROBE_BUSY_HIGH();
    return PLAT_OK;
}
