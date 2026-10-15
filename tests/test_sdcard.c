#include <stdint.h>
#include <string.h>
#include "minitest.h"
#include "sdcard.h"
#include "plat_spi.h"
#include "fake_sdcard.h"

#define CHECK_CMD(i, cmd, arg) \
    do { \
        CHECK_EQ(fake_sd_cmd_at(i), (cmd)); \
        CHECK_EQ(fake_sd_arg_at(i), (arg)); \
    } while (0)

static uint8_t buf[SDCARD_BLOCK_SIZE];

static void fill_boot_block(uint8_t *b)
{
    size_t i;

    for (i = 0U; i < SDCARD_BLOCK_SIZE; i++) {
        b[i] = (uint8_t)(i * 7U + 3U);
    }
    b[510] = 0x55U;
    b[511] = 0xAAU;
}


static void test_init_frames_match_the_spec_vectors(void)
{
    static const uint8_t cmd0[6] = { 0x40, 0x00, 0x00, 0x00, 0x00, 0x95 };
    static const uint8_t cmd8[6] = { 0x48, 0x00, 0x00, 0x01, 0xAA, 0x87 };
    sdcard_t sd;

    fake_sd_reset();
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    CHECK(memcmp(fake_sd_frame_at(0), cmd0, 6) == 0);
    CHECK(memcmp(fake_sd_frame_at(1), cmd8, 6) == 0);
}

static void test_init_sdhc_command_sequence(void)
{
    sdcard_t sd;

    fake_sd_reset();
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    CHECK_EQ(sd.type, SD_TYPE_V2_HC);

    CHECK_EQ(fake_sd_cmd_count(), 5);
    CHECK_CMD(0, 0, 0x00000000U);
    CHECK_CMD(1, 8, 0x000001AAU);
    CHECK_CMD(2, 55, 0x00000000U);
    CHECK_CMD(3, 41, 0x40000000U);
    CHECK_CMD(4, 58, 0x00000000U);
}

static void test_init_clock_and_chip_select_discipline(void)
{
    sdcard_t sd;
    size_t i;

    fake_sd_reset();
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);

    CHECK(fake_sd_idle_bytes_before_first_cmd() >= 10U);
    for (i = 0U; i < fake_sd_cmd_count(); i++) {
        CHECK_EQ(fake_sd_speed_at(i), PLAT_SPI_SPEED_SLOW);
    }
    CHECK_EQ(fake_sd_speed(), PLAT_SPI_SPEED_FAST);
    CHECK_EQ(fake_sd_cs_asserted(), 0);
}

static void test_init_waits_for_acmd41(void)
{
    sdcard_t sd;
    size_t i;

    fake_sd_reset();
    fake_sd_set_acmd41_busy_polls(3);
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);

    CHECK_EQ(fake_sd_cmd_count(), 11);
    for (i = 0U; i < 4U; i++) {
        CHECK_CMD(2U + 2U * i, 55, 0U);
        CHECK_CMD(3U + 2U * i, 41, 0x40000000U);
    }
    CHECK_CMD(10, 58, 0U);
    CHECK_EQ(sd.type, SD_TYPE_V2_HC);
}

static void test_init_version2_standard_capacity(void)
{
    sdcard_t sd;

    fake_sd_reset();
    fake_sd_set_high_capacity(0U);
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    CHECK_EQ(sd.type, SD_TYPE_V2);
}

static void test_init_version1_card(void)
{
    sdcard_t sd;

    fake_sd_reset();
    fake_sd_set_version1(1U);
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    CHECK_EQ(sd.type, SD_TYPE_V1);

    CHECK_EQ(fake_sd_cmd_count(), 4);
    CHECK_CMD(3, 41, 0U);
}

static void test_init_wrong_cmd8_echo_is_a_bus_error(void)
{
    sdcard_t sd;

    fake_sd_reset();
    fake_sd_set_cmd8_echo(0x000001A5U);
    CHECK_EQ(sdcard_init(&sd), PLAT_ERR_BUS);
    CHECK_EQ(sd.type, SD_TYPE_NONE);
    CHECK_EQ(fake_sd_cs_asserted(), 0);
}

static void test_init_voltage_not_accepted_is_a_bus_error(void)
{
    sdcard_t sd;

    fake_sd_reset();
    fake_sd_set_cmd8_echo(0x000000AAU);
    CHECK_EQ(sdcard_init(&sd), PLAT_ERR_BUS);
    CHECK_EQ(sd.type, SD_TYPE_NONE);
}

static void test_init_silent_card_times_out_after_cmd0_retries(void)
{
    sdcard_t sd;

    fake_sd_reset();
    fake_sd_set_silent(1U);
    CHECK_EQ(sdcard_init(&sd), PLAT_ERR_TIMEOUT);
    CHECK_EQ(sd.type, SD_TYPE_NONE);
    CHECK_EQ(fake_sd_cmd_count(), SDCARD_CMD0_TRIES);
    CHECK_EQ(fake_sd_cs_asserted(), 0);
}

