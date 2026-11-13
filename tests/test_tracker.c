#include <stddef.h>
#include <stdint.h>
#include "minitest.h"
#include "tracker.h"

static tracker_t t;
static uint16_t raw[TRACKER_SENSORS];

static void light(uint16_t tl, uint16_t tr, uint16_t bl, uint16_t br)
{
    raw[TRACKER_TL] = tl;
    raw[TRACKER_TR] = tr;
    raw[TRACKER_BL] = bl;
    raw[TRACKER_BR] = br;
}

static void fresh(void)
{
    tracker_init(&t);
    light(2000U, 2000U, 2000U, 2000U);
}

static void test_init_sets_the_documented_defaults(void)
{
    fresh();
    CHECK_EQ(t.az, 90);
    CHECK_EQ(t.tilt, 90);
    CHECK_EQ(t.zero_az, 90);
    CHECK_EQ(t.zero_tilt, 90);
    CHECK_EQ(t.deadband, 100);
    CHECK_EQ(t.dark_below, 200);
    CHECK_EQ(t.step_deg, 1);
    CHECK_EQ(t.dark_count, 0);
    CHECK_EQ(t.parked, 0);
    CHECK_EQ(t.stowed, 0);
}

static void test_balanced_light_changes_nothing(void)
{
    tracker_result_t r;

    fresh();
    r = tracker_update(&t, raw);
    CHECK_EQ(r.az_changed, 0);
    CHECK_EQ(r.tilt_changed, 0);
    CHECK_EQ(r.parked_now, 0);
    CHECK_EQ(t.az, 90);
    CHECK_EQ(t.tilt, 90);
}

static void test_brighter_right_turns_azimuth_up(void)
{
    tracker_result_t r;

    fresh();
    light(1000U, 3000U, 1000U, 3000U);
    r = tracker_update(&t, raw);
    CHECK_EQ(r.az_changed, 1);
    CHECK_EQ(r.tilt_changed, 0);
    CHECK_EQ(t.az, 91);
    CHECK_EQ(t.tilt, 90);
}

static void test_brighter_left_turns_azimuth_down(void)
{
    fresh();
    light(3000U, 1000U, 3000U, 1000U);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.az, 89);
    CHECK_EQ(t.tilt, 90);
}

static void test_brighter_top_turns_tilt_up(void)
{
    tracker_result_t r;

    fresh();
    light(3000U, 3000U, 1000U, 1000U);
    r = tracker_update(&t, raw);
    CHECK_EQ(r.az_changed, 0);
    CHECK_EQ(r.tilt_changed, 1);
    CHECK_EQ(t.az, 90);
    CHECK_EQ(t.tilt, 91);
}

static void test_brighter_bottom_turns_tilt_down(void)
{
    fresh();
    light(1000U, 1000U, 3000U, 3000U);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.tilt, 89);
}

static void test_both_axes_move_in_the_same_update(void)
{
    tracker_result_t r;

    fresh();
    light(1000U, 3500U, 1000U, 1000U);
    r = tracker_update(&t, raw);
    CHECK_EQ(r.az_changed, 1);
    CHECK_EQ(r.tilt_changed, 1);
    CHECK_EQ(t.az, 91);
    CHECK_EQ(t.tilt, 91);
}

static void test_dead_zone_edge_on_the_positive_side(void)
{
    fresh();
    light(1000U, 1100U, 1000U, 1100U);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.az, 90);
    light(1000U, 1101U, 1000U, 1101U);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.az, 91);
}

static void test_dead_zone_edge_on_the_negative_side(void)
{
    fresh();
    light(1100U, 1000U, 1100U, 1000U);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.az, 90);
    light(1101U, 1000U, 1101U, 1000U);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.az, 89);
}

static void test_an_odd_difference_is_rounded_towards_zero(void)
{
    fresh();
    light(1000U, 1101U, 1000U, 1100U);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.az, 90);
}

static void test_dead_zone_can_be_narrowed_to_zero(void)
{
    fresh();
    CHECK_EQ(tracker_set_deadband(&t, 0U), PLAT_OK);
    light(1000U, 1001U, 1000U, 1001U);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.az, 91);
}

static void test_step_is_the_largest_move_per_update(void)
{
    fresh();
    t.step_deg = 5U;
    light(1000U, 3000U, 1000U, 3000U);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.az, 95);
    light(3000U, 1000U, 3000U, 1000U);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.az, 90);
}

