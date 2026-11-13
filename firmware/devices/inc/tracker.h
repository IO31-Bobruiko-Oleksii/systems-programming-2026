#ifndef TRACKER_H
#define TRACKER_H

#include <stdint.h>
#include "plat_status.h"

#define TRACKER_TL  0U
#define TRACKER_TR  1U
#define TRACKER_BL  2U
#define TRACKER_BR  3U
#define TRACKER_SENSORS  4U

#define TRACKER_ANGLE_MAX         180U
#define TRACKER_ADC_MAX           4095U
#define TRACKER_DEFAULT_DEADBAND  100U
#define TRACKER_DEFAULT_STEP_DEG  1U
#define TRACKER_DEFAULT_ZERO_DEG  90U
#define TRACKER_DEFAULT_DARK_BELOW  200U
#define TRACKER_DARK_SAMPLES      20U

typedef struct {
    uint16_t deadband;
    uint16_t dark_below;
    uint8_t  step_deg;
    uint8_t  zero_az;
    uint8_t  zero_tilt;
    uint8_t  az;
    uint8_t  tilt;
    uint8_t  dark_count;
    uint8_t  parked;
    uint8_t  stowed;
} tracker_t;

typedef struct {
    uint8_t az_changed;
    uint8_t tilt_changed;
    uint8_t parked_now;
} tracker_result_t;

void tracker_init(tracker_t *t);

tracker_result_t tracker_update(tracker_t *t, const uint16_t raw[TRACKER_SENSORS]);

tracker_result_t tracker_stow(tracker_t *t);
void tracker_resume(tracker_t *t);

plat_status_t tracker_set_deadband(tracker_t *t, uint32_t deadband);
plat_status_t tracker_set_zero(tracker_t *t, uint32_t az, uint32_t tilt);

#endif /* TRACKER_H */