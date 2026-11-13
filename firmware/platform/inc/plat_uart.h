#ifndef PLAT_UART_H
#define PLAT_UART_H

#include <stddef.h>
#include <stdint.h>
#include "plat_status.h"


plat_status_t plat_uart_write(const uint8_t *data, size_t len);

plat_status_t plat_uart_read_byte(uint8_t *byte, uint32_t timeout_ms);

typedef struct {
    uint32_t dropped;
    uint32_t high;
    uint32_t errors;
    uint32_t isr_n;
    uint32_t isr_sum;
    uint32_t isr_max;
} plat_uart_rx_info_t;

plat_status_t plat_uart_rx_start(void);

plat_status_t plat_uart_rx_info(plat_uart_rx_info_t *info);

void plat_uart_rx_reset_stats(void);

#endif /* PLAT_UART_H */
