#include <stddef.h>
#include "tracker.h"

static uint8_t go_to(uint8_t *axis, uint8_t target)
{
    uint8_t changed = (*axis != target) ? 1U : 0U;

    *axis = target;
    return changed;
}

static uint8_t nudge(uint8_t *axis, int32_t error, uint16_t deadband, uint8_t step)
{
    uint8_t before = *axis;

    if (error > (int32_t)deadband) {
        *axis = (*axis + step > (int)TRACKER_ANGLE_MAX) ? (uint8_t)TRACKER_ANGLE_MAX
                                                        : (uint8_t)(*axis + step);
    } else if (error < -(int32_t)deadband) {
        *axis = (*axis > step) ? (uint8_t)(*axis - step) : 0U;
    }
    return (*axis != before) ? 1U : 0U;
}

void tracker_init(tracker_t *t)
{
    if (t == NULL) {
        return;
    }
    t->deadband = TRACKER_DEFAULT_DEADBAND;
    t->dark_below = TRACKER_DEFAULT_DARK_BELOW;
    t->step_deg = TRACKER_DEFAULT_STEP_DEG;
    t->zero_az = TRACKER_DEFAULT_ZERO_DEG;
    t->zero_tilt = TRACKER_DEFAULT_ZERO_DEG;
    t->az = TRACKER_DEFAULT_ZERO_DEG;
    t->tilt = TRACKER_DEFAULT_ZERO_DEG;
    t->dark_count = 0U;
    t->parked = 0U;
    t->stowed = 0U;
}

tracker_result_t tracker_update(tracker_t *t, const uint16_t raw[TRACKER_SENSORS])
{
    tracker_result_t r = { 0U, 0U, 0U };
    int32_t az_error;
    int32_t tilt_error;
    unsigned i;
    int dark = 1;

    if (t == NULL || raw == NULL || t->stowed != 0U) {
        return r;
    }
    for (i = 0U; i < TRACKER_SENSORS; i++) {
        if (raw[i] >= t->dark_below) {
            dark = 0;
        }
    }

    if (dark == 0) {
        t->dark_count = 0U;
        t->parked = 0U;
        az_error = ((int32_t)raw[TRACKER_TR] + raw[TRACKER_BR] - raw[TRACKER_TL] - raw[TRACKER_BL]) / 2;
        tilt_error = ((int32_t)raw[TRACKER_TL] + raw[TRACKER_TR] - raw[TRACKER_BL] - raw[TRACKER_BR]) / 2;
        r.az_changed = nudge(&t->az, az_error, t->deadband, t->step_deg);
        r.tilt_changed = nudge(&t->tilt, tilt_error, t->deadband, t->step_deg);
        return r;
    }

    if (t->dark_count < TRACKER_DARK_SAMPLES) {
        t->dark_count++;
    }
    if (t->dark_count >= TRACKER_DARK_SAMPLES && t->parked == 0U) {
        t->parked = 1U;
        r.parked_now = 1U;
        r.az_changed = go_to(&t->az, t->zero_az);
        r.tilt_changed = go_to(&t->tilt, t->zero_tilt);
    }
    return r;
}

tracker_result_t tracker_stow(tracker_t *t)
{
    tracker_result_t r = { 0U, 0U, 0U };

    if (t == NULL) {
        return r;
    }
    t->stowed = 1U;
    r.az_changed = go_to(&t->az, t->zero_az);
    r.tilt_changed = go_to(&t->tilt, t->zero_tilt);
    return r;
}

void tracker_resume(tracker_t *t)
{
    if (t == NULL) {
        return;
    }
    t->stowed = 0U;
    t->parked = 0U;
    t->dark_count = 0U;
}

plat_status_t tracker_set_deadband(tracker_t *t, uint32_t deadband)
{
    if (t == NULL || deadband > TRACKER_ADC_MAX) {
        return PLAT_ERR_ARG;
    }
    t->deadband = (uint16_t)deadband;
    return PLAT_OK;
}

plat_status_t tracker_set_zero(tracker_t *t, uint32_t az, uint32_t tilt)
{
    if (t == NULL || az > TRACKER_ANGLE_MAX || tilt > TRACKER_ANGLE_MAX) {
        return PLAT_ERR_ARG;
    }
    t->zero_az = (uint8_t)az;
    t->zero_tilt = (uint8_t)tilt;
    if (t->parked != 0U || t->stowed != 0U) {
        t->az = t->zero_az;
        t->tilt = t->zero_tilt;
    }
    return PLAT_OK;
}