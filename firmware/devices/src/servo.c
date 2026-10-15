#include <stddef.h>
#include "servo.h"
#include "plat_pwm.h"

uint16_t servo_angle_to_us(int16_t angle_deg)
{
    uint32_t a;

    if (angle_deg < 0) {
        angle_deg = 0;
    }
    if (angle_deg > SERVO_ANGLE_MAX) {
        angle_deg = SERVO_ANGLE_MAX;
    }
    a = (uint32_t)angle_deg;

    return (uint16_t)(SERVO_MIN_US +
                      (a * (SERVO_MAX_US - SERVO_MIN_US) + SERVO_ANGLE_MAX / 2U) / SERVO_ANGLE_MAX);
}

plat_status_t servo_set_angle(servo_t *s, int16_t angle_deg)
{
    uint16_t us;
    plat_status_t st;

    if (s == NULL) {
        return PLAT_ERR_ARG;
    }
    us = servo_angle_to_us(angle_deg);
    st = plat_pwm_set_pulse_us(s->channel, us);
    if (st == PLAT_OK) {
        s->pulse_us = us;
    }
    return st;
}

plat_status_t servo_init(servo_t *s, uint8_t channel)
{
    if (s == NULL) {
        return PLAT_ERR_ARG;
    }
    s->channel = channel;
    s->pulse_us = 0U;
    return servo_set_angle(s, SERVO_NEUTRAL_DEG);
}
