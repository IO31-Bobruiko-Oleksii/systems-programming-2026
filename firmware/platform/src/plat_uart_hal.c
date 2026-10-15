#include "stm32c0xx_hal.h"
#include "plat_uart.h"

extern UART_HandleTypeDef huart2;

#define TX_TIMEOUT_MS  1000U

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

plat_status_t plat_uart_read_byte(uint8_t *byte, uint32_t timeout_ms)
{
    const uint32_t error_flags = UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE;
    HAL_StatusTypeDef st;

    if (byte == NULL) {
        return PLAT_ERR_ARG;
    }
    st = HAL_UART_Receive(&huart2, byte, 1U, timeout_ms);
    if (st == HAL_TIMEOUT) {
        return PLAT_ERR_TIMEOUT;
    }
    if (st != HAL_OK) {
        return PLAT_ERR_BUS;
    }
    if ((huart2.Instance->ISR & error_flags) != 0U) {
        __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_OREF | UART_CLEAR_FEF | UART_CLEAR_NEF);
        return PLAT_ERR_BUS;
    }
    return PLAT_OK;
}
