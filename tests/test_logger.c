#include <stdint.h>
#include <string.h>
#include "minitest.h"
#include "logger.h"
#include "sdcard.h"
#include "fake_sdcard.h"

static logger_t lg;
static sdcard_t sd;
static uint8_t scratch[SDCARD_BLOCK_SIZE];

static const uint16_t RAW[LOGGER_SENSORS] = { 1025U, 2050U, 3074U, 4095U };

static void world(void)
{
    fake_sd_reset();
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    logger_init(&lg);
}

static void add_many(unsigned n, uint32_t t0)
{
    unsigned i;

    for (i = 0U; i < n; i++) {
        CHECK_EQ(logger_add(&lg, t0 + i, RAW, (uint8_t)(90U + i), (uint8_t)(90U - i),
                            LOGGER_F_AZ | LOGGER_F_TILT), 0);
    }
}

static void test_a_record_is_32_bytes_little_endian_with_a_check_word(void)
{
    logger_record_t r = { 0x01020304U, 0x0A0B0C0DU, { 0x1122U, 0x3344U, 0x5566U, 0x7788U },
                          91U, 89U, 0x03U };
    uint8_t out[LOGGER_RECORD_SIZE];
    uint32_t sum = 0U;
    unsigned i;

    CHECK_EQ(LOGGER_RECORD_SIZE, 32);
    CHECK_EQ(LOGGER_RECORDS_PER_BLOCK, 16);
    logger_encode(out, &r);
    CHECK_EQ(out[0], 0x04);
    CHECK_EQ(out[3], 0x01);
    CHECK_EQ(out[4], 0x0D);
    CHECK_EQ(out[7], 0x0A);
    CHECK_EQ(out[8], 0x22);
    CHECK_EQ(out[9], 0x11);
    CHECK_EQ(out[14], 0x88);
    CHECK_EQ(out[15], 0x77);
    CHECK_EQ(out[16], 91);
    CHECK_EQ(out[17], 89);
    CHECK_EQ(out[18], 3);
    for (i = 19U; i < 28U; i++) {
        CHECK_EQ(out[i], 0);
    }
    for (i = 0U; i < 28U; i++) {
        sum += out[i];
    }
    sum ^= 0xA5A5A5A5UL;
    CHECK_EQ(out[28], (int)(sum & 0xFFU));
    CHECK_EQ(out[31], (int)(sum >> 24));
}

static void test_decode_returns_what_encode_stored_and_confirms_the_check_word(void)
{
    logger_record_t in = { 70000U, 123456U, { 0U, 4095U, 17U, 2048U }, 180U, 0U, 0x0CU };
    logger_record_t out;
    uint8_t buf[LOGGER_RECORD_SIZE];

    logger_encode(buf, &in);
    CHECK_EQ(logger_decode(buf, &out), 1);
    CHECK_EQ(out.seq, 70000);
    CHECK_EQ(out.t_ms, 123456);
    CHECK_EQ(out.raw[0], 0);
    CHECK_EQ(out.raw[1], 4095);
    CHECK_EQ(out.raw[2], 17);
    CHECK_EQ(out.raw[3], 2048);
    CHECK_EQ(out.az, 180);
    CHECK_EQ(out.tilt, 0);
    CHECK_EQ(out.flags, 0x0C);
}

static void test_decode_rejects_a_damaged_record_and_an_empty_slot(void)
{
    logger_record_t r = { 1U, 2U, { 3U, 4U, 5U, 6U }, 7U, 8U, 1U };
    uint8_t buf[LOGGER_RECORD_SIZE];
    unsigned i;

    logger_encode(buf, &r);
    for (i = 0U; i < LOGGER_RECORD_SIZE; i++) {
        uint8_t keep = buf[i];
        logger_record_t out;

        buf[i] ^= 0x01U;
        CHECK_EQ(logger_decode(buf, &out), 0);
        buf[i] = keep;
    }
    memset(buf, 0, sizeof buf);
    CHECK_EQ(logger_decode(buf, &r), 0);
    memset(buf, 0xFF, sizeof buf);
    CHECK_EQ(logger_decode(buf, &r), 0);
}

static void test_encode_and_decode_ignore_null_pointers(void)
{
    logger_record_t r = { 0U, 0U, { 0U, 0U, 0U, 0U }, 0U, 0U, 0U };
    uint8_t buf[LOGGER_RECORD_SIZE];

    logger_encode(NULL, &r);
    logger_encode(buf, NULL);
    CHECK_EQ(logger_decode(NULL, &r), 0);
    CHECK_EQ(logger_decode(buf, NULL), 0);
    logger_init(NULL);
    CHECK_EQ(logger_total(NULL), 0);
    CHECK_EQ(logger_stored(NULL), 0);
    logger_failed(NULL, 0U);
    CHECK_EQ(logger_due(NULL, 0U), 0);
}

