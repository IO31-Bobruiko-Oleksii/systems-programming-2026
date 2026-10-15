#include <stddef.h>
#include "plat_spi.h"
#include "main.h"

extern SPI_HandleTypeDef hspi1;

#define SPI_TIMEOUT_MS  100U
#define SPI_CHUNK       32U

#define CS_PORT  GPIOB
#define CS_PIN   GPIO_PIN_9

static plat_status_t from_hal(HAL_StatusTypeDef st)
{
    if (st == HAL_OK) {
        return PLAT_OK;
    }
    return (st == HAL_TIMEOUT) ? PLAT_ERR_TIMEOUT : PLAT_ERR_BUS;
}

plat_status_t plat_spi_init(void)
{
    plat_spi_cs(0U);
    return plat_spi_set_speed(PLAT_SPI_SPEED_SLOW);
}

plat_status_t plat_spi_set_speed(plat_spi_speed_t speed)
{
    switch (speed) {
    case PLAT_SPI_SPEED_SLOW:
        hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
        break;
    case PLAT_SPI_SPEED_FAST:
        hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2; 
        break;
    default:
        return PLAT_ERR_ARG;
    }
    return from_hal(HAL_SPI_Init(&hspi1));
}

void plat_spi_cs(uint8_t asserted)
{
    HAL_GPIO_WritePin(CS_PORT, CS_PIN, (asserted != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

plat_status_t plat_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    static const uint8_t ff[SPI_CHUNK] = {
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,
        0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU
    };
    uint8_t scratch[SPI_CHUNK];
    size_t done = 0U;

    if (len == 0U) {
        return PLAT_ERR_ARG;
    }
    while (done < len) {
        size_t n = ((len - done) < SPI_CHUNK) ? (len - done) : SPI_CHUNK;
        uint8_t *out = (uint8_t *)(uintptr_t)((tx != NULL) ? (tx + done) : ff);
        uint8_t *in = (rx != NULL) ? (rx + done) : scratch;
        plat_status_t st = from_hal(HAL_SPI_TransmitReceive(&hspi1, out, in, (uint16_t)n, SPI_TIMEOUT_MS));

        if (st != PLAT_OK) {
            return st;
        }
        done += n;
    }
    return PLAT_OK;
}
