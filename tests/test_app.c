#include <stdint.h>
#include <string.h>
#include "minitest.h"
#include "app.h"
#include "plat_pwm.h"
#include "fake_uart.h"
#include "fake_pwm.h"
#include "fake_adc.h"
#include "fake_sdcard.h"
#include "fake_time.h"

#define CHECK_STR(actual, expected) \
    do { \
        const char *a_ = (actual); \
        const char *e_ = (expected); \
        mt_checks++; \
        if (strcmp(a_, e_) != 0) { \
            mt_failures++; \
            printf("    FAIL %s:%d: text differs\n      got:      \"%s\"\n      expected: \"%s\"\n", \
                   __FILE__, __LINE__, a_, e_); \
        } \
    } while (0)

#define EOL "\r\n"

#define L_BEGIN   "SELFTEST BEGIN version=1" EOL
#define L_ADC_OK  "SELFTEST adc OK raw=1025,2050,3074,4095" EOL
#define L_SPI_OK  "SELFTEST spi OK" EOL
#define L_SD_OK   "SELFTEST sd OK type=V2HC sig=55AA" EOL
#define L_SV1_OK  "SELFTEST servo1 OK pulse_us=1500" EOL
#define L_SV2_OK  "SELFTEST servo2 OK pulse_us=1500" EOL

static app_t app;
static uint8_t card_block[SDCARD_BLOCK_SIZE];

static void world(void)
{
    size_t i;

    fake_uart_reset();
    fake_pwm_reset();
    fake_adc_reset();
    fake_sd_reset();
    fake_time_reset();
    fake_adc_set_raw(0U, 1025U);
    fake_adc_set_raw(1U, 2050U);
    fake_adc_set_raw(2U, 3074U);
    fake_adc_set_raw(3U, 4095U);
    for (i = 0U; i < SDCARD_BLOCK_SIZE; i++) {
        card_block[i] = (uint8_t)(i * 7U + 3U);
    }
    card_block[510] = 0x55U;
    card_block[511] = 0xAAU;
    fake_sd_set_block(0U, card_block);
}

static void start(void)
{
    world();
    app_init(&app);
}

static const char *run(const char *input)
{
    fake_uart_clear_output();
    fake_uart_feed(input);
    while (fake_uart_in_pending() > 0U) {
        app_poll(&app);
    }
    return fake_uart_output();
}

static void test_init_starts_platform_and_centres_servos_silently(void)
{
    start();
    CHECK_EQ(app.adc_init, PLAT_OK);
    CHECK_EQ(app.spi_init, PLAT_OK);
    CHECK_EQ(app.pwm_init, PLAT_OK);
    CHECK_EQ(fake_pwm_calls(), 2);
    CHECK_EQ(fake_pwm_channel_at(0), PLAT_PWM_CH_SERVO1);
    CHECK_EQ(fake_pwm_pulse_at(0), 1500);
    CHECK_EQ(fake_pwm_channel_at(1), PLAT_PWM_CH_SERVO2);
    CHECK_EQ(fake_pwm_pulse_at(1), 1500);
    CHECK_STR(fake_uart_output(), "");
}

static void test_init_remembers_platform_failures(void)
{
    world();
    fake_adc_fail_init(PLAT_ERR_BUS);
    fake_sd_fail_spi_init(PLAT_ERR_TIMEOUT);
    fake_pwm_fail_init(PLAT_ERR_ARG);
    app_init(&app);
    CHECK_EQ(app.adc_init, PLAT_ERR_BUS);
    CHECK_EQ(app.spi_init, PLAT_ERR_TIMEOUT);
    CHECK_EQ(app.pwm_init, PLAT_ERR_ARG);
    CHECK_EQ(fake_pwm_calls(), 0);
}

static void test_selftest_all_pass_golden_output(void)
{
    start();
    CHECK_EQ(app_selftest(&app), 0);
    CHECK_STR(fake_uart_output(),
              L_BEGIN L_ADC_OK L_SPI_OK L_SD_OK L_SV1_OK L_SV2_OK
              "SELFTEST END total=5 passed=5 failed=0" EOL);
}

static void test_selftest_adc_channel_failure_does_not_stop_the_others(void)
{
    start();
    fake_adc_fail_channel(2U, PLAT_ERR_TIMEOUT);
    CHECK_EQ(app_selftest(&app), 1);
    CHECK_STR(fake_uart_output(),
              L_BEGIN "SELFTEST adc FAIL status=2 at=2" EOL L_SPI_OK L_SD_OK L_SV1_OK L_SV2_OK
              "SELFTEST END total=5 passed=4 failed=1" EOL);
}

static void test_selftest_adc_calibration_failure(void)
{
    world();
    fake_adc_fail_init(PLAT_ERR_BUS);
    app_init(&app);
    CHECK_EQ(app_selftest(&app), 1);
    CHECK_STR(fake_uart_output(),
              L_BEGIN "SELFTEST adc FAIL status=3 at=-1" EOL L_SPI_OK L_SD_OK L_SV1_OK L_SV2_OK
              "SELFTEST END total=5 passed=4 failed=1" EOL);
    CHECK_EQ(fake_adc_calls(), 0);
}

