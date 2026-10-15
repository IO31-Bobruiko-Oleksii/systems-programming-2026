#include <stdint.h>
#include "minitest.h"
#include "pots.h"
#include "plat_adc.h"
#include "fake_adc.h"

static const uint16_t LOG_RAW[POTS_COUNT] = { 1025U, 2050U, 3074U, 4095U };

static void load_log_values(void)
{
    uint8_t i;

    for (i = 0U; i < POTS_COUNT; i++) {
        fake_adc_set_raw(i, LOG_RAW[i]);
    }
}

static void test_raw_to_mv_vectors(void)
{
    CHECK_EQ(pots_raw_to_mv(0),    0);
    CHECK_EQ(pots_raw_to_mv(1025), 826);
    CHECK_EQ(pots_raw_to_mv(2050), 1652);
    CHECK_EQ(pots_raw_to_mv(3074), 2477);
    CHECK_EQ(pots_raw_to_mv(4095), 3300);
}

static void test_raw_to_percent_vectors(void)
{
    CHECK_EQ(pots_raw_to_percent(0),    0);
    CHECK_EQ(pots_raw_to_percent(1025), 25);
    CHECK_EQ(pots_raw_to_percent(2050), 50);
    CHECK_EQ(pots_raw_to_percent(3074), 75);
    CHECK_EQ(pots_raw_to_percent(4095), 100);
}

static void test_conversions_clamp_out_of_range_raw(void)
{
    CHECK_EQ(pots_raw_to_mv(4096), 3300);
    CHECK_EQ(pots_raw_to_mv(UINT16_MAX), 3300);
    CHECK_EQ(pots_raw_to_percent(4096), 100);
    CHECK_EQ(pots_raw_to_percent(UINT16_MAX), 100);
}

static void test_read_all_reads_channels_in_order(void)
{
    pots_t p = { { 0U, 0U, 0U, 0U } };
    int failed_at = 99;
    uint8_t i;

    fake_adc_reset();
    load_log_values();
    CHECK_EQ(pots_read_all(&p, &failed_at), PLAT_OK);
    CHECK_EQ(failed_at, -1);
    CHECK_EQ(fake_adc_calls(), POTS_COUNT);
    for (i = 0U; i < POTS_COUNT; i++) {
        CHECK_EQ(fake_adc_channel_at(i), i);
        CHECK_EQ(p.raw[i], LOG_RAW[i]);
    }
}

static void test_failure_midway_stops_and_keeps_old_values(void)
{
    pots_t p = { { 7U, 8U, 9U, 10U } };
    int failed_at = 99;

    fake_adc_reset();
    load_log_values();
    fake_adc_fail_channel(2U, PLAT_ERR_TIMEOUT);

    CHECK_EQ(pots_read_all(&p, &failed_at), PLAT_ERR_TIMEOUT);
    CHECK_EQ(failed_at, 2);
    CHECK_EQ(fake_adc_calls(), 3);
    CHECK_EQ(p.raw[0], 1025);
    CHECK_EQ(p.raw[1], 2050);
    CHECK_EQ(p.raw[2], 9);
    CHECK_EQ(p.raw[3], 10);
}

static void test_failure_on_first_channel(void)
{
    pots_t p = { { 7U, 8U, 9U, 10U } };
    int failed_at = 99;

    fake_adc_reset();
    fake_adc_fail_channel(0U, PLAT_ERR_BUS);

    CHECK_EQ(pots_read_all(&p, &failed_at), PLAT_ERR_BUS);
    CHECK_EQ(failed_at, 0);
    CHECK_EQ(fake_adc_calls(), 1);
    CHECK_EQ(p.raw[0], 7);
}

static void test_failed_at_is_optional(void)
{
    pots_t p = { { 0U, 0U, 0U, 0U } };

    fake_adc_reset();
    fake_adc_fail_channel(1U, PLAT_ERR_BUS);
    CHECK_EQ(pots_read_all(&p, NULL), PLAT_ERR_BUS);
    fake_adc_reset();
    CHECK_EQ(pots_read_all(&p, NULL), PLAT_OK);
}

static void test_null_pointer_is_rejected_without_hardware_access(void)
{
    int failed_at = 99;

    fake_adc_reset();
    CHECK_EQ(pots_read_all(NULL, &failed_at), PLAT_ERR_ARG);
    CHECK_EQ(failed_at, -1);
    CHECK_EQ(fake_adc_calls(), 0);
}

int main(void)
{
    RUN(test_raw_to_mv_vectors);
    RUN(test_raw_to_percent_vectors);
    RUN(test_conversions_clamp_out_of_range_raw);
    RUN(test_read_all_reads_channels_in_order);
    RUN(test_failure_midway_stops_and_keeps_old_values);
    RUN(test_failure_on_first_channel);
    RUN(test_failed_at_is_optional);
    RUN(test_null_pointer_is_rejected_without_hardware_access);
    return REPORT();
}
