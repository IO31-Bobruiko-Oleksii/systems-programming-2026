#ifndef REPLAY_SPI_H
#define REPLAY_SPI_H

#include <stddef.h>
#include <stdint.h>
#include "plat_spi.h"


#define REPLAY_FLAG_CS    0x01U
#define REPLAY_FLAG_FAST  0x02U

void replay_spi_load(const uint8_t *mosi, const uint8_t *miso, const uint8_t *flags, size_t len);

size_t replay_spi_position(void);
size_t replay_spi_tx_mismatches(void);
size_t replay_spi_flag_mismatches(void);
size_t replay_spi_overruns(void);

#endif /* REPLAY_SPI_H */