static void test_angles_stop_at_the_ends_of_the_range(void)
{
    tracker_result_t r;

    fresh();
    t.az = 180U;
    t.tilt = 0U;
    light(1000U, 3000U, 3000U, 3000U);
    r = tracker_update(&t, raw);
    CHECK_EQ(t.az, 180);
    CHECK_EQ(r.az_changed, 0);
    light(1000U, 1000U, 3000U, 3000U);
    r = tracker_update(&t, raw);
    CHECK_EQ(t.tilt, 0);
    CHECK_EQ(r.tilt_changed, 0);

    t.az = 0U;
    light(3000U, 1000U, 3000U, 1000U);
    r = tracker_update(&t, raw);
    CHECK_EQ(t.az, 0);
    CHECK_EQ(r.az_changed, 0);
    t.tilt = 180U;
    light(3000U, 3000U, 1000U, 1000U);
    r = tracker_update(&t, raw);
    CHECK_EQ(t.tilt, 180);
    CHECK_EQ(r.tilt_changed, 0);
}

static void test_a_step_that_would_pass_the_end_is_cut_off(void)
{
    fresh();
    t.step_deg = 10U;
    t.az = 175U;
    t.tilt = 4U;
    light(1000U, 3000U, 3000U, 3000U);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.az, 180);
    light(1000U, 1000U, 3000U, 3000U);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.tilt, 0);
}

static void test_constant_light_to_the_right_walks_to_the_end_and_stops(void)
{
    unsigned i;
    tracker_result_t r;

    fresh();
    light(1000U, 3000U, 1000U, 3000U);
    for (i = 0U; i < 89U; i++) {
        (void)tracker_update(&t, raw);
    }
    CHECK_EQ(t.az, 179);
    r = tracker_update(&t, raw);
    CHECK_EQ(t.az, 180);
    CHECK_EQ(r.az_changed, 1);
    r = tracker_update(&t, raw);
    CHECK_EQ(t.az, 180);
    CHECK_EQ(r.az_changed, 0);
}

static void test_the_night_park_needs_a_full_run_of_dark_updates(void)
{
    unsigned i;
    tracker_result_t r;

    fresh();
    t.az = 120U;
    t.tilt = 60U;
    light(100U, 100U, 100U, 100U);
    for (i = 0U; i < TRACKER_DARK_SAMPLES - 1U; i++) {
        r = tracker_update(&t, raw);
        CHECK_EQ(r.parked_now, 0);
        CHECK_EQ(r.az_changed, 0);
    }
    CHECK_EQ(t.az, 120);
    CHECK_EQ(t.tilt, 60);
    CHECK_EQ(t.parked, 0);

    r = tracker_update(&t, raw);
    CHECK_EQ(r.parked_now, 1);
    CHECK_EQ(r.az_changed, 1);
    CHECK_EQ(r.tilt_changed, 1);
    CHECK_EQ(t.az, 90);
    CHECK_EQ(t.tilt, 90);
    CHECK_EQ(t.parked, 1);
}

static void test_the_night_park_is_reported_once(void)
{
    unsigned i;
    tracker_result_t r;

    fresh();
    t.az = 120U;
    light(100U, 100U, 100U, 100U);
    for (i = 0U; i < TRACKER_DARK_SAMPLES; i++) {
        (void)tracker_update(&t, raw);
    }
    r = tracker_update(&t, raw);
    CHECK_EQ(r.parked_now, 0);
    CHECK_EQ(r.az_changed, 0);
    CHECK_EQ(t.az, 90);
    CHECK_EQ(t.dark_count, TRACKER_DARK_SAMPLES);
}

static void test_park_when_already_at_the_safe_position_reports_no_move(void)
{
    unsigned i;
    tracker_result_t r = { 0U, 0U, 0U };

    fresh();
    light(100U, 100U, 100U, 100U);
    for (i = 0U; i < TRACKER_DARK_SAMPLES; i++) {
        r = tracker_update(&t, raw);
    }
    CHECK_EQ(r.parked_now, 1);
    CHECK_EQ(r.az_changed, 0);
    CHECK_EQ(r.tilt_changed, 0);
}

