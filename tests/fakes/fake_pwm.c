#include "fake_pwm.h"
#include "plat_pwm.h"

static uint8_t  hist_channel[FAKE_PWM_HISTORY];
static uint16_t hist_pulse[FAKE_PWM_HISTORY];
static size_t   call_count;
static plat_status_t next_status = PLAT_OK;

void fake_pwm_reset(void)
{
    call_count = 0U;
    next_status = PLAT_OK;
}

void fake_pwm_fail_next(plat_status_t status)
{
    next_status = status;
}

size_t fake_pwm_calls(void)
{
    return call_count;
}

uint8_t fake_pwm_channel_at(size_t index)
{
    return hist_channel[index];
}

uint16_t fake_pwm_pulse_at(size_t index)
{
    return hist_pulse[index];
}

plat_status_t plat_pwm_set_pulse_us(uint8_t channel, uint16_t pulse_us)
{
    plat_status_t st = next_status;

    next_status = PLAT_OK;
    if (call_count < FAKE_PWM_HISTORY) {
        hist_channel[call_count] = channel;
        hist_pulse[call_count] = pulse_us;
    }
    call_count++;
    return st;
}