static void test_records_get_consecutive_numbers_and_the_time_they_were_added(void)
{
    logger_record_t r;
    int i;

    world();
    CHECK_EQ(logger_total(&lg), 0);
    add_many(3U, 100U);
    CHECK_EQ(logger_total(&lg), 3);
    CHECK_EQ(logger_stored(&lg), 0);
    for (i = 0; i < 3; i++) {
        CHECK_EQ(logger_decode(lg.buf + i * LOGGER_RECORD_SIZE, &r), 1);
        CHECK_EQ(r.seq, i);
        CHECK_EQ(r.t_ms, 100 + i);
        CHECK_EQ(r.az, 90 + i);
        CHECK_EQ(r.tilt, 90 - i);
        CHECK_EQ(r.raw[3], 4095);
    }
}

static void test_add_refuses_missing_arguments(void)
{
    world();
    CHECK_EQ(logger_add(NULL, 0U, RAW, 1U, 1U, 0U), -1);
    CHECK_EQ(logger_add(&lg, 0U, NULL, 1U, 1U, 0U), -1);
    CHECK_EQ(logger_total(&lg), 0);
    CHECK_EQ(lg.dropped, 0);
}

static void test_a_block_is_due_when_full_or_when_its_oldest_record_is_ten_seconds_old(void)
{
    world();
    CHECK_EQ(logger_due(&lg, 50000U), 0);
    CHECK_EQ(logger_add(&lg, 1000U, RAW, 90U, 90U, LOGGER_F_AZ), 0);
    CHECK_EQ(logger_due(&lg, 1000U), 0);
    CHECK_EQ(logger_due(&lg, 10999U), 0);
    CHECK_EQ(logger_due(&lg, 11000U), 1);

    world();
    add_many(15U, 0U);
    CHECK_EQ(logger_due(&lg, 20U), 0);
    add_many(1U, 20U);
    CHECK_EQ(logger_due(&lg, 20U), 1);
}

static void test_a_full_block_is_written_to_the_log_area_and_the_buffer_starts_again(void)
{
    logger_record_t r;

    world();
    add_many(16U, 500U);
    CHECK_EQ(logger_flush(&lg, &sd, 600U), PLAT_OK);
    CHECK_EQ(fake_sd_write_count(), 1);
    CHECK_EQ(fake_sd_stored_index(), 8192);
    CHECK_EQ(lg.blocks, 1);
    CHECK_EQ(lg.count, 0);
    CHECK_EQ(logger_total(&lg), 16);
    CHECK_EQ(logger_stored(&lg), 16);
    CHECK_EQ(logger_decode(fake_sd_stored_block() + 15 * LOGGER_RECORD_SIZE, &r), 1);
    CHECK_EQ(r.seq, 15);
    CHECK_EQ(r.t_ms, 515);
    CHECK_EQ(logger_due(&lg, 600000U), 0);

    add_many(16U, 700U);
    CHECK_EQ(logger_flush(&lg, &sd, 800U), PLAT_OK);
    CHECK_EQ(fake_sd_stored_index(), 8193);
    CHECK_EQ(logger_decode(fake_sd_stored_block(), &r), 1);
    CHECK_EQ(r.seq, 16);

    add_many(2U, 900U);
    CHECK_EQ(logger_flush(&lg, &sd, 1000U), PLAT_OK);
    CHECK_EQ(fake_sd_stored_index(), 8194);
    CHECK_EQ(logger_decode(fake_sd_stored_block() + 1 * LOGGER_RECORD_SIZE, &r), 1);
    CHECK_EQ(r.seq, 33);
    CHECK_EQ(logger_decode(fake_sd_stored_block() + 2 * LOGGER_RECORD_SIZE, &r), 0);
    CHECK_EQ(fake_sd_stored_block()[2 * LOGGER_RECORD_SIZE], 0);
}

