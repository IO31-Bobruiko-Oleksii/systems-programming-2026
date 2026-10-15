#ifndef PLAT_UART_H
#define PLAT_UART_H

#include <stddef.h>
#include <stdint.h>
#include "plat_status.h"


plat_status_t plat_uart_write(const uint8_t *data, size_t len);

plat_status_t plat_uart_read_byte(uint8_t *byte, uint32_t timeout_ms);

#endif /* PLAT_UART_H */