static void test_init_card_that_never_leaves_idle_times_out(void)
{
    sdcard_t sd;

    fake_sd_reset();
    fake_sd_set_acmd41_busy_polls(-1);
    CHECK_EQ(sdcard_init(&sd), PLAT_ERR_TIMEOUT);
    CHECK_EQ(sd.type, SD_TYPE_NONE);
    CHECK_EQ(fake_sd_cmd_count(), 2U + 2U * SDCARD_ACMD41_TRIES);
    CHECK_EQ(fake_sd_cs_asserted(), 0);
}

static void test_init_failed_init_does_not_switch_to_fast_clock(void)
{
    sdcard_t sd;

    fake_sd_reset();
    fake_sd_set_cmd8_echo(0U);
    CHECK_EQ(sdcard_init(&sd), PLAT_ERR_BUS);
    CHECK_EQ(fake_sd_speed(), PLAT_SPI_SPEED_SLOW);
}

static int init_fails_cleanly_at(unsigned call)
{
    sdcard_t sd;

    fake_sd_reset();
    fake_sd_fail_transfer(call, PLAT_ERR_BUS);
    return sdcard_init(&sd) == PLAT_ERR_BUS && sd.type == SD_TYPE_NONE &&
           fake_sd_cs_asserted() == 0U;
}

static void test_init_bus_error_at_every_transfer_is_reported(void)
{
    sdcard_t sd;
    unsigned total;
    unsigned k;
    unsigned clean = 0U;

    fake_sd_reset();
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    total = fake_sd_transfer_calls();
    CHECK(total > 20U);

    for (k = 0U; k < total; k++) {
        if (init_fails_cleanly_at(k)) {
            clean++;
        } else {
            printf("    not clean at transfer %u\n", k);
        }
    }
    CHECK_EQ(clean, total);
}

static void test_init_null_pointer_is_rejected_without_bus_traffic(void)
{
    fake_sd_reset();
    CHECK_EQ(sdcard_init(NULL), PLAT_ERR_ARG);
    CHECK_EQ(fake_sd_transfer_calls(), 0);
}


static void test_read_block_sdhc_uses_block_address(void)
{
    sdcard_t sd;
    uint8_t expected[SDCARD_BLOCK_SIZE];
    size_t n;

    fill_boot_block(expected);
    fake_sd_reset();
    fake_sd_set_block(7U, expected);
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    n = fake_sd_cmd_count();

    memset(buf, 0, sizeof buf);
    CHECK_EQ(sdcard_read_block(&sd, 7U, buf), PLAT_OK);
    CHECK_EQ(fake_sd_cmd_count(), n + 1U);
    CHECK_CMD(n, 17, 7U);
    CHECK(memcmp(buf, expected, SDCARD_BLOCK_SIZE) == 0);
    CHECK_EQ(sdcard_boot_signature_ok(buf), 1);
    CHECK_EQ(fake_sd_cs_asserted(), 0);
}

static void test_read_block_standard_capacity_uses_byte_address(void)
{
    sdcard_t sd;
    uint8_t expected[SDCARD_BLOCK_SIZE];
    size_t n;

    fill_boot_block(expected);
    fake_sd_reset();
    fake_sd_set_high_capacity(0U);
    fake_sd_set_block(3U, expected);
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    n = fake_sd_cmd_count();

    CHECK_EQ(sdcard_read_block(&sd, 3U, buf), PLAT_OK);
    CHECK_CMD(n, 17, 3U * 512U);
    CHECK(memcmp(buf, expected, SDCARD_BLOCK_SIZE) == 0);
}

static void test_read_block_unwritten_block_reads_zeros(void)
{
    sdcard_t sd;
    size_t i;

    fake_sd_reset();
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    memset(buf, 0xA5, sizeof buf);
    CHECK_EQ(sdcard_read_block(&sd, 0U, buf), PLAT_OK);
    for (i = 0U; i < SDCARD_BLOCK_SIZE; i++) {
        if (buf[i] != 0U) {
            CHECK_EQ(i, -1);
            break;
        }
    }
    CHECK_EQ(sdcard_boot_signature_ok(buf), 0);
}

static void test_read_block_error_token_is_a_bus_error(void)
{
    sdcard_t sd;

    fake_sd_reset();
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    fake_sd_set_read_token(0x08U);
    CHECK_EQ(sdcard_read_block(&sd, 0U, buf), PLAT_ERR_BUS);
    CHECK_EQ(fake_sd_cs_asserted(), 0);
}

static void test_read_block_missing_start_token_times_out(void)
{
    sdcard_t sd;

    fake_sd_reset();
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    fake_sd_set_read_token(0xFFU);
    CHECK_EQ(sdcard_read_block(&sd, 0U, buf), PLAT_ERR_TIMEOUT);
    CHECK_EQ(fake_sd_cs_asserted(), 0);
}