static void test_dark_means_every_sensor_below_the_threshold(void)
{
    unsigned i;

    fresh();
    light(200U, 200U, 200U, 200U);
    for (i = 0U; i < TRACKER_DARK_SAMPLES; i++) {
        (void)tracker_update(&t, raw);
    }
    CHECK_EQ(t.dark_count, 0);

    light(199U, 199U, 199U, 199U);
    for (i = 0U; i < TRACKER_DARK_SAMPLES; i++) {
        (void)tracker_update(&t, raw);
    }
    CHECK_EQ(t.parked, 1);

    fresh();
    light(100U, 100U, 100U, 200U);
    for (i = 0U; i < TRACKER_DARK_SAMPLES; i++) {
        (void)tracker_update(&t, raw);
    }
    CHECK_EQ(t.parked, 0);
    CHECK_EQ(t.dark_count, 0);
}

static void test_one_bright_update_restarts_the_dark_count(void)
{
    unsigned i;
    tracker_result_t r;

    fresh();
    for (i = 0U; i < TRACKER_DARK_SAMPLES - 1U; i++) {
        light(100U, 100U, 100U, 100U);
        (void)tracker_update(&t, raw);
    }
    light(2000U, 2000U, 2000U, 2000U);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.dark_count, 0);

    light(100U, 100U, 100U, 100U);
    for (i = 0U; i < TRACKER_DARK_SAMPLES - 1U; i++) {
        r = tracker_update(&t, raw);
        CHECK_EQ(r.parked_now, 0);
    }
    r = tracker_update(&t, raw);
    CHECK_EQ(r.parked_now, 1);
}

static void test_tracking_resumes_from_the_safe_position_when_light_returns(void)
{
    unsigned i;

    fresh();
    t.az = 150U;
    light(100U, 100U, 100U, 100U);
    for (i = 0U; i < TRACKER_DARK_SAMPLES; i++) {
        (void)tracker_update(&t, raw);
    }
    CHECK_EQ(t.az, 90);
    light(1000U, 3000U, 1000U, 3000U);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.parked, 0);
    CHECK_EQ(t.dark_count, 0);
    CHECK_EQ(t.az, 91);
}

static void test_stow_goes_to_the_safe_position_and_holds_it(void)
{
    tracker_result_t r;

    fresh();
    t.az = 120U;
    t.tilt = 45U;
    r = tracker_stow(&t);
    CHECK_EQ(r.az_changed, 1);
    CHECK_EQ(r.tilt_changed, 1);
    CHECK_EQ(t.az, 90);
    CHECK_EQ(t.tilt, 90);
    CHECK_EQ(t.stowed, 1);

    light(1000U, 3000U, 1000U, 3000U);
    r = tracker_update(&t, raw);
    CHECK_EQ(r.az_changed, 0);
    CHECK_EQ(t.az, 90);

    tracker_resume(&t);
    CHECK_EQ(t.stowed, 0);
    (void)tracker_update(&t, raw);
    CHECK_EQ(t.az, 91);
}

static void test_stow_at_the_safe_position_reports_no_move(void)
{
    tracker_result_t r;

    fresh();
    r = tracker_stow(&t);
    CHECK_EQ(r.az_changed, 0);
    CHECK_EQ(r.tilt_changed, 0);
    CHECK_EQ(t.stowed, 1);
}

static void test_resume_restarts_the_night_logic(void)
{
    unsigned i;

    fresh();
    light(100U, 100U, 100U, 100U);
    for (i = 0U; i < TRACKER_DARK_SAMPLES; i++) {
        (void)tracker_update(&t, raw);
    }
    CHECK_EQ(t.parked, 1);
    (void)tracker_stow(&t);
    tracker_resume(&t);
    CHECK_EQ(t.parked, 0);
    CHECK_EQ(t.dark_count, 0);
}

static void test_calibrated_zero_is_the_park_position(void)
{
    unsigned i;

    fresh();
    CHECK_EQ(tracker_set_zero(&t, 30U, 150U), PLAT_OK);
    CHECK_EQ(t.zero_az, 30);
    CHECK_EQ(t.zero_tilt, 150);
    CHECK_EQ(t.az, 90);
    light(100U, 100U, 100U, 100U);
    for (i = 0U; i < TRACKER_DARK_SAMPLES; i++) {
        (void)tracker_update(&t, raw);
    }
    CHECK_EQ(t.az, 30);
    CHECK_EQ(t.tilt, 150);
}