static void test_selftest_spi_failure_takes_the_card_with_it(void)
{
    world();
    fake_sd_fail_spi_init(PLAT_ERR_BUS);
    app_init(&app);
    CHECK_EQ(app_selftest(&app), 2);
    CHECK_STR(fake_uart_output(),
              L_BEGIN L_ADC_OK "SELFTEST spi FAIL status=3" EOL "SELFTEST sd FAIL step=init status=3" EOL
              L_SV1_OK L_SV2_OK "SELFTEST END total=5 passed=3 failed=2" EOL);
    CHECK_EQ(fake_sd_transfer_calls(), 0);
}

static void test_selftest_missing_card(void)
{
    start();
    fake_sd_set_silent(1U);
    CHECK_EQ(app_selftest(&app), 1);
    CHECK(strstr(fake_uart_output(), "SELFTEST sd FAIL step=init status=2" EOL) != NULL);
}

static void test_selftest_card_read_error(void)
{
    start();
    fake_sd_set_read_token(0x08U);
    CHECK_EQ(app_selftest(&app), 1);
    CHECK(strstr(fake_uart_output(), "SELFTEST sd FAIL step=read status=3" EOL) != NULL);
}

static void test_selftest_card_without_boot_signature(void)
{
    start();
    card_block[510] = 0xAAU;
    card_block[511] = 0x55U;
    fake_sd_set_block(0U, card_block);
    CHECK_EQ(app_selftest(&app), 1);
    CHECK(strstr(fake_uart_output(), "SELFTEST sd FAIL step=signature sig=AA55" EOL) != NULL);
}

static void test_selftest_reports_the_card_type(void)
{
    start();
    fake_sd_set_version1(1U);
    CHECK_EQ(app_selftest(&app), 0);
    CHECK(strstr(fake_uart_output(), "SELFTEST sd OK type=V1 sig=55AA" EOL) != NULL);

    start();
    fake_sd_set_high_capacity(0U);
    CHECK_EQ(app_selftest(&app), 0);
    CHECK(strstr(fake_uart_output(), "SELFTEST sd OK type=V2 sig=55AA" EOL) != NULL);
}

static void test_selftest_pwm_failure_fails_both_servos(void)
{
    world();
    fake_pwm_fail_init(PLAT_ERR_BUS);
    app_init(&app);
    CHECK_EQ(app_selftest(&app), 2);
    CHECK_STR(fake_uart_output(),
              L_BEGIN L_ADC_OK L_SPI_OK L_SD_OK "SELFTEST servo1 FAIL status=3" EOL
              "SELFTEST servo2 FAIL status=3" EOL "SELFTEST END total=5 passed=3 failed=2" EOL);
}

static void test_selftest_servo_platform_error(void)
{
    start();
    fake_pwm_fail_next(PLAT_ERR_BUS);
    CHECK_EQ(app_selftest(&app), 1);
    CHECK(strstr(fake_uart_output(), "SELFTEST servo1 FAIL status=3" EOL L_SV2_OK) != NULL);
}

static void test_selftest_can_run_again(void)
{
    start();
    CHECK_EQ(app_selftest(&app), 0);
    fake_uart_clear_output();
    CHECK_EQ(app_selftest(&app), 0);
    CHECK(strstr(fake_uart_output(), L_BEGIN) == fake_uart_output());
}

static void test_cmd_help(void)
{
    start();
    CHECK_STR(run("help\n"), "CMD help OK commands=help,selftest,adc,servo,sd,time,track,deadband,zero,stow,resume" EOL);
}

static void test_cmd_unknown(void)
{
    start();
    CHECK_STR(run("frobnicate 1 2\n"), "CMD frobnicate FAIL reason=unknown" EOL);
}

static void test_cmd_selftest_prints_report_and_verdict(void)
{
    start();
    CHECK_STR(run("selftest\n"),
              L_BEGIN L_ADC_OK L_SPI_OK L_SD_OK L_SV1_OK L_SV2_OK
              "SELFTEST END total=5 passed=5 failed=0" EOL "CMD selftest OK failed=0" EOL);
    fake_sd_set_silent(1U);
    CHECK(strstr(run("selftest\n"), "CMD selftest FAIL failed=1" EOL) != NULL);
}

static void test_cmd_servo_moves_the_right_channel(void)
{
    start();
    CHECK_STR(run("servo 1 45\n"), "CMD servo OK ch=1 angle=45 pulse_us=1250" EOL);
    CHECK_EQ(fake_pwm_calls(), 3);
    CHECK_EQ(fake_pwm_channel_at(2), PLAT_PWM_CH_SERVO1);
    CHECK_EQ(fake_pwm_pulse_at(2), 1250);

    CHECK_STR(run("servo 2 180\n"), "CMD servo OK ch=2 angle=180 pulse_us=2000" EOL);
    CHECK_EQ(fake_pwm_channel_at(3), PLAT_PWM_CH_SERVO2);
    CHECK_EQ(fake_pwm_pulse_at(3), 2000);

    CHECK_STR(run("servo 1 0\n"), "CMD servo OK ch=1 angle=0 pulse_us=1000" EOL);
    CHECK_EQ(fake_pwm_pulse_at(4), 1000);
}

