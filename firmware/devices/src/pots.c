#include <stddef.h>
#include "pots.h"
#include "plat_adc.h"

#define POTS_VREF_MV  3300U

static uint32_t clamp_raw(uint16_t raw)
{
    return (raw > PLAT_ADC_MAX_RAW) ? PLAT_ADC_MAX_RAW : raw;
}

uint16_t pots_raw_to_mv(uint16_t raw)
{
    return (uint16_t)((clamp_raw(raw) * POTS_VREF_MV + PLAT_ADC_MAX_RAW / 2U) / PLAT_ADC_MAX_RAW);
}

uint8_t pots_raw_to_percent(uint16_t raw)
{
    return (uint8_t)((clamp_raw(raw) * 100U + PLAT_ADC_MAX_RAW / 2U) / PLAT_ADC_MAX_RAW);
}

plat_status_t pots_read_all(pots_t *p, int *failed_at)
{
    uint8_t ch;
    uint16_t value;
    plat_status_t st;

    if (failed_at != NULL) {
        *failed_at = -1;
    }
    if (p == NULL) {
        return PLAT_ERR_ARG;
    }
    for (ch = 0U; ch < POTS_COUNT; ch++) {
        st = plat_adc_read(ch, &value);
        if (st != PLAT_OK) {
            if (failed_at != NULL) {
                *failed_at = (int)ch;
            }
            return st;
        }
        p->raw[ch] = value;
    }
    return PLAT_OK;
}
