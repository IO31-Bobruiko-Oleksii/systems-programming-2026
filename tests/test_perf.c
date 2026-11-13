#include <stdint.h>
#include <string.h>
#include "minitest.h"
#include "perf.h"
#include "plat_time.h"

#define HZ 12000000U

static void test_compose_counts_ticks_and_the_position_inside_the_tick(void)
{
    CHECK_EQ(plat_time_compose(0U, 12000U, 11999U, 0), 0U);
    CHECK_EQ(plat_time_compose(0U, 12000U, 0U, 0), 11999U);
    CHECK_EQ(plat_time_compose(5U, 12000U, 11999U, 0), 60000U);
    CHECK_EQ(plat_time_compose(5U, 12000U, 11000U, 0), 60999U);
    CHECK_EQ(plat_time_compose(1000U, 12000U, 6000U, 0), 12005999U);
}

static void test_compose_adds_the_tick_that_has_not_been_counted_yet(void)
{
    CHECK_EQ(plat_time_compose(4U, 12000U, 11990U, 1), 5U * 12000U + 9U);

    CHECK_EQ(plat_time_compose(4U, 12000U, 0U, 0), 59999U);
    CHECK_EQ(plat_time_compose(4U, 12000U, 11999U, 1), 60000U);
}

static void test_compose_is_monotonic_across_the_tick_boundary(void)
{
    uint32_t before = plat_time_compose(7U, 12000U, 0U, 0);
    uint32_t after = plat_time_compose(8U, 12000U, 11999U, 0);

    CHECK_EQ(after - before, 1U);
}

static void test_unsigned_difference_survives_the_counter_wrap(void)
{
    uint32_t earlier = 0xFFFFFF00U;
    uint32_t later = 0x00000100U;

    CHECK_EQ((uint32_t)(later - earlier), 0x200U);
}

static void test_stat_starts_empty_and_takes_the_first_sample_as_min_and_max(void)
{
    perf_stat_t s;

    perf_stat_init(&s, "task");
    CHECK_EQ(s.count, 0U);
    CHECK_EQ(s.sum, 0U);
    perf_stat_add(&s, 500U);
    CHECK_EQ(s.count, 1U);
    CHECK_EQ(s.min, 500U);
    CHECK_EQ(s.max, 500U);
}

static void test_stat_tracks_min_max_and_sum(void)
{
    perf_stat_t s;

    perf_stat_init(&s, "task");
    perf_stat_add(&s, 300U);
    perf_stat_add(&s, 100U);
    perf_stat_add(&s, 700U);
    perf_stat_add(&s, 200U);
    CHECK_EQ(s.count, 4U);
    CHECK_EQ(s.min, 100U);
    CHECK_EQ(s.max, 700U);
    CHECK_EQ(s.sum, 1300U);
}

static void test_stat_sum_does_not_overflow_32_bits(void)
{
    perf_stat_t s;
    unsigned i;

    perf_stat_init(&s, "big");
    for (i = 0U; i < 10U; i++) {
        perf_stat_add(&s, 0xF0000000U);
    }
    CHECK_EQ(s.sum, 10ULL * 0xF0000000ULL);
    CHECK_EQ(s.max, 0xF0000000U);
}

static void test_stat_ignores_a_null_pointer(void)
{
    perf_stat_init(NULL, "x");
    perf_stat_add(NULL, 1U);
    CHECK(1);
}

static void test_cycles_to_us_rounds_to_nearest(void)
{
    CHECK_EQ(perf_cycles_to_us(0U, HZ), 0U);
    CHECK_EQ(perf_cycles_to_us(12U, HZ), 1U);
    CHECK_EQ(perf_cycles_to_us(5U, HZ), 0U);
    CHECK_EQ(perf_cycles_to_us(6U, HZ), 1U);
    CHECK_EQ(perf_cycles_to_us(12000U, HZ), 1000U);
    CHECK_EQ(perf_cycles_to_us(UINT32_MAX, HZ), 357913941U);
    CHECK_EQ(perf_cycles_to_us(100U, 0U), 0U);
}

static void test_format_matches_the_documented_stat_line(void)
{
    perf_stat_t s;
    char text[96];
    int n;

    perf_stat_init(&s, "sensors");
    perf_stat_add(&s, 6000U);
    perf_stat_add(&s, 12000U);
    perf_stat_add(&s, 24000U);
    n = perf_stat_format(&s, HZ, text, sizeof text);
    CHECK_EQ(strcmp(text, "STAT name=sensors n=3 min_us=500 avg_us=1167 max_us=2000"), 0);
    CHECK_EQ(n, (int)strlen(text));
}

static void test_format_of_an_empty_stat_shows_zeros(void)
{
    perf_stat_t s;
    char text[96];

    perf_stat_init(&s, "idle");
    (void)perf_stat_format(&s, HZ, text, sizeof text);
    CHECK_EQ(strcmp(text, "STAT name=idle n=0 min_us=0 avg_us=0 max_us=0"), 0);
}