static void test_cmd_servo_rejects_bad_input_without_touching_hardware(void)
{
    static const char *const usage[] = {
        "servo\n", "servo 1\n", "servo 1 2 3\n", "servo 0 10\n", "servo 3 10\n",
        "servo x 10\n", "servo 1 x\n", "servo 1 -5\n", "servo 1 99999999999\n"
    };
    size_t i;

    start();
    for (i = 0U; i < sizeof usage / sizeof usage[0]; i++) {
        CHECK_STR(run(usage[i]), "CMD servo FAIL reason=usage" EOL);
    }
    CHECK_STR(run("servo 1 181\n"), "CMD servo FAIL reason=range" EOL);
    CHECK_EQ(fake_pwm_calls(), 2);
}

static void test_cmd_servo_platform_error_keeps_the_old_pulse(void)
{
    start();
    fake_pwm_fail_next(PLAT_ERR_BUS);
    CHECK_STR(run("servo 1 45\n"), "CMD servo FAIL status=3" EOL);
    CHECK_EQ(app.servo[0].pulse_us, 1500);
}

static void test_cmd_servo_refused_when_pwm_did_not_start(void)
{
    world();
    fake_pwm_fail_init(PLAT_ERR_BUS);
    app_init(&app);
    CHECK_STR(run("servo 1 45\n"), "CMD servo FAIL status=3" EOL);
    CHECK_EQ(fake_pwm_calls(), 0);
}

static void test_cmd_adc(void)
{
    start();
    CHECK_STR(run("adc\n"), "CMD adc OK raw=1025,2050,3074,4095" EOL);
    fake_adc_fail_channel(1U, PLAT_ERR_TIMEOUT);
    CHECK_STR(run("adc\n"), "CMD adc FAIL status=2 at=1" EOL);
}

static void test_cmd_adc_refused_after_failed_calibration(void)
{
    world();
    fake_adc_fail_init(PLAT_ERR_BUS);
    app_init(&app);
    CHECK_STR(run("adc\n"), "CMD adc FAIL status=3 at=-1" EOL);
    CHECK_EQ(fake_adc_calls(), 0);
}

static void test_cmd_sd_read_initialises_once_then_reads(void)
{
    start();
    fake_sd_set_block(7U, card_block);
    CHECK_STR(run("sd read 7\n"),
              "CMD sd-read OK block=7 sig=55AA first16=030A11181F262D343B424950575E656C" EOL);
    CHECK_EQ(fake_sd_cmd_count(), 6);
    CHECK_EQ(fake_sd_cmd_at(5), 17);
    CHECK_EQ(fake_sd_arg_at(5), 7U);

    (void)run("sd read 7\n");
    CHECK_EQ(fake_sd_cmd_count(), 7);
}

static void test_cmd_sd_read_of_an_empty_block(void)
{
    start();
    CHECK_STR(run("sd read 3\n"),
              "CMD sd-read OK block=3 sig=0000 first16=00000000000000000000000000000000" EOL);
}

static void test_cmd_sd_write_writes_a_patterned_block_and_reports_the_time(void)
{
    uint8_t expect[SDCARD_BLOCK_SIZE];
    size_t i;

    start();
    fake_time_set_step(12000U);
    CHECK_STR(run("sd write 8200\n"), "CMD sd-write OK block=8200 us=1000" EOL);
    for (i = 0U; i < SDCARD_BLOCK_SIZE; i++) {
        expect[i] = (uint8_t)(i + 8200U);
    }
    CHECK_EQ(fake_sd_write_count(), 1);
    CHECK_EQ(fake_sd_stored_index(), 8200);
    CHECK(memcmp(fake_sd_stored_block(), expect, SDCARD_BLOCK_SIZE) == 0);
}

static void test_cmd_sd_write_can_be_read_back_through_the_console(void)
{
    start();
    CHECK_STR(run("sd write 9000\n"), "CMD sd-write OK block=9000 us=0" EOL);
    CHECK_STR(run("sd read 9000\n"),
              "CMD sd-read OK block=9000 sig=2627 first16=28292A2B2C2D2E2F3031323334353637" EOL);
}

static void test_cmd_sd_write_refuses_the_protected_area(void)
{
    start();
    CHECK_STR(run("sd write 0\n"), "CMD sd-write FAIL reason=protected" EOL);
    CHECK_STR(run("sd write 8191\n"), "CMD sd-write FAIL reason=protected" EOL);
    CHECK_EQ(fake_sd_transfer_calls(), 0);
    CHECK_EQ(fake_sd_write_count(), 0);
}

