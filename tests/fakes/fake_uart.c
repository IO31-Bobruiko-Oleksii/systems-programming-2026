#include <string.h>
#include "fake_uart.h"
#include "plat_uart.h"

static char   out[FAKE_UART_OUT_CAPACITY];
static size_t out_len;
static char   in[FAKE_UART_IN_CAPACITY];
static size_t in_head;
static size_t in_tail;
static plat_uart_rx_info_t rx_info;
static plat_status_t rx_start_status;
static plat_status_t rx_info_status;
static unsigned rx_starts;
static unsigned rx_resets;

void fake_uart_reset(void)
{
    out_len = 0U;
    out[0] = '\0';
    in_head = 0U;
    in_tail = 0U;
    memset(&rx_info, 0, sizeof rx_info);
    rx_start_status = PLAT_OK;
    rx_info_status = PLAT_OK;
    rx_starts = 0U;
    rx_resets = 0U;
}

void fake_uart_feed(const char *text)
{
    size_t n = strlen(text);

    if (in_tail + n <= FAKE_UART_IN_CAPACITY) {
        memcpy(&in[in_tail], text, n);
        in_tail += n;
    }
}

void fake_uart_feed_error(void)
{
    if (in_tail < FAKE_UART_IN_CAPACITY) {
        in[in_tail++] = (char)0xFF;
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

void fake_uart_set_rx_info(const plat_uart_rx_info_t *info)
{
    rx_info = *info;
}

void fake_uart_fail_rx_start(plat_status_t status)
{
    rx_start_status = status;
}

void fake_uart_fail_rx_info(plat_status_t status)
{
    rx_info_status = status;
}

unsigned fake_uart_rx_starts(void)
{
    return rx_starts;
}

unsigned fake_uart_rx_resets(void)
{
    return rx_resets;
}

plat_status_t plat_uart_rx_start(void)
{
    rx_starts++;
    return rx_start_status;
}

plat_status_t plat_uart_rx_info(plat_uart_rx_info_t *info)
{
    if (info == NULL) {
        return PLAT_ERR_ARG;
    }
    if (rx_info_status != PLAT_OK) {
        return rx_info_status;
    }
    *info = rx_info;
    return PLAT_OK;
}

void plat_uart_rx_reset_stats(void)
{
    rx_resets++;
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
    if (*byte == 0xFFU) {
        return PLAT_ERR_BUS;
    }
    return PLAT_OK;
}
