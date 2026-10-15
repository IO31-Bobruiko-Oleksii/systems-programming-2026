#ifndef POTS_H
#define POTS_H

#include <stdint.h>
#include "plat_status.h"
#include "plat_adc.h"

#define POTS_COUNT  PLAT_ADC_CHANNELS

typedef struct {
    uint16_t raw[POTS_COUNT]; 
} pots_t;

uint16_t pots_raw_to_mv(uint16_t raw);
uint8_t  pots_raw_to_percent(uint16_t raw);

plat_status_t pots_read_all(pots_t *p, int *failed_at);

#endif /* POTS_H */