static void test_cmd_sd_write_errors(void)
{
    start();
    CHECK_STR(run("sd write x\n"), "CMD sd-write FAIL reason=usage" EOL);
    CHECK_STR(run("sd write\n"), "CMD sd FAIL reason=usage" EOL);
    fake_sd_set_silent(1U);
    CHECK_STR(run("sd write 9000\n"), "CMD sd-write FAIL step=init status=2" EOL);

    start();
    fake_sd_set_write_r1(0x04U);
    CHECK_STR(run("sd write 9000\n"), "CMD sd-write FAIL step=write status=3" EOL);
    CHECK_EQ(fake_sd_write_count(), 0);
}

static void test_cmd_time_reports_the_cost_of_one_reading(void)
{
    start();
    fake_time_set_step(5U);
    CHECK_STR(run("time\n"), "CMD time OK hz=12000000 read_cycles=5 backsteps=0" EOL);
    CHECK_EQ(fake_time_calls(), 40000);
}

static void test_cmd_time_counts_readings_that_go_backwards(void)
{
    start();
    fake_time_set_step(0xFFFFFFFBU);
    CHECK_STR(run("time\n"),
              "CMD time OK hz=12000000 read_cycles=4294967295 backsteps=20000" EOL);
}

static void test_cmd_sd_read_errors(void)
{
    start();
    fake_sd_set_silent(1U);
    CHECK_STR(run("sd read 0\n"), "CMD sd-read FAIL step=init status=2" EOL);

    start();
    fake_sd_set_read_token(0x08U);
    CHECK_STR(run("sd read 0\n"), "CMD sd-read FAIL step=read status=3" EOL);

    world();
    fake_sd_fail_spi_init(PLAT_ERR_BUS);
    app_init(&app);
    CHECK_STR(run("sd read 0\n"), "CMD sd-read FAIL step=init status=3" EOL);
    CHECK_EQ(fake_sd_transfer_calls(), 0);
}

static void test_cmd_sd_init_runs_the_whole_sequence_each_time(void)
{
    start();
    CHECK_STR(run("sd init\n"), "CMD sd-init OK type=V2HC" EOL);
    CHECK_EQ(fake_sd_cmd_count(), 5);
    CHECK_STR(run("sd init\n"), "CMD sd-init OK type=V2HC" EOL);
    CHECK_EQ(fake_sd_cmd_count(), 10);
    CHECK_EQ(fake_sd_cmd_at(5), 0);

    fake_sd_set_silent(1U);
    CHECK_STR(run("sd init\n"), "CMD sd-init FAIL status=2" EOL);
    CHECK_EQ(app.sd.type, SD_TYPE_NONE);
}

static void test_cmd_sd_usage(void)
{
    start();
    CHECK_STR(run("sd\n"), "CMD sd FAIL reason=usage" EOL);
    CHECK_STR(run("sd read\n"), "CMD sd FAIL reason=usage" EOL);
    CHECK_STR(run("sd foo\n"), "CMD sd FAIL reason=usage" EOL);
    CHECK_STR(run("sd read 1 2\n"), "CMD sd FAIL reason=usage" EOL);
    CHECK_STR(run("sd read x\n"), "CMD sd-read FAIL reason=usage" EOL);
    CHECK_EQ(fake_sd_transfer_calls(), 0);
}

#define PERIOD  600000U

static void start_tracking(void)
{
    start();
    app_start(&app);
}

static void tick(uint32_t cycles)
{
    fake_time_set(cycles);
    app_poll(&app);
}

static void set_all_light(uint16_t raw)
{
    uint8_t ch;

    for (ch = 0U; ch < 4U; ch++) {
        fake_adc_set_raw(ch, raw);
    }
}

static void test_tracking_stays_off_until_app_start(void)
{
    start();
    fake_time_set(10U * PERIOD);
    app_poll(&app);
    CHECK_EQ(app.tracking, 0);
    CHECK_EQ(app.track_runs, 0);
    CHECK_EQ(fake_pwm_calls(), 2);
}

static void test_app_start_sets_the_first_release_one_period_ahead(void)
{
    start_tracking();
    CHECK_EQ(app.tracking, 1);
    CHECK_EQ(app.track_period, PERIOD);
    CHECK_EQ(app.track_next, PERIOD);
    tick(PERIOD - 1U);
    CHECK_EQ(app.track_runs, 0);
    tick(PERIOD);
    CHECK_EQ(app.track_runs, 1);
    CHECK_EQ(app.track_next, 2U * PERIOD);
    CHECK_EQ(app.track_missed, 0);
}

static void test_app_start_follows_the_clock_frequency_and_refuses_a_useless_one(void)
{
    start();
    fake_time_set_hz(1000U);
    app_start(&app);
    CHECK_EQ(app.track_period, APP_TRACK_PERIOD_MS);
    CHECK_EQ(app.tracking, 1);

    start();
    fake_time_set_hz(999U);
    app_start(&app);
    CHECK_EQ(app.track_period, 0);
    CHECK_EQ(app.tracking, 0);
    fake_time_set(10U * PERIOD);
    app_poll(&app);
    CHECK_EQ(app.track_runs, 0);

    app_start(NULL);
}

