#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>
#include "plat_status.h"

#define SERVO_MIN_US       1000U
#define SERVO_MAX_US       2000U
#define SERVO_ANGLE_MAX    180
#define SERVO_NEUTRAL_DEG  90

typedef struct {
    uint8_t  channel;
    uint16_t pulse_us;
} servo_t;

uint16_t servo_angle_to_us(int16_t angle_deg);

plat_status_t servo_init(servo_t *s, uint8_t channel);

plat_status_t servo_set_angle(servo_t *s, int16_t angle_deg);

#endif /* SERVO_H */