static void test_format_reports_the_needed_length_when_the_buffer_is_small(void)
{
    perf_stat_t s;
    char text[8];
    int n;

    perf_stat_init(&s, "sensors");
    perf_stat_add(&s, 12U);
    n = perf_stat_format(&s, HZ, text, sizeof text);
    CHECK(n > (int)sizeof text);
    CHECK_EQ(strlen(text), sizeof text - 1U);
}

static void test_format_rejects_bad_arguments(void)
{
    perf_stat_t s;
    char text[64];

    perf_stat_init(&s, NULL);
    CHECK_EQ(perf_stat_format(NULL, HZ, text, sizeof text), -1);
    CHECK_EQ(perf_stat_format(&s, HZ, NULL, sizeof text), -1);
    CHECK_EQ(perf_stat_format(&s, HZ, text, 0U), -1);
    (void)perf_stat_format(&s, HZ, text, sizeof text);
    CHECK_EQ(strcmp(text, "STAT name=? n=0 min_us=0 avg_us=0 max_us=0"), 0);
}

static void test_clock_counts_milliseconds_and_keeps_the_remainder(void)
{
    perf_clock_t c;

    perf_clock_init(&c, 1000U);
    CHECK_EQ(perf_clock_advance(&c, 1000U + 5000U, HZ), 0U);
    CHECK_EQ(perf_clock_advance(&c, 1000U + 10000U, HZ), 0U);
    CHECK_EQ(perf_clock_advance(&c, 1000U + 15000U, HZ), 1U);
    CHECK_EQ(c.rem, 3000U);
    CHECK_EQ(perf_clock_advance(&c, 1000U + 15000U + 9000U, HZ), 2U);
    CHECK_EQ(c.rem, 0U);
    CHECK_EQ(perf_clock_advance(&c, 1000U + 15000U + 9000U + 12U * 12000U, HZ), 14U);
}

static void test_clock_does_not_drift_over_many_short_steps(void)
{
    perf_clock_t c;
    uint32_t now = 0U;
    unsigned i;

    perf_clock_init(&c, now);
    for (i = 0U; i < 20000U; i++) {
        now += 600000U / 7U;
        (void)perf_clock_advance(&c, now, HZ);
    }
    CHECK_EQ(c.ms, (uint32_t)(((uint64_t)20000U * (600000U / 7U)) / 12000U));
}

static void test_clock_survives_the_counter_wrap_and_runs_past_it(void)
{
    perf_clock_t c;
    uint32_t now = 0xFFFFFF00U;
    uint32_t ms;
    unsigned i;

    perf_clock_init(&c, now);
    now += 600000U;
    ms = perf_clock_advance(&c, now, HZ);
    CHECK_EQ(ms, 50U);
    for (i = 0U; i < 8000U; i++) {
        now += 600000U;
        ms = perf_clock_advance(&c, now, HZ);
    }
    CHECK_EQ(ms, 50U + 8000U * 50U);
}

static void test_clock_with_a_useless_frequency_stands_still_and_null_is_harmless(void)
{
    perf_clock_t c;

    perf_clock_init(&c, 0U);
    CHECK_EQ(perf_clock_advance(&c, 5000U, 999U), 0U);
    CHECK_EQ(c.last, 5000U);
    CHECK_EQ(perf_clock_advance(&c, 5000U + 12000U, HZ), 1U);
    perf_clock_init(NULL, 0U);
    CHECK_EQ(perf_clock_advance(NULL, 0U, HZ), 0U);
}


int main(void)
{
    RUN(test_compose_counts_ticks_and_the_position_inside_the_tick);
    RUN(test_compose_adds_the_tick_that_has_not_been_counted_yet);
    RUN(test_compose_is_monotonic_across_the_tick_boundary);
    RUN(test_unsigned_difference_survives_the_counter_wrap);
    RUN(test_stat_starts_empty_and_takes_the_first_sample_as_min_and_max);
    RUN(test_stat_tracks_min_max_and_sum);
    RUN(test_stat_sum_does_not_overflow_32_bits);
    RUN(test_stat_ignores_a_null_pointer);
    RUN(test_cycles_to_us_rounds_to_nearest);
    RUN(test_format_matches_the_documented_stat_line);
    RUN(test_format_of_an_empty_stat_shows_zeros);
    RUN(test_format_reports_the_needed_length_when_the_buffer_is_small);
    RUN(test_format_rejects_bad_arguments);
    RUN(test_clock_counts_milliseconds_and_keeps_the_remainder);
    RUN(test_clock_does_not_drift_over_many_short_steps);
    RUN(test_clock_survives_the_counter_wrap_and_runs_past_it);
    RUN(test_clock_with_a_useless_frequency_stands_still_and_null_is_harmless);
    return REPORT();
}
