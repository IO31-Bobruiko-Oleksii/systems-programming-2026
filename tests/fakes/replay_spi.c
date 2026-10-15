#include <stddef.h>
#include "replay_spi.h"

static const uint8_t *g_mosi;
static const uint8_t *g_miso;
static const uint8_t *g_flags;
static size_t g_len;
static size_t g_pos;
static size_t g_tx_mismatch;
static size_t g_flag_mismatch;
static size_t g_overrun;
static uint8_t g_cs_asserted;
static plat_spi_speed_t g_speed = PLAT_SPI_SPEED_SLOW;

void replay_spi_load(const uint8_t *mosi, const uint8_t *miso, const uint8_t *flags, size_t len)
{
    g_mosi = mosi;
    g_miso = miso;
    g_flags = flags;
    g_len = len;
    g_pos = 0U;
    g_tx_mismatch = 0U;
    g_flag_mismatch = 0U;
    g_overrun = 0U;
    g_cs_asserted = 0U;
    g_speed = PLAT_SPI_SPEED_SLOW;
}

size_t replay_spi_position(void) { return g_pos; }
size_t replay_spi_tx_mismatches(void) { return g_tx_mismatch; }
size_t replay_spi_flag_mismatches(void) { return g_flag_mismatch; }
size_t replay_spi_overruns(void) { return g_overrun; }

plat_status_t plat_spi_init(void)
{
    g_cs_asserted = 0U;
    g_speed = PLAT_SPI_SPEED_SLOW;
    return PLAT_OK;
}

plat_status_t plat_spi_set_speed(plat_spi_speed_t speed)
{
    g_speed = speed;
    return PLAT_OK;
}

void plat_spi_cs(uint8_t asserted)
{
    g_cs_asserted = (asserted != 0U) ? 1U : 0U;
}

plat_status_t plat_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    size_t i;

    if (len == 0U) {
        return PLAT_ERR_ARG;
    }
    for (i = 0U; i < len; i++) {
        uint8_t out = (tx != NULL) ? tx[i] : 0xFFU;
        uint8_t want_cs;
        uint8_t want_fast;

        if (g_pos >= g_len) {
            g_overrun++;
            return PLAT_ERR_BUS;
        }
        if (out != g_mosi[g_pos]) {
            g_tx_mismatch++;
        }
        want_cs = (uint8_t)((g_flags[g_pos] & REPLAY_FLAG_CS) != 0U);
        want_fast = (uint8_t)((g_flags[g_pos] & REPLAY_FLAG_FAST) != 0U);
        if (want_cs != g_cs_asserted || want_fast != (uint8_t)(g_speed == PLAT_SPI_SPEED_FAST)) {
            g_flag_mismatch++;
        }
        if (rx != NULL) {
            rx[i] = g_miso[g_pos];
        }
        g_pos++;
    }
    return PLAT_OK;
}