static void test_a_run_moves_the_servos_toward_the_light_and_stays_silent(void)
{
    start_tracking();
    tick(PERIOD);
    CHECK_EQ(app.tracker.az, 91);
    CHECK_EQ(app.tracker.tilt, 89);
    CHECK_EQ(app.servo[0].pulse_us, servo_angle_to_us(91));
    CHECK_EQ(app.servo[1].pulse_us, servo_angle_to_us(89));
    CHECK_EQ(fake_pwm_calls(), 4);
    CHECK_EQ(fake_pwm_channel_at(2), PLAT_PWM_CH_SERVO1);
    CHECK_EQ(fake_pwm_channel_at(3), PLAT_PWM_CH_SERVO2);
    CHECK_STR(fake_uart_output(), "");
}

static void test_ten_periods_give_ten_runs_and_ten_degrees(void)
{
    unsigned i;

    start_tracking();
    for (i = 1U; i <= 10U; i++) {
        tick(i * PERIOD);
    }
    CHECK_EQ(app.track_runs, 10);
    CHECK_EQ(app.tracker.az, 100);
    CHECK_EQ(app.tracker.tilt, 80);
}

static void test_a_late_pass_counts_the_releases_it_skipped(void)
{
    start_tracking();
    tick(PERIOD + 5U);
    CHECK_EQ(app.track_runs, 1);
    CHECK_EQ(app.track_missed, 0);
    CHECK_EQ(app.track_next, 2U * PERIOD);

    tick(4U * PERIOD + PERIOD / 2U);
    CHECK_EQ(app.track_runs, 2);
    CHECK_EQ(app.track_missed, 2);
    CHECK_EQ(app.track_next, 5U * PERIOD);

    tick(5U * PERIOD - 1U);
    CHECK_EQ(app.track_runs, 2);
    tick(5U * PERIOD);
    CHECK_EQ(app.track_runs, 3);
    CHECK_EQ(app.track_missed, 2);
}

static void test_the_schedule_survives_the_counter_wrap(void)
{
    uint32_t release;

    start();
    fake_time_set(0xFFFF0000U);
    app_start(&app);
    release = 0xFFFF0000U + PERIOD;
    CHECK(release < 0xFFFF0000U);
    CHECK_EQ(app.track_next, release);

    tick(0xFFFFFFFFU);
    CHECK_EQ(app.track_runs, 0);
    tick(release);
    CHECK_EQ(app.track_runs, 1);
    tick(release + PERIOD);
    CHECK_EQ(app.track_runs, 2);
    CHECK_EQ(app.track_missed, 0);
}

static void test_adc_failure_counts_an_error_and_leaves_the_servos_alone(void)
{
    start_tracking();
    fake_adc_fail_channel(1U, PLAT_ERR_TIMEOUT);
    tick(PERIOD);
    CHECK_EQ(app.track_runs, 1);
    CHECK_EQ(app.track_errors, 1);
    CHECK_EQ(fake_pwm_calls(), 2);
    CHECK_EQ(app.tracker.az, 90);
}

static void test_servo_failure_counts_an_error_and_the_next_run_repairs_it(void)
{
    start_tracking();
    fake_pwm_fail_next(PLAT_ERR_BUS);
    tick(PERIOD);
    CHECK_EQ(app.track_errors, 1);
    CHECK_EQ(app.servo[0].pulse_us, 1500);
    CHECK_EQ(app.servo[1].pulse_us, servo_angle_to_us(89));

    tick(2U * PERIOD);
    CHECK_EQ(app.track_errors, 1);
    CHECK_EQ(app.servo[0].pulse_us, servo_angle_to_us(92));
    CHECK_EQ(app.servo[1].pulse_us, servo_angle_to_us(88));
}

static void test_a_failing_tilt_servo_is_counted_too(void)
{
    start_tracking();
    fake_pwm_fail_call(3U, PLAT_ERR_BUS);
    tick(PERIOD);
    CHECK_EQ(app.track_errors, 1);
    CHECK_EQ(app.servo[0].pulse_us, servo_angle_to_us(91));
    CHECK_EQ(app.servo[1].pulse_us, 1500);
}

static void test_failed_platform_start_counts_as_an_error_in_every_run(void)
{
    world();
    fake_adc_fail_init(PLAT_ERR_BUS);
    app_init(&app);
    app_start(&app);
    tick(PERIOD);
    tick(2U * PERIOD);
    CHECK_EQ(app.track_runs, 2);
    CHECK_EQ(app.track_errors, 2);
    CHECK_EQ(fake_adc_calls(), 0);

    world();
    fake_pwm_fail_init(PLAT_ERR_ARG);
    app_init(&app);
    app_start(&app);
    tick(PERIOD);
    CHECK_EQ(app.track_errors, 1);
    CHECK_EQ(fake_pwm_calls(), 0);
}

