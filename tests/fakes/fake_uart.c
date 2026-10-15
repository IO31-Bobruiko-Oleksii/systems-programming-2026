#include <string.h>
#include "fake_uart.h"
#include "plat_uart.h"

static char   out[FAKE_UART_OUT_CAPACITY];
static size_t out_len;
static char   in[FAKE_UART_IN_CAPACITY];
static size_t in_head;
static size_t in_tail;

void fake_uart_reset(void)
{
    out_len = 0U;
    out[0] = '\0';
    in_head = 0U;
    in_tail = 0U;
}

void fake_uart_feed(const char *text)
{
    size_t n = strlen(text);

    if (in_tail + n <= FAKE_UART_IN_CAPACITY) {
        memcpy(&in[in_tail], text, n);
        in_tail += n;
    }
}

size_t fake_uart_in_pending(void)
{
    return in_tail - in_head;
}

const char *fake_uart_output(void)
{
    return out;
}

void fake_uart_clear_output(void)
{
    out_len = 0U;
    out[0] = '\0';
}

plat_status_t plat_uart_write(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0U) {
        return PLAT_ERR_ARG;
    }
    if (out_len + len >= FAKE_UART_OUT_CAPACITY) {
        return PLAT_ERR_BUS;
    }
    memcpy(&out[out_len], data, len);
    out_len += len;
    out[out_len] = '\0';
    return PLAT_OK;
}

plat_status_t plat_uart_read_byte(uint8_t *byte, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (byte == NULL) {
        return PLAT_ERR_ARG;
    }
    if (in_head == in_tail) {
        return PLAT_ERR_TIMEOUT;
    }
    *byte = (uint8_t)in[in_head++];
    return PLAT_OK;
}
