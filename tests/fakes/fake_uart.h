#ifndef FAKE_UART_H
#define FAKE_UART_H

#include <stddef.h>
#include "plat_status.h"


#define FAKE_UART_OUT_CAPACITY  4096U
#define FAKE_UART_IN_CAPACITY   1024U

void        fake_uart_reset(void);
void        fake_uart_feed(const char *text);
size_t      fake_uart_in_pending(void);
const char *fake_uart_output(void);
void        fake_uart_clear_output(void);

#endif /* FAKE_UART_H */