static void test_a_partial_block_is_rewritten_in_place_when_more_records_arrive(void)
{
    logger_record_t r;
    unsigned i;
    int empty = 1;

    world();
    add_many(3U, 0U);
    CHECK_EQ(logger_flush(&lg, &sd, 10000U), PLAT_OK);
    CHECK_EQ(fake_sd_stored_index(), 8192);
    CHECK_EQ(lg.blocks, 0);
    CHECK_EQ(logger_stored(&lg), 3);
    CHECK_EQ(logger_total(&lg), 3);
    CHECK_EQ(logger_decode(fake_sd_stored_block() + 2 * LOGGER_RECORD_SIZE, &r), 1);
    CHECK_EQ(logger_decode(fake_sd_stored_block() + 3 * LOGGER_RECORD_SIZE, &r), 0);
    for (i = 3U * LOGGER_RECORD_SIZE; i < SDCARD_BLOCK_SIZE; i++) {
        if (fake_sd_stored_block()[i] != 0U) {
            empty = 0;
        }
    }
    CHECK_EQ(empty, 1);
    CHECK_EQ(logger_due(&lg, 99999U), 0);

    add_many(2U, 20000U);
    CHECK_EQ(logger_total(&lg), 5);
    CHECK_EQ(logger_stored(&lg), 3);
    CHECK_EQ(logger_due(&lg, 29999U), 0);
    CHECK_EQ(logger_due(&lg, 30000U), 1);
    CHECK_EQ(logger_flush(&lg, &sd, 30000U), PLAT_OK);
    CHECK_EQ(fake_sd_write_count(), 2);
    CHECK_EQ(fake_sd_stored_index(), 8192);
    CHECK_EQ(logger_decode(fake_sd_stored_block() + 4 * LOGGER_RECORD_SIZE, &r), 1);
    CHECK_EQ(r.seq, 4);
    CHECK_EQ(logger_stored(&lg), 5);
}

static void test_flushing_with_nothing_new_does_not_touch_the_card(void)
{
    unsigned calls;

    world();
    CHECK_EQ(logger_flush(&lg, &sd, 0U), PLAT_OK);
    add_many(2U, 0U);
    CHECK_EQ(logger_flush(&lg, &sd, 0U), PLAT_OK);
    calls = fake_sd_transfer_calls();
    CHECK_EQ(logger_flush(&lg, &sd, 1U), PLAT_OK);
    CHECK_EQ(fake_sd_transfer_calls(), calls);
    CHECK_EQ(fake_sd_write_count(), 1);
}

static void test_a_failed_write_keeps_the_records_and_waits_a_second_before_the_next_try(void)
{
    world();
    add_many(16U, 0U);
    fake_sd_set_write_r1(0x04U);
    CHECK_EQ(logger_flush(&lg, &sd, 5000U), PLAT_ERR_BUS);
    CHECK_EQ(lg.errors, 1);
    CHECK_EQ(lg.count, 16);
    CHECK_EQ(lg.blocks, 0);
    CHECK_EQ(fake_sd_write_count(), 0);
    CHECK_EQ(logger_due(&lg, 5000U), 0);
    CHECK_EQ(logger_due(&lg, 5999U), 0);
    CHECK_EQ(logger_due(&lg, 6000U), 1);

    fake_sd_set_write_r1(0x00U);
    CHECK_EQ(logger_flush(&lg, &sd, 6000U), PLAT_OK);
    CHECK_EQ(lg.errors, 1);
    CHECK_EQ(lg.blocks, 1);
    CHECK_EQ(lg.retry_wait, 0);
    CHECK_EQ(fake_sd_write_count(), 1);
}

static void test_a_failure_noted_by_the_caller_has_the_same_back_off(void)
{
    world();
    add_many(1U, 0U);
    logger_failed(&lg, 20000U);
    CHECK_EQ(lg.errors, 1);
    CHECK_EQ(logger_due(&lg, 20500U), 0);
    CHECK_EQ(logger_due(&lg, 21000U), 1);
}

static void test_the_back_off_works_across_the_wrap_of_the_millisecond_counter(void)
{
    world();
    add_many(16U, 0U);
    logger_failed(&lg, 0xFFFFFF00U);
    CHECK_EQ(lg.retry_ms, 0xFFFFFF00U + LOGGER_RETRY_MS);
    CHECK_EQ(logger_due(&lg, 0xFFFFFFFFU), 0);
    CHECK_EQ(logger_due(&lg, lg.retry_ms), 1);
}

static void test_records_that_find_the_buffer_full_are_dropped_and_counted(void)
{
    world();
    add_many(16U, 0U);
    CHECK_EQ(logger_add(&lg, 99U, RAW, 1U, 2U, 0U), -1);
    CHECK_EQ(logger_add(&lg, 99U, RAW, 1U, 2U, 0U), -1);
    CHECK_EQ(lg.dropped, 2);
    CHECK_EQ(logger_total(&lg), 16);
}