static void test_twenty_dark_runs_park_the_panel_in_the_safe_position(void)
{
    unsigned i;

    start_tracking();
    tick(PERIOD);
    CHECK_EQ(app.tracker.az, 91);
    set_all_light(100U);
    for (i = 2U; i <= 20U; i++) {
        tick(i * PERIOD);
    }
    CHECK_EQ(app.tracker.parked, 0);
    CHECK_EQ(app.servo[0].pulse_us, servo_angle_to_us(91));

    tick(21U * PERIOD);
    CHECK_EQ(app.tracker.parked, 1);
    CHECK_EQ(app.servo[0].pulse_us, 1500);
    CHECK_EQ(app.servo[1].pulse_us, 1500);
}



static void test_cmd_track_reports_the_state(void)
{
    start_tracking();
    CHECK_STR(run("track\n"),
              "CMD track OK az=90 tilt=90 band=100 zero=90,90 parked=0 stowed=0 dark=0"
              " runs=0 missed=0 errors=0" EOL);
    tick(PERIOD);
    CHECK_STR(run("track\n"),
              "CMD track OK az=91 tilt=89 band=100 zero=90,90 parked=0 stowed=0 dark=0"
              " runs=1 missed=0 errors=0" EOL);
}

static void test_cmd_track_longest_line_is_not_cut_off(void)
{
    start();
    app.tracker.az = 180U;
    app.tracker.tilt = 180U;
    app.tracker.deadband = 4095U;
    app.tracker.zero_az = 180U;
    app.tracker.zero_tilt = 180U;
    app.tracker.parked = 1U;
    app.tracker.stowed = 1U;
    app.tracker.dark_count = 20U;
    app.track_runs = UINT32_MAX;
    app.track_missed = UINT32_MAX;
    app.track_errors = UINT32_MAX;
    CHECK_STR(run("track\n"),
              "CMD track OK az=180 tilt=180 band=4095 zero=180,180 parked=1 stowed=1 dark=20"
              " runs=4294967295 missed=4294967295 errors=4294967295" EOL);
}

static void test_a_command_and_a_due_task_run_in_the_same_pass(void)
{
    start_tracking();
    fake_time_set(PERIOD);
    CHECK_STR(run("track\n"),
              "CMD track OK az=91 tilt=89 band=100 zero=90,90 parked=0 stowed=0 dark=0"
              " runs=1 missed=0 errors=0" EOL);
}

static void test_cmd_deadband_sets_the_dead_zone_and_it_changes_the_tracking(void)
{
    start_tracking();
    CHECK_STR(run("deadband 4000\n"), "CMD deadband OK value=4000" EOL);
    CHECK_EQ(app.tracker.deadband, 4000);
    tick(PERIOD);
    CHECK_EQ(app.tracker.az, 90);
    CHECK_EQ(app.tracker.tilt, 90);
    CHECK_STR(run("deadband 0\n"), "CMD deadband OK value=0" EOL);
    tick(2U * PERIOD);
    CHECK_EQ(app.tracker.az, 91);
}

static void test_cmd_deadband_rejects_bad_input(void)
{
    start();
    CHECK_STR(run("deadband\n"), "CMD deadband FAIL reason=usage" EOL);
    CHECK_STR(run("deadband x\n"), "CMD deadband FAIL reason=usage" EOL);
    CHECK_STR(run("deadband 1 2\n"), "CMD deadband FAIL reason=usage" EOL);
    CHECK_STR(run("deadband 4096\n"), "CMD deadband FAIL reason=range" EOL);
    CHECK_STR(run("deadband 99999999999\n"), "CMD deadband FAIL reason=usage" EOL);
    CHECK_EQ(app.tracker.deadband, 100);
}

static void test_cmd_zero_changes_the_safe_position_and_moves_a_stowed_panel(void)
{
    start_tracking();
    CHECK_STR(run("zero 45 135\n"), "CMD zero OK az=45 tilt=135" EOL);
    CHECK_EQ(app.tracker.zero_az, 45);
    CHECK_EQ(app.tracker.zero_tilt, 135);
    CHECK_EQ(app.servo[0].pulse_us, 1500);
    CHECK_EQ(app.servo[1].pulse_us, 1500);

    CHECK_STR(run("stow\n"), "CMD stow OK az=45 tilt=135" EOL);
    CHECK_EQ(app.servo[0].pulse_us, servo_angle_to_us(45));
    CHECK_EQ(app.servo[1].pulse_us, servo_angle_to_us(135));

    CHECK_STR(run("zero 10 20\n"), "CMD zero OK az=10 tilt=20" EOL);
    CHECK_EQ(app.servo[0].pulse_us, servo_angle_to_us(10));
    CHECK_EQ(app.servo[1].pulse_us, servo_angle_to_us(20));
}