static void test_read_block_command_rejected_is_a_bus_error(void)
{
    sdcard_t sd;

    fake_sd_reset();
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    fake_sd_set_read_r1(0x40U);
    CHECK_EQ(sdcard_read_block(&sd, 0U, buf), PLAT_ERR_BUS);
    CHECK_EQ(fake_sd_cs_asserted(), 0);
}

static int read_fails_cleanly_at(unsigned offset)
{
    sdcard_t sd;

    fake_sd_reset();
    if (sdcard_init(&sd) != PLAT_OK) {
        return 0;
    }
    fake_sd_fail_transfer(fake_sd_transfer_calls() + offset, PLAT_ERR_TIMEOUT);
    return sdcard_read_block(&sd, 0U, buf) == PLAT_ERR_TIMEOUT && fake_sd_cs_asserted() == 0U;
}

static void test_read_block_bus_error_at_every_transfer_is_reported(void)
{
    sdcard_t sd;
    unsigned before;
    unsigned total;
    unsigned k;
    unsigned clean = 0U;

    fake_sd_reset();
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    before = fake_sd_transfer_calls();
    CHECK_EQ(sdcard_read_block(&sd, 0U, buf), PLAT_OK);
    total = fake_sd_transfer_calls() - before;
    CHECK(total >= 5U);

    for (k = 0U; k < total; k++) {
        if (read_fails_cleanly_at(k)) {
            clean++;
        } else {
            printf("    not clean at transfer %u\n", k);
        }
    }
    CHECK_EQ(clean, total);
}

static void test_read_block_rejects_bad_arguments_without_bus_traffic(void)
{
    sdcard_t ready = { SD_TYPE_V2 };
    sdcard_t none = { SD_TYPE_NONE };

    fake_sd_reset();
    CHECK_EQ(sdcard_read_block(NULL, 0U, buf), PLAT_ERR_ARG);
    CHECK_EQ(sdcard_read_block(&ready, 0U, NULL), PLAT_ERR_ARG);
    CHECK_EQ(sdcard_read_block(&none, 0U, buf), PLAT_ERR_ARG);
    CHECK_EQ(sdcard_read_block(&ready, 0x00800000U, buf), PLAT_ERR_ARG);
    CHECK_EQ(fake_sd_transfer_calls(), 0);
}

static void test_read_block_sdhc_accepts_large_block_numbers(void)
{
    sdcard_t sd;
    size_t n;

    fake_sd_reset();
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    n = fake_sd_cmd_count();
    CHECK_EQ(sdcard_read_block(&sd, 0x00800000U, buf), PLAT_OK);
    CHECK_CMD(n, 17, 0x00800000U);
}

static void test_boot_signature_vectors(void)
{
    uint8_t b[SDCARD_BLOCK_SIZE];

    memset(b, 0, sizeof b);
    CHECK_EQ(sdcard_boot_signature_ok(b), 0);
    b[510] = 0x55U;
    CHECK_EQ(sdcard_boot_signature_ok(b), 0);
    b[511] = 0xAAU;
    CHECK_EQ(sdcard_boot_signature_ok(b), 1);
    b[510] = 0xAAU;
    b[511] = 0x55U;
    CHECK_EQ(sdcard_boot_signature_ok(b), 0);
    CHECK_EQ(sdcard_boot_signature_ok(NULL), 0);
}

int main(void)
{
    RUN(test_init_frames_match_the_spec_vectors);
    RUN(test_init_sdhc_command_sequence);
    RUN(test_init_clock_and_chip_select_discipline);
    RUN(test_init_waits_for_acmd41);
    RUN(test_init_version2_standard_capacity);
    RUN(test_init_version1_card);
    RUN(test_init_wrong_cmd8_echo_is_a_bus_error);
    RUN(test_init_voltage_not_accepted_is_a_bus_error);
    RUN(test_init_silent_card_times_out_after_cmd0_retries);
    RUN(test_init_card_that_never_leaves_idle_times_out);
    RUN(test_init_failed_init_does_not_switch_to_fast_clock);
    RUN(test_init_bus_error_at_every_transfer_is_reported);
    RUN(test_init_null_pointer_is_rejected_without_bus_traffic);
    RUN(test_read_block_sdhc_uses_block_address);
    RUN(test_read_block_standard_capacity_uses_byte_address);
    RUN(test_read_block_unwritten_block_reads_zeros);
    RUN(test_read_block_error_token_is_a_bus_error);
    RUN(test_read_block_missing_start_token_times_out);
    RUN(test_read_block_command_rejected_is_a_bus_error);
    RUN(test_read_block_bus_error_at_every_transfer_is_reported);
    RUN(test_read_block_rejects_bad_arguments_without_bus_traffic);
    RUN(test_read_block_sdhc_accepts_large_block_numbers);
    RUN(test_boot_signature_vectors);
    return REPORT();
}