static void test_the_log_area_has_a_fixed_size_and_stops_without_overwriting_anything(void)
{
    world();
    lg.blocks = LOGGER_BLOCKS;
    CHECK_EQ(logger_add(&lg, 1U, RAW, 1U, 2U, 0U), -1);
    CHECK_EQ(lg.dropped, 1);
    CHECK_EQ(logger_flush(&lg, &sd, 1U), PLAT_ERR_ARG);
    CHECK_EQ(fake_sd_write_count(), 0);

    world();
    lg.blocks = LOGGER_BLOCKS - 1U;
    add_many(16U, 0U);
    CHECK_EQ(logger_flush(&lg, &sd, 0U), PLAT_OK);
    CHECK_EQ(fake_sd_stored_index(), 8192U + LOGGER_BLOCKS - 1U);
    CHECK_EQ(logger_add(&lg, 1U, RAW, 1U, 2U, 0U), -1);
}

static void test_flush_refuses_missing_arguments(void)
{
    world();
    add_many(1U, 0U);
    CHECK_EQ(logger_flush(NULL, &sd, 0U), PLAT_ERR_ARG);
    CHECK_EQ(logger_flush(&lg, NULL, 0U), PLAT_ERR_ARG);
}

static void test_a_record_is_read_back_from_the_card_with_its_check_result(void)
{
    logger_record_t r;
    int valid = -1;

    world();
    add_many(16U, 300U);
    CHECK_EQ(logger_flush(&lg, &sd, 400U), PLAT_OK);
    CHECK_EQ(logger_read(&sd, 5U, scratch, &r, &valid), PLAT_OK);
    CHECK_EQ(valid, 1);
    CHECK_EQ(r.seq, 5);
    CHECK_EQ(r.t_ms, 305);
    CHECK_EQ(r.az, 95);
    CHECK_EQ(r.tilt, 85);
    CHECK_EQ(r.flags, LOGGER_F_AZ | LOGGER_F_TILT);

    CHECK_EQ(logger_read(&sd, 16U, scratch, &r, &valid), PLAT_OK);
    CHECK_EQ(valid, 0);
}

static void test_reading_reports_card_errors_and_bad_arguments(void)
{
    logger_record_t r;
    int valid = 7;

    world();
    fake_sd_set_read_token(0x08U);
    CHECK_EQ(logger_read(&sd, 0U, scratch, &r, &valid), PLAT_ERR_BUS);
    CHECK_EQ(valid, 7);
    CHECK_EQ(logger_read(NULL, 0U, scratch, &r, &valid), PLAT_ERR_ARG);
    CHECK_EQ(logger_read(&sd, 0U, NULL, &r, &valid), PLAT_ERR_ARG);
    CHECK_EQ(logger_read(&sd, 0U, scratch, NULL, &valid), PLAT_ERR_ARG);
    CHECK_EQ(logger_read(&sd, 0U, scratch, &r, NULL), PLAT_ERR_ARG);
}

int main(void)
{
    RUN(test_a_record_is_32_bytes_little_endian_with_a_check_word);
    RUN(test_decode_returns_what_encode_stored_and_confirms_the_check_word);
    RUN(test_decode_rejects_a_damaged_record_and_an_empty_slot);
    RUN(test_encode_and_decode_ignore_null_pointers);
    RUN(test_records_get_consecutive_numbers_and_the_time_they_were_added);
    RUN(test_add_refuses_missing_arguments);
    RUN(test_a_block_is_due_when_full_or_when_its_oldest_record_is_ten_seconds_old);
    RUN(test_a_full_block_is_written_to_the_log_area_and_the_buffer_starts_again);
    RUN(test_a_partial_block_is_rewritten_in_place_when_more_records_arrive);
    RUN(test_flushing_with_nothing_new_does_not_touch_the_card);
    RUN(test_a_failed_write_keeps_the_records_and_waits_a_second_before_the_next_try);
    RUN(test_a_failure_noted_by_the_caller_has_the_same_back_off);
    RUN(test_the_back_off_works_across_the_wrap_of_the_millisecond_counter);
    RUN(test_records_that_find_the_buffer_full_are_dropped_and_counted);
    RUN(test_the_log_area_has_a_fixed_size_and_stops_without_overwriting_anything);
    RUN(test_flush_refuses_missing_arguments);
    RUN(test_a_record_is_read_back_from_the_card_with_its_check_result);
    RUN(test_reading_reports_card_errors_and_bad_arguments);
    return REPORT();
}