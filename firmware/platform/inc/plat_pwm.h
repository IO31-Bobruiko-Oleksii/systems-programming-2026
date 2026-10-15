#ifndef PLAT_PWM_H
#define PLAT_PWM_H

#include <stdint.h>
#include "plat_status.h"

#define PLAT_PWM_CH_SERVO1  0U
#define PLAT_PWM_CH_SERVO2  1U

plat_status_t plat_pwm_set_pulse_us(uint8_t channel, uint16_t pulse_us);

#endif /* PLAT_PWM_H */
