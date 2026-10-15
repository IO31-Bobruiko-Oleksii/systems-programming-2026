#include "fake_adc.h"
#include "plat_adc.h"

static uint16_t      raw_value[PLAT_ADC_CHANNELS];
static plat_status_t fail_status[PLAT_ADC_CHANNELS];
static uint8_t       hist_channel[FAKE_ADC_HISTORY];
static size_t        call_count;
static plat_status_t init_status = PLAT_OK;

void fake_adc_reset(void)
{
    uint8_t i;

    for (i = 0U; i < PLAT_ADC_CHANNELS; i++) {
        raw_value[i] = 0U;
        fail_status[i] = PLAT_OK;
    }
    call_count = 0U;
    init_status = PLAT_OK;
}

void fake_adc_set_raw(uint8_t channel, uint16_t raw)
{
    if (channel < PLAT_ADC_CHANNELS) {
        raw_value[channel] = raw;
    }
}

void fake_adc_fail_channel(uint8_t channel, plat_status_t status)
{
    if (channel < PLAT_ADC_CHANNELS) {
        fail_status[channel] = status;
    }
}

size_t fake_adc_calls(void)
{
    return call_count;
}

uint8_t fake_adc_channel_at(size_t index)
{
    return hist_channel[index];
}

void fake_adc_fail_init(plat_status_t status)
{
    init_status = status;
}

plat_status_t plat_adc_init(void)
{
    return init_status;
}

plat_status_t plat_adc_read(uint8_t channel, uint16_t *raw)
{
    if (call_count < FAKE_ADC_HISTORY) {
        hist_channel[call_count] = channel;
    }
    call_count++;

    if (raw == NULL || channel >= PLAT_ADC_CHANNELS) {
        return PLAT_ERR_ARG;
    }
    if (fail_status[channel] != PLAT_OK) {
        return fail_status[channel];
    }
    *raw = raw_value[channel];
    return PLAT_OK;
}
