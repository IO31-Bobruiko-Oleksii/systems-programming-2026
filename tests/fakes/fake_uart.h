#ifndef FAKE_UART_H
#define FAKE_UART_H

#include <stddef.h>
#include "plat_status.h"
#include "plat_uart.h"


#define FAKE_UART_OUT_CAPACITY  4096U
#define FAKE_UART_IN_CAPACITY   1024U

void        fake_uart_reset(void);
void        fake_uart_feed(const char *text);
void        fake_uart_feed_error(void);
size_t      fake_uart_in_pending(void);
const char *fake_uart_output(void);
void        fake_uart_clear_output(void);
void        fake_uart_set_rx_info(const plat_uart_rx_info_t *info);
void        fake_uart_fail_rx_start(plat_status_t status);
void        fake_uart_fail_rx_info(plat_status_t status);
unsigned    fake_uart_rx_starts(void);
unsigned    fake_uart_rx_resets(void);

#endif /* FAKE_UART_H */
