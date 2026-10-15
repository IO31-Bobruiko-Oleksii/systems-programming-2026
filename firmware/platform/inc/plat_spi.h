#ifndef PLAT_SPI_H
#define PLAT_SPI_H

#include <stddef.h>
#include <stdint.h>
#include "plat_status.h"


typedef enum {
    PLAT_SPI_SPEED_SLOW,
    PLAT_SPI_SPEED_FAST
} plat_spi_speed_t;

plat_status_t plat_spi_init(void);

plat_status_t plat_spi_set_speed(plat_spi_speed_t speed);

void plat_spi_cs(uint8_t asserted);

plat_status_t plat_spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len);

#endif /* PLAT_SPI_H */
