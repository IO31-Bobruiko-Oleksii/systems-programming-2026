#ifndef FAKE_ADC_H
#define FAKE_ADC_H

#include <stddef.h>
#include <stdint.h>
#include "plat_status.h"

#define FAKE_ADC_HISTORY 16U

void    fake_adc_reset(void);
void    fake_adc_set_raw(uint8_t channel, uint16_t raw);
void    fake_adc_fail_channel(uint8_t channel, plat_status_t status);
size_t  fake_adc_calls(void);
uint8_t fake_adc_channel_at(size_t index);

#endif /* FAKE_ADC_H */
