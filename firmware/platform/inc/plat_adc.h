#ifndef PLAT_ADC_H
#define PLAT_ADC_H

#include <stdint.h>
#include "plat_status.h"

#define PLAT_ADC_CHANNELS  4U
#define PLAT_ADC_MAX_RAW   4095U

plat_status_t plat_adc_init(void);

plat_status_t plat_adc_read(uint8_t channel, uint16_t *raw);

#endif /* PLAT_ADC_H */