static void test_cmd_zero_rejects_bad_input(void)
{
    start();
    CHECK_STR(run("zero\n"), "CMD zero FAIL reason=usage" EOL);
    CHECK_STR(run("zero 1\n"), "CMD zero FAIL reason=usage" EOL);
    CHECK_STR(run("zero a 1\n"), "CMD zero FAIL reason=usage" EOL);
    CHECK_STR(run("zero 1 b\n"), "CMD zero FAIL reason=usage" EOL);
    CHECK_STR(run("zero 181 0\n"), "CMD zero FAIL reason=range" EOL);
    CHECK_STR(run("zero 0 181\n"), "CMD zero FAIL reason=range" EOL);
    CHECK_EQ(app.tracker.zero_az, 90);
    CHECK_EQ(app.tracker.zero_tilt, 90);
}

static void test_cmd_zero_reports_a_servo_failure(void)
{
    start();
    fake_pwm_fail_next(PLAT_ERR_BUS);
    CHECK_STR(run("zero 10 20\n"), "CMD zero FAIL status=3" EOL);
}

static void test_cmd_stow_holds_the_panel_until_resume(void)
{
    start_tracking();
    tick(PERIOD);
    CHECK_STR(run("stow\n"), "CMD stow OK az=90 tilt=90" EOL);
    CHECK_EQ(app.tracker.stowed, 1);
    CHECK_EQ(app.servo[0].pulse_us, 1500);
    CHECK_EQ(app.servo[1].pulse_us, 1500);

    tick(2U * PERIOD);
    CHECK_EQ(app.track_runs, 2);
    CHECK_EQ(app.tracker.az, 90);
    CHECK_EQ(app.servo[0].pulse_us, 1500);

    CHECK_STR(run("resume\n"), "CMD resume OK" EOL);
    CHECK_EQ(app.tracker.stowed, 0);
    tick(3U * PERIOD);
    CHECK_EQ(app.tracker.az, 91);
    CHECK_EQ(app.servo[0].pulse_us, servo_angle_to_us(91));
}

static void test_cmd_stow_reports_a_servo_failure(void)
{
    start();
    fake_pwm_fail_next(PLAT_ERR_TIMEOUT);
    CHECK_STR(run("stow\n"), "CMD stow FAIL status=2" EOL);
    CHECK_EQ(app.tracker.stowed, 1);
}

static void test_cmd_servo_sets_the_position_the_tracker_continues_from(void)
{
    start_tracking();
    CHECK_STR(run("servo 1 30\n"), "CMD servo OK ch=1 angle=30 pulse_us=1167" EOL);
    CHECK_EQ(app.tracker.az, 30);
    CHECK_EQ(app.tracker.tilt, 90);
    CHECK_STR(run("servo 2 150\n"), "CMD servo OK ch=2 angle=150 pulse_us=1833" EOL);
    CHECK_EQ(app.tracker.tilt, 150);
    tick(PERIOD);
    CHECK_EQ(app.tracker.az, 31);
    CHECK_EQ(app.tracker.tilt, 149);
    CHECK_EQ(app.servo[0].pulse_us, servo_angle_to_us(31));
}

static void test_line_crlf_terminator_runs_the_command_once(void)
{
    start();
    CHECK_STR(run("help\r\n"), "CMD help OK commands=help,selftest,adc,servo,sd,time,track,deadband,zero,stow,resume" EOL);
}

static void test_line_empty_and_blank_lines_are_ignored(void)
{
    start();
    CHECK_STR(run("\n\r\n"), "");
    CHECK_STR(run("   \n"), "");
    CHECK_STR(run("\t\n"), "");
}

static void test_line_waits_for_the_terminator(void)
{
    start();
    CHECK_STR(run("serv"), "");
    CHECK_STR(run("o 1 90\n"), "CMD servo OK ch=1 angle=90 pulse_us=1500" EOL);
}

static void test_line_spaces_and_tabs_are_separators(void)
{
    start();
    CHECK_STR(run("  servo   2   0  \n"), "CMD servo OK ch=2 angle=0 pulse_us=1000" EOL);
    CHECK_STR(run("servo\t1\t90\n"), "CMD servo OK ch=1 angle=90 pulse_us=1500" EOL);
}

static void test_line_length_limit_and_recovery(void)
{
    char longest[APP_LINE_MAX + 2U];
    char too_long[APP_LINE_MAX + 3U];
    char expected[APP_LINE_MAX + 40U];

    memset(longest, 'x', APP_LINE_MAX);
    longest[APP_LINE_MAX] = '\n';
    longest[APP_LINE_MAX + 1U] = '\0';
    memset(too_long, 'x', APP_LINE_MAX + 1U);
    too_long[APP_LINE_MAX + 1U] = '\n';
    too_long[APP_LINE_MAX + 2U] = '\0';

    start();
    memset(expected, 0, sizeof expected);
    strcpy(expected, "CMD ");
    memset(expected + 4, 'x', APP_LINE_MAX);
    strcpy(expected + 4 + APP_LINE_MAX, " FAIL reason=unknown" EOL);
    CHECK_STR(run(longest), expected);

    CHECK_STR(run(too_long), "CMD ? FAIL reason=too-long" EOL);
    CHECK_STR(run("help\n"), "CMD help OK commands=help,selftest,adc,servo,sd,time,track,deadband,zero,stow,resume" EOL);
}