static void test_calibrated_zero_moves_a_parked_or_stowed_panel(void)
{
    unsigned i;

    fresh();
    light(100U, 100U, 100U, 100U);
    for (i = 0U; i < TRACKER_DARK_SAMPLES; i++) {
        (void)tracker_update(&t, raw);
    }
    CHECK_EQ(tracker_set_zero(&t, 10U, 20U), PLAT_OK);
    CHECK_EQ(t.az, 10);
    CHECK_EQ(t.tilt, 20);

    fresh();
    (void)tracker_stow(&t);
    CHECK_EQ(tracker_set_zero(&t, 60U, 70U), PLAT_OK);
    CHECK_EQ(t.az, 60);
    CHECK_EQ(t.tilt, 70);
}

static void test_set_zero_rejects_angles_above_180(void)
{
    fresh();
    CHECK_EQ(tracker_set_zero(&t, 181U, 90U), PLAT_ERR_ARG);
    CHECK_EQ(tracker_set_zero(&t, 90U, 181U), PLAT_ERR_ARG);
    CHECK_EQ(tracker_set_zero(&t, 180U, 180U), PLAT_OK);
    CHECK_EQ(tracker_set_zero(&t, 0U, 0U), PLAT_OK);
    CHECK_EQ(t.zero_az, 0);
    CHECK_EQ(tracker_set_zero(&t, 100U, 181U), PLAT_ERR_ARG);
    CHECK_EQ(t.zero_az, 0);
}

static void test_set_deadband_rejects_values_above_the_adc_range(void)
{
    fresh();
    CHECK_EQ(tracker_set_deadband(&t, 4096U), PLAT_ERR_ARG);
    CHECK_EQ(t.deadband, 100);
    CHECK_EQ(tracker_set_deadband(&t, 4095U), PLAT_OK);
    CHECK_EQ(t.deadband, 4095);
    CHECK_EQ(tracker_set_deadband(&t, 250U), PLAT_OK);
    CHECK_EQ(t.deadband, 250);
}

static void test_null_pointers_are_harmless(void)
{
    tracker_result_t r;

    fresh();
    tracker_init(NULL);
    r = tracker_update(NULL, raw);
    CHECK_EQ(r.az_changed, 0);
    r = tracker_update(&t, NULL);
    CHECK_EQ(r.az_changed, 0);
    r = tracker_stow(NULL);
    CHECK_EQ(r.az_changed, 0);
    tracker_resume(NULL);
    CHECK_EQ(tracker_set_deadband(NULL, 10U), PLAT_ERR_ARG);
    CHECK_EQ(tracker_set_zero(NULL, 10U, 10U), PLAT_ERR_ARG);
    CHECK_EQ(t.az, 90);
}

int main(void)
{
    RUN(test_init_sets_the_documented_defaults);
    RUN(test_balanced_light_changes_nothing);
    RUN(test_brighter_right_turns_azimuth_up);
    RUN(test_brighter_left_turns_azimuth_down);
    RUN(test_brighter_top_turns_tilt_up);
    RUN(test_brighter_bottom_turns_tilt_down);
    RUN(test_both_axes_move_in_the_same_update);
    RUN(test_dead_zone_edge_on_the_positive_side);
    RUN(test_dead_zone_edge_on_the_negative_side);
    RUN(test_an_odd_difference_is_rounded_towards_zero);
    RUN(test_dead_zone_can_be_narrowed_to_zero);
    RUN(test_step_is_the_largest_move_per_update);
    RUN(test_angles_stop_at_the_ends_of_the_range);
    RUN(test_a_step_that_would_pass_the_end_is_cut_off);
    RUN(test_constant_light_to_the_right_walks_to_the_end_and_stops);
    RUN(test_the_night_park_needs_a_full_run_of_dark_updates);
    RUN(test_the_night_park_is_reported_once);
    RUN(test_park_when_already_at_the_safe_position_reports_no_move);
    RUN(test_dark_means_every_sensor_below_the_threshold);
    RUN(test_one_bright_update_restarts_the_dark_count);
    RUN(test_tracking_resumes_from_the_safe_position_when_light_returns);
    RUN(test_stow_goes_to_the_safe_position_and_holds_it);
    RUN(test_stow_at_the_safe_position_reports_no_move);
    RUN(test_resume_restarts_the_night_logic);
    RUN(test_calibrated_zero_is_the_park_position);
    RUN(test_calibrated_zero_moves_a_parked_or_stowed_panel);
    RUN(test_set_zero_rejects_angles_above_180);
    RUN(test_set_deadband_rejects_values_above_the_adc_range);
    RUN(test_null_pointers_are_harmless);
    return REPORT();
}