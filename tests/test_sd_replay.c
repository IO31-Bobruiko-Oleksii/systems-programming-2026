#include <stdint.h>
#include <string.h>
#include "minitest.h"
#include "sdcard.h"
#include "replay_spi.h"
#include "vectors/sd_boot.inc"
#include "vectors/sd_nosd.inc"

static uint8_t blk[SDCARD_BLOCK_SIZE];

static uint16_t crc16_xmodem(const uint8_t *d, size_t n)
{
    uint16_t crc = 0U;
    size_t i;
    unsigned k;

    for (i = 0U; i < n; i++) {
        crc = (uint16_t)(crc ^ (uint16_t)((unsigned)d[i] << 8));
        for (k = 0U; k < 8U; k++) {
            crc = ((crc & 0x8000U) != 0U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void load_boot(void)
{
    replay_spi_load(SD_BOOT_MOSI, SD_BOOT_MISO, SD_BOOT_FLAGS, SD_BOOT_LEN);
}

static size_t data_start(void)
{
    size_t i;
    size_t k;

    for (i = 0U; i + 6U < SD_BOOT_LEN; i++) {
        if (SD_BOOT_MOSI[i] == 0x51U && (SD_BOOT_FLAGS[i] & REPLAY_FLAG_CS) != 0U) {
            for (k = i + 6U; k < SD_BOOT_LEN; k++) {
                if (SD_BOOT_MISO[k] == 0xFEU) {
                    return k + 1U;
                }
            }
        }
    }
    return 0U;
}

static void test_recording_is_what_the_capture_decoder_found(void)
{
    CHECK_EQ(SD_BOOT_LEN, 587);
    CHECK_EQ(SD_NOSD_LEN, 46);
    CHECK(data_start() != 0U);
    CHECK_EQ(data_start() + SDCARD_BLOCK_SIZE + 2U + 1U, SD_BOOT_LEN);
}

static void test_init_and_read_reproduce_the_uart_log(void)
{
    sdcard_t sd;
    size_t i;

    load_boot();
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    CHECK_EQ(sd.type, SD_TYPE_V2_HC);
    CHECK_EQ(sdcard_read_block(&sd, 0U, blk), PLAT_OK);
    CHECK(sdcard_boot_signature_ok(blk) == 1); 
    CHECK_EQ(blk[510], 0x55);
    CHECK_EQ(blk[511], 0xAA);
    for (i = 0U; i < 16U; i++) {
        CHECK_EQ(blk[i], 0);
    }
}

static void test_driver_sends_exactly_what_the_firmware_sent(void)
{
    sdcard_t sd;

    load_boot();
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    CHECK_EQ(sdcard_read_block(&sd, 0U, blk), PLAT_OK);
    CHECK_EQ(replay_spi_position(), SD_BOOT_LEN);
    CHECK_EQ(replay_spi_overruns(), 0);
    CHECK_EQ(replay_spi_tx_mismatches(), 0);
    CHECK_EQ(replay_spi_flag_mismatches(), 0);
}

static void test_block_equals_the_recorded_payload_and_its_crc(void)
{
    sdcard_t sd;
    size_t d = data_start();
    uint16_t crc_rx;

    load_boot();
    CHECK_EQ(sdcard_init(&sd), PLAT_OK);
    CHECK_EQ(sdcard_read_block(&sd, 0U, blk), PLAT_OK);
    CHECK(memcmp(blk, &SD_BOOT_MISO[d], SDCARD_BLOCK_SIZE) == 0);
    crc_rx = (uint16_t)(((unsigned)SD_BOOT_MISO[d + SDCARD_BLOCK_SIZE] << 8) | SD_BOOT_MISO[d + SDCARD_BLOCK_SIZE + 1U]);
    CHECK_EQ(crc16_xmodem(blk, SDCARD_BLOCK_SIZE), crc_rx);
    CHECK_EQ(blk[450], 0x04);
    CHECK_EQ(blk[454], 63);
}

static void test_card_that_never_answers_idle_gives_bus_error(void)
{
    sdcard_t sd;

    replay_spi_load(SD_NOSD_MOSI, SD_NOSD_MISO, SD_NOSD_FLAGS, SD_NOSD_LEN);
    CHECK_EQ(PLAT_ERR_BUS, 3);
    CHECK_EQ(sdcard_init(&sd), PLAT_ERR_BUS);
    CHECK_EQ(sd.type, SD_TYPE_NONE);
    CHECK_EQ(replay_spi_position(), SD_NOSD_LEN);
    CHECK_EQ(replay_spi_tx_mismatches(), 0);
    CHECK_EQ(replay_spi_flag_mismatches(), 0);
    CHECK_EQ(replay_spi_overruns(), 0);
}

int main(void)
{
    RUN(test_recording_is_what_the_capture_decoder_found);
    RUN(test_init_and_read_reproduce_the_uart_log);
    RUN(test_driver_sends_exactly_what_the_firmware_sent);
    RUN(test_block_equals_the_recorded_payload_and_its_crc);
    RUN(test_card_that_never_answers_idle_gives_bus_error);
    return REPORT();
}