static void test_null_and_oversized_arguments_are_harmless(void)
{
    char oversized[APP_LINE_MAX + 2U];

    world();
    app_init(NULL);
    CHECK_EQ(app_selftest(NULL), -1);
    app_poll(NULL);
    app_execute(NULL, "help");
    start();
    app_execute(&app, NULL);
    memset(oversized, 'y', APP_LINE_MAX + 1U);
    oversized[APP_LINE_MAX + 1U] = '\0';
    app_execute(&app, oversized);
    CHECK_STR(fake_uart_output(), "");
}

int main(void)
{
    RUN(test_init_starts_platform_and_centres_servos_silently);
    RUN(test_init_remembers_platform_failures);
    RUN(test_selftest_all_pass_golden_output);
    RUN(test_selftest_adc_channel_failure_does_not_stop_the_others);
    RUN(test_selftest_adc_calibration_failure);
    RUN(test_selftest_spi_failure_takes_the_card_with_it);
    RUN(test_selftest_missing_card);
    RUN(test_selftest_card_read_error);
    RUN(test_selftest_card_without_boot_signature);
    RUN(test_selftest_reports_the_card_type);
    RUN(test_selftest_pwm_failure_fails_both_servos);
    RUN(test_selftest_servo_platform_error);
    RUN(test_selftest_can_run_again);
    RUN(test_cmd_help);
    RUN(test_cmd_unknown);
    RUN(test_cmd_selftest_prints_report_and_verdict);
    RUN(test_cmd_servo_moves_the_right_channel);
    RUN(test_cmd_servo_rejects_bad_input_without_touching_hardware);
    RUN(test_cmd_servo_platform_error_keeps_the_old_pulse);
    RUN(test_cmd_servo_refused_when_pwm_did_not_start);
    RUN(test_cmd_adc);
    RUN(test_cmd_adc_refused_after_failed_calibration);
    RUN(test_cmd_sd_read_initialises_once_then_reads);
    RUN(test_cmd_sd_read_of_an_empty_block);
    RUN(test_cmd_sd_read_errors);
    RUN(test_cmd_sd_write_writes_a_patterned_block_and_reports_the_time);
    RUN(test_cmd_sd_write_can_be_read_back_through_the_console);
    RUN(test_cmd_sd_write_refuses_the_protected_area);
    RUN(test_cmd_sd_write_errors);
    RUN(test_cmd_time_reports_the_cost_of_one_reading);
    RUN(test_cmd_time_counts_readings_that_go_backwards);
    RUN(test_tracking_stays_off_until_app_start);
    RUN(test_app_start_sets_the_first_release_one_period_ahead);
    RUN(test_app_start_follows_the_clock_frequency_and_refuses_a_useless_one);
    RUN(test_a_run_moves_the_servos_toward_the_light_and_stays_silent);
    RUN(test_ten_periods_give_ten_runs_and_ten_degrees);
    RUN(test_a_late_pass_counts_the_releases_it_skipped);
    RUN(test_the_schedule_survives_the_counter_wrap);
    RUN(test_adc_failure_counts_an_error_and_leaves_the_servos_alone);
    RUN(test_servo_failure_counts_an_error_and_the_next_run_repairs_it);
    RUN(test_a_failing_tilt_servo_is_counted_too);
    RUN(test_failed_platform_start_counts_as_an_error_in_every_run);
    RUN(test_twenty_dark_runs_park_the_panel_in_the_safe_position);
    RUN(test_cmd_track_reports_the_state);
    RUN(test_cmd_track_longest_line_is_not_cut_off);
    RUN(test_a_command_and_a_due_task_run_in_the_same_pass);
    RUN(test_cmd_deadband_sets_the_dead_zone_and_it_changes_the_tracking);
    RUN(test_cmd_deadband_rejects_bad_input);
    RUN(test_cmd_zero_changes_the_safe_position_and_moves_a_stowed_panel);
    RUN(test_cmd_zero_rejects_bad_input);
    RUN(test_cmd_zero_reports_a_servo_failure);
    RUN(test_cmd_stow_holds_the_panel_until_resume);
    RUN(test_cmd_stow_reports_a_servo_failure);
    RUN(test_cmd_servo_sets_the_position_the_tracker_continues_from);
    RUN(test_cmd_sd_init_runs_the_whole_sequence_each_time);
    RUN(test_cmd_sd_usage);
    RUN(test_line_crlf_terminator_runs_the_command_once);
    RUN(test_line_empty_and_blank_lines_are_ignored);
    RUN(test_line_waits_for_the_terminator);
    RUN(test_line_spaces_and_tabs_are_separators);
    RUN(test_line_length_limit_and_recovery);
    RUN(test_null_and_oversized_arguments_are_harmless);
    return REPORT();
}
