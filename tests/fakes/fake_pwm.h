#ifndef FAKE_PWM_H
#define FAKE_PWM_H

#include <stddef.h>
#include <stdint.h>
#include "plat_status.h"

#define FAKE_PWM_HISTORY 16U

void     fake_pwm_reset(void);
void     fake_pwm_fail_next(plat_status_t status);
void     fake_pwm_fail_init(plat_status_t status);
size_t   fake_pwm_calls(void);
uint8_t  fake_pwm_channel_at(size_t index);
uint16_t fake_pwm_pulse_at(size_t index);

#endif /* FAKE_PWM_H */
