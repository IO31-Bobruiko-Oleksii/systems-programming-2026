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
#include "fake_button.h"
#include "fake_mem.h"
#include "fake_probe.h"

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
    fake_button_reset();
    fake_mem_reset();
    fake_probe_reset();
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
    CHECK_STR(run("help\n"), "CMD help OK commands=help,selftest,adc,servo,sd,time,track,deadband,zero,stow,resume,button,log,stats,load" EOL);
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
    CHECK_EQ(fake_time_calls(), 40002);
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

#define LOCKOUT  2400000U

static void press_at(uint32_t edge, uint32_t now)
{
    fake_button_press(edge);
    fake_time_set(now);
    fake_uart_clear_output();
    app_poll(&app);
}

static void test_init_remembers_a_failed_button_start(void)
{
    world();
    fake_button_fail_init(PLAT_ERR_BUS);
    app_init(&app);
    CHECK_EQ(app.button_init, PLAT_ERR_BUS);
    CHECK_STR(run("button\n"), "CMD button FAIL status=3" EOL);
}

static void test_a_press_stows_the_panel_and_reports_the_reaction_time(void)
{
    start_tracking();
    tick(PERIOD);
    CHECK_EQ(app.tracker.az, 91);
    press_at(PERIOD + 1000U, PERIOD + 1000U + 7200U);
    CHECK_STR(fake_uart_output(), "EVT stow t_us=50083 lat_us=600" EOL);
    CHECK_EQ(app.tracker.stowed, 1);
    CHECK_EQ(app.tracker.az, 90);
    CHECK_EQ(app.servo[0].pulse_us, 1500);
    CHECK_EQ(app.servo[1].pulse_us, 1500);
    CHECK_EQ(app.button_accepted, 1);
}

static void test_a_press_is_handled_before_a_due_tracking_run(void)
{
    start_tracking();
    press_at(PERIOD, PERIOD + 600U);
    CHECK_EQ(app.track_runs, 1);
    CHECK_EQ(fake_pwm_calls(), 6);
    CHECK_EQ(fake_pwm_pulse_at(2), 1500);
    CHECK_EQ(fake_pwm_pulse_at(3), 1500);
    CHECK_EQ(app.tracker.az, 90);
}

static void test_a_press_works_before_tracking_has_started(void)
{
    start();
    press_at(0U, 120U);
    CHECK_STR(fake_uart_output(), "EVT stow t_us=0 lat_us=10" EOL);
    CHECK_EQ(app.tracker.stowed, 1);
}

static void test_presses_inside_the_lockout_are_ignored_and_counted(void)
{
    start();
    press_at(1000U, 2000U);
    CHECK_EQ(app.button_accepted, 1);

    press_at(1000U + LOCKOUT / 2U, 1300000U);
    CHECK_STR(fake_uart_output(), "");
    CHECK_EQ(app.button_accepted, 1);
    CHECK_EQ(app.button_ignored, 1);

    press_at(1000U + LOCKOUT - 1U, 2500000U);
    CHECK_EQ(app.button_accepted, 1);
    CHECK_EQ(app.button_ignored, 2);

    press_at(1000U + LOCKOUT, 2500000U);
    CHECK_EQ(app.button_accepted, 2);
    CHECK_EQ(app.button_ignored, 2);
    CHECK_EQ(app.button_edges, 4);
}

static void test_edges_that_arrive_together_count_as_one_press(void)
{
    start();
    fake_button_press(5000U);
    fake_button_press(5100U);
    fake_button_press(5200U);
    fake_time_set(6000U);
    fake_uart_clear_output();
    app_poll(&app);
    CHECK_STR(fake_uart_output(), "EVT stow t_us=417 lat_us=83" EOL);
    CHECK_EQ(app.button_edges, 3);
    CHECK_EQ(app.button_accepted, 1);
    CHECK_EQ(app.button_ignored, 2);
}

static void test_a_servo_failure_during_the_stow_is_reported(void)
{
    start();
    fake_pwm_fail_next(PLAT_ERR_BUS);
    press_at(1000U, 2000U);
    CHECK_STR(fake_uart_output(), "EVT stow FAIL status=3" EOL);
    CHECK_EQ(app.button_accepted, 1);
    CHECK_EQ(app.stow_latency.count, 0);
    CHECK_EQ(app.tracker.stowed, 1);
}

static void test_reaction_times_are_collected_for_every_accepted_press(void)
{
    start();
    press_at(0U, 7200U);
    press_at(LOCKOUT, LOCKOUT + 14400U);
    press_at(LOCKOUT + 100U, LOCKOUT + 999999U);
    CHECK_EQ(app.stow_latency.count, 2);
    CHECK_EQ(app.stow_latency.min, 7200);
    CHECK_EQ(app.stow_latency.max, 14400);
    CHECK_EQ(app.stow_latency.sum, 21600);
}

static void test_cmd_button_reports_the_counters(void)
{
    start();
    CHECK_STR(run("button\n"), "CMD button OK edges=0 accepted=0 ignored=0" EOL);
    press_at(1000U, 2000U);
    press_at(2000U, 3000U);
    CHECK_STR(run("button\n"), "CMD button OK edges=2 accepted=1 ignored=1" EOL);
}

static void log_on(void)
{
    CHECK_STR(run("log on\n"), "CMD log OK on=1" EOL);
}

static void run_n(unsigned from, unsigned to)
{
    unsigned i;

    for (i = from; i <= to; i++) {
        tick(i * PERIOD);
    }
}

static void test_cmd_log_starts_off_and_reports_empty_counters(void)
{
    start_tracking();
    CHECK_STR(run("log\n"),
              "CMD log OK on=0 seq=0 stored=0 blocks=0 dropped=0 errors=0 flushes=0"
              " flush_max_us=0" EOL);
}

static void test_cmd_log_switches_on_and_off_and_rejects_bad_forms(void)
{
    start_tracking();
    log_on();
    CHECK_EQ(app.log_on, 1);
    CHECK_STR(run("log off\n"), "CMD log OK on=0" EOL);
    CHECK_EQ(app.log_on, 0);
    CHECK_STR(run("log x\n"), "CMD log FAIL reason=usage" EOL);
    CHECK_STR(run("log get\n"), "CMD log FAIL reason=usage" EOL);
    CHECK_STR(run("log on off\n"), "CMD log FAIL reason=usage" EOL);
    CHECK_STR(run("log flush now\n"), "CMD log FAIL reason=usage" EOL);
}

static void test_nothing_is_logged_while_the_log_is_off(void)
{
    start_tracking();
    run_n(1U, 5U);
    CHECK_EQ(app.tracker.az, 95);
    CHECK_EQ(logger_total(&app.logger), 0);
    CHECK_EQ(fake_sd_write_count(), 0);
}

static void test_a_run_that_moves_a_servo_stores_one_record(void)
{
    logger_record_t r;

    start_tracking();
    log_on();
    tick(PERIOD);
    CHECK_EQ(logger_total(&app.logger), 1);
    CHECK_EQ(logger_decode(app.logger.buf, &r), 1);
    CHECK_EQ(r.seq, 0);
    CHECK_EQ(r.t_ms, 50);
    CHECK_EQ(r.raw[0], 1025);
    CHECK_EQ(r.raw[1], 2050);
    CHECK_EQ(r.raw[2], 3074);
    CHECK_EQ(r.raw[3], 4095);
    CHECK_EQ(r.az, 91);
    CHECK_EQ(r.tilt, 89);
    CHECK_EQ(r.flags, LOGGER_F_AZ | LOGGER_F_TILT);
    CHECK_STR(fake_uart_output(), "CMD log OK on=1" EOL);
}

static void test_a_run_that_moves_nothing_stores_nothing(void)
{
    start_tracking();
    log_on();
    set_all_light(2000U);
    run_n(1U, 5U);
    CHECK_EQ(app.track_runs, 5);
    CHECK_EQ(logger_total(&app.logger), 0);
}

static void test_the_time_stamp_keeps_counting_past_the_wrap_of_the_cycle_counter(void)
{
    logger_record_t r;
    uint32_t t;

    start();
    fake_time_set(0xFFFF0000U);
    app_start(&app);
    log_on();
    t = 0xFFFF0000U + PERIOD;
    tick(t);
    CHECK_EQ(logger_decode(app.logger.buf, &r), 1);
    CHECK_EQ(r.t_ms, 50);
    tick(t + PERIOD);
    CHECK_EQ(logger_decode(app.logger.buf + LOGGER_RECORD_SIZE, &r), 1);
    CHECK_EQ(r.t_ms, 100);
}

static void test_the_sixteenth_record_writes_the_block_in_the_same_pass(void)
{
    start_tracking();
    log_on();
    run_n(1U, 15U);
    CHECK_EQ(fake_sd_write_count(), 0);
    run_n(16U, 16U);
    CHECK_EQ(fake_sd_write_count(), 1);
    CHECK_EQ(fake_sd_stored_index(), 8192);
    CHECK_EQ(app.logger.blocks, 1);
    CHECK_EQ(app.log_flush.count, 1);
    run_n(17U, 32U);
    CHECK_EQ(fake_sd_write_count(), 2);
    CHECK_EQ(fake_sd_stored_index(), 8193);
    CHECK_STR(run("log\n"),
              "CMD log OK on=1 seq=32 stored=32 blocks=2 dropped=0 errors=0 flushes=2"
              " flush_max_us=0" EOL);
}

static void test_a_run_to_the_end_stops_gives_ninety_records_and_then_the_log_goes_quiet(void)
{
    start_tracking();
    log_on();
    run_n(1U, 120U);
    CHECK_EQ(app.tracker.az, 180);
    CHECK_EQ(app.tracker.tilt, 0);
    CHECK_EQ(logger_total(&app.logger), 90);
    CHECK_EQ(logger_stored(&app.logger), 80);
    CHECK_EQ(app.logger.blocks, 5);
    CHECK_STR(run("log flush\n"), "CMD log-flush OK stored=90 blocks=5 us=0" EOL);
    CHECK_STR(run("log get 89\n"),
              "CMD log-get OK n=89 t_ms=4500 raw=1025,2050,3074,4095 az=180 tilt=0 flags=3 ok=1" EOL);
    CHECK_STR(run("log get 90\n"), "CMD log-get FAIL reason=range stored=90" EOL);
}

static void test_a_lone_record_is_written_when_it_is_ten_seconds_old(void)
{
    start_tracking();
    log_on();
    tick(PERIOD);
    set_all_light(2000U);
    run_n(2U, 200U);
    CHECK_EQ(logger_total(&app.logger), 1);
    CHECK_EQ(fake_sd_write_count(), 0);
    run_n(201U, 201U);
    CHECK_EQ(fake_sd_write_count(), 1);
    CHECK_EQ(logger_stored(&app.logger), 1);
    run_n(202U, 260U);
    CHECK_EQ(fake_sd_write_count(), 1);
}

static void test_nothing_is_written_while_the_log_is_off_even_if_records_are_waiting(void)
{
    start_tracking();
    log_on();
    tick(PERIOD);
    CHECK_STR(run("log off\n"), "CMD log OK on=0" EOL);
    set_all_light(2000U);
    run_n(2U, 260U);
    CHECK_EQ(logger_total(&app.logger), 1);
    CHECK_EQ(fake_sd_write_count(), 0);
    CHECK_EQ(app.logger.errors, 0);
    CHECK_STR(run("log on\n"), "CMD log OK on=1" EOL);
    run_n(261U, 261U);
    CHECK_EQ(fake_sd_write_count(), 1);
}

static void test_flushing_an_empty_log_does_not_touch_the_card_or_the_statistics(void)
{
    start_tracking();
    CHECK_STR(run("log flush\n"), "CMD log-flush OK stored=0 blocks=0 us=0" EOL);
    CHECK_EQ(fake_sd_transfer_calls(), 0);
    CHECK_EQ(app.log_flush.count, 0);
}

static void test_the_flush_time_is_measured_with_the_cycle_counter(void)
{
    start_tracking();
    log_on();
    fake_time_set_step(1200U);
    run_n(1U, 16U);
    fake_time_set_step(0U);
    CHECK_EQ(app.log_flush.count, 1);
    CHECK(app.log_flush.max > 0U);
    CHECK_EQ(app.log_flush.max, app.log_flush.min);
}

static void test_parking_is_logged_even_when_the_servos_do_not_move(void)
{
    logger_record_t r;

    start_tracking();
    log_on();
    set_all_light(0U);
    run_n(1U, 19U);
    CHECK_EQ(logger_total(&app.logger), 0);
    run_n(20U, 20U);
    CHECK_EQ(logger_total(&app.logger), 1);
    CHECK_EQ(logger_decode(app.logger.buf, &r), 1);
    CHECK_EQ(r.flags, LOGGER_F_PARK);
    CHECK_EQ(r.az, 90);
}

static void test_a_stow_press_is_logged_with_the_edge_time_of_the_reaction(void)
{
    logger_record_t r;

    start_tracking();
    log_on();
    tick(PERIOD);
    CHECK_EQ(logger_total(&app.logger), 1);
    press_at(PERIOD + 1200000U, PERIOD + 1200000U + 600U);
    CHECK_EQ(logger_total(&app.logger), 2);
    CHECK_EQ(logger_decode(app.logger.buf + LOGGER_RECORD_SIZE, &r), 1);
    CHECK_EQ(r.seq, 1);
    CHECK_EQ(r.t_ms, 150);
    CHECK_EQ(r.az, 90);
    CHECK_EQ(r.tilt, 90);
    CHECK_EQ(r.flags, LOGGER_F_STOW | LOGGER_F_AZ | LOGGER_F_TILT);
}

static void test_a_press_is_not_logged_while_the_log_is_off_or_when_the_servo_fails(void)
{
    start_tracking();
    press_at(PERIOD, PERIOD + 600U);
    CHECK_EQ(logger_total(&app.logger), 0);

    start_tracking();
    log_on();
    fake_pwm_fail_call(2U, PLAT_ERR_BUS);
    press_at(PERIOD, PERIOD + 600U);
    CHECK_STR(fake_uart_output(), "EVT stow FAIL status=3" EOL);
    CHECK_EQ(logger_total(&app.logger), 0);
}

static void test_cmd_log_flush_writes_a_partial_block_and_reports_the_time(void)
{
    start_tracking();
    log_on();
    run_n(1U, 3U);
    fake_time_set_step(0U);
    CHECK_STR(run("log flush\n"), "CMD log-flush OK stored=3 blocks=0 us=0" EOL);
    CHECK_EQ(fake_sd_write_count(), 1);
    CHECK_EQ(fake_sd_stored_index(), 8192);
    CHECK_STR(run("log flush\n"), "CMD log-flush OK stored=3 blocks=0 us=0" EOL);
    CHECK_EQ(fake_sd_write_count(), 1);
}

static void test_cmd_log_flush_reports_card_failures(void)
{
    start_tracking();
    log_on();
    run_n(1U, 3U);
    fake_sd_set_write_r1(0x04U);
    CHECK_STR(run("log flush\n"), "CMD log-flush FAIL status=3" EOL);
    CHECK_EQ(app.logger.errors, 1);
    CHECK_EQ(logger_stored(&app.logger), 0);

    start_tracking();
    log_on();
    run_n(1U, 3U);
    fake_sd_set_silent(1U);
    CHECK_STR(run("log flush\n"), "CMD log-flush FAIL status=2" EOL);
    CHECK_EQ(app.logger.errors, 1);
}

static void test_a_missing_card_is_retried_once_a_second_not_in_every_pass(void)
{
    unsigned calls;

    start_tracking();
    log_on();
    fake_sd_set_silent(1U);
    run_n(1U, 16U);
    CHECK_EQ(app.logger.errors, 1);
    calls = fake_sd_transfer_calls();
    run_n(17U, 35U);
    CHECK_EQ(app.logger.errors, 1);
    CHECK_EQ(fake_sd_transfer_calls(), calls);
    run_n(36U, 36U);
    CHECK_EQ(app.logger.errors, 2);
    CHECK(fake_sd_transfer_calls() > calls);
    CHECK_EQ(app.track_runs, 36);

    fake_sd_set_silent(0U);
    run_n(37U, 56U);
    CHECK_EQ(app.logger.errors, 2);
    CHECK_EQ(app.logger.blocks, 1);
    CHECK_EQ(app.logger.dropped, 40);
    run_n(57U, 57U);
    CHECK_EQ(logger_total(&app.logger), 17);
}

static void test_cmd_log_get_reads_a_stored_record_back_from_the_card(void)
{
    start_tracking();
    log_on();
    run_n(1U, 16U);
    CHECK_STR(run("log get 0\n"),
              "CMD log-get OK n=0 t_ms=50 raw=1025,2050,3074,4095 az=91 tilt=89 flags=3 ok=1" EOL);
    CHECK_STR(run("log get 15\n"),
              "CMD log-get OK n=15 t_ms=800 raw=1025,2050,3074,4095 az=106 tilt=74 flags=3 ok=1" EOL);
}

static void test_cmd_log_get_refuses_what_is_not_on_the_card(void)
{
    start_tracking();
    log_on();
    run_n(1U, 5U);
    CHECK_STR(run("log get 0\n"), "CMD log-get FAIL reason=range stored=0" EOL);
    CHECK_STR(run("log flush\n"), "CMD log-flush OK stored=5 blocks=0 us=0" EOL);
    CHECK_STR(run("log get 5\n"), "CMD log-get FAIL reason=range stored=5" EOL);
    CHECK_STR(run("log get -1\n"), "CMD log-get FAIL reason=usage" EOL);
    CHECK_STR(run("log get x\n"), "CMD log-get FAIL reason=usage" EOL);
}

static void test_cmd_log_get_reports_card_and_check_errors(void)
{
    start_tracking();
    log_on();
    run_n(1U, 16U);
    fake_sd_set_read_token(0x08U);
    CHECK_STR(run("log get 1\n"), "CMD log-get FAIL step=read status=3" EOL);
    fake_sd_set_read_token(0xFEU);
    fake_sd_set_block(8192U, card_block);
    CHECK_STR(run("log get 1\n"),
              "CMD log-get OK n=1 t_ms=336398079 raw=8731,12329,15927,19525 az=83 tilt=90"
              " flags=97 ok=0" EOL);
    app.sd.type = SD_TYPE_NONE;
    fake_sd_set_silent(1U);
    CHECK_STR(run("log get 1\n"), "CMD log-get FAIL step=init status=2" EOL);
}

static void stats_step(uint32_t at, uint32_t step)
{
    fake_time_set(at);
    fake_time_set_step(step);
    app_poll(&app);
    fake_time_set_step(0U);
}

static void test_every_tracking_pass_records_the_gap_since_the_previous_one(void)
{
    start_tracking();
    tick(1000U);
    tick(1500U);
    tick(4500U);
    CHECK_EQ(app.passes, 3);
    CHECK_EQ(app.pass_min, 500);
    CHECK_EQ(app.pass_max, 3000);
}

static void test_the_first_gap_and_the_first_period_start_at_app_start(void)
{
    start();
    fake_time_set(1000000U);
    app_start(&app);
    tick(1000000U + PERIOD);
    CHECK_EQ(app.pass_max, PERIOD);
    CHECK_EQ(app.meter.period.min, PERIOD);
}

static void test_the_loop_line_shows_the_shortest_the_average_and_the_longest_pass(void)
{
    start_tracking();
    tick(120000U);
    tick(180000U);
    tick(480000U);
    fake_uart_clear_output();
    app_execute(&app, "stats");
    CHECK(strstr(fake_uart_output(), "STAT name=loop n=3 min_us=5000 avg_us=13333 max_us=25000" EOL) != NULL);
}

static void test_passes_are_not_counted_before_tracking_has_started(void)
{
    start();
    tick(1000U);
    tick(2000U);
    CHECK_EQ(app.passes, 0);
    CHECK_EQ(app.pass_max, 0);
}

static void test_the_start_lateness_and_the_real_period_of_each_run_are_recorded(void)
{
    start_tracking();
    tick(PERIOD + 30U);
    tick(2U * PERIOD + 10U);
    CHECK_EQ(app.meter.late.count, 2);
    CHECK_EQ(app.meter.late.min, 10);
    CHECK_EQ(app.meter.late.max, 30);
    CHECK_EQ(app.meter.late.sum, 40);
    CHECK_EQ(app.meter.period.count, 2);
    CHECK_EQ(app.meter.period.min, PERIOD - 20U);
    CHECK_EQ(app.meter.period.max, PERIOD + 30U);
    tick(5U * PERIOD);
    CHECK_EQ(app.meter.late.max, 2U * PERIOD);
    CHECK_EQ(app.track_missed, 2);
}

static void test_sense_and_track_times_are_read_from_the_cycle_counter(void)
{
    start_tracking();
    stats_step(PERIOD, 100U);
    CHECK_EQ(app.meter.sense.count, 1);
    CHECK_EQ(app.meter.sense.min, 100);
    CHECK_EQ(app.meter.sense.max, 100);
    CHECK_EQ(app.meter.track.count, 1);
    CHECK_EQ(app.meter.track.min, 300);
    CHECK_EQ(app.meter.track.max, 300);
    CHECK_EQ(app.meter.busy, 300);
}

static void test_a_failed_conversion_leaves_the_sense_statistic_alone(void)
{
    start_tracking();
    fake_adc_fail_channel(1U, PLAT_ERR_TIMEOUT);
    tick(PERIOD);
    CHECK_EQ(app.track_errors, 1);
    CHECK_EQ(app.meter.sense.count, 0);
    CHECK_EQ(app.meter.track.count, 1);
}

static void test_a_dead_converter_is_not_timed_at_all(void)
{
    world();
    fake_adc_fail_init(PLAT_ERR_BUS);
    app_init(&app);
    app_start(&app);
    tick(PERIOD);
    CHECK_EQ(app.track_errors, 1);
    CHECK_EQ(app.meter.sense.count, 0);
}

static void test_a_command_is_timed_and_counts_as_busy_time(void)
{
    start_tracking();
    fake_time_set_step(50U);
    run("help\n");
    fake_time_set_step(0U);
    CHECK_EQ(app.meter.cmd.count, 1);
    CHECK_EQ(app.meter.cmd.min, 50);
    CHECK_EQ(app.meter.cmd.max, 50);
    CHECK_EQ(app.meter.busy, 50);
}

static void test_a_log_flush_in_the_task_loop_counts_as_busy_time(void)
{
    start_tracking();
    log_on();
    fake_time_set_step(1200U);
    run_n(1U, 16U);
    fake_time_set_step(0U);
    CHECK_EQ(app.log_flush.count, 1);
    CHECK_EQ(app.meter.track.count, 16);
    CHECK(app.meter.busy >= app.meter.track.sum + app.log_flush.sum);
}

static void test_received_bytes_and_bus_errors_are_counted_separately(void)
{
    start_tracking();
    fake_uart_clear_output();
    fake_uart_feed("ab");
    fake_uart_feed_error();
    fake_uart_feed("\n");
    while (fake_uart_in_pending() > 0U) {
        app_poll(&app);
    }
    CHECK_EQ(app.rx_bytes, 3);
    CHECK_EQ(app.meter.rx_errors, 1);
    CHECK_STR(fake_uart_output(), "CMD ab FAIL reason=unknown" EOL);
    app_poll(&app);
    app_poll(&app);
    CHECK_EQ(app.rx_bytes, 3);
    CHECK_EQ(app.meter.rx_errors, 1);
}

static void test_cmd_stats_prints_every_statistic_and_ends_with_the_verdict(void)
{
    start_tracking();
    stats_step(PERIOD, 12000U);
    CHECK_STR(run("stats\n"),
              "STAT name=loop n=7 min_us=0 avg_us=7714 max_us=50000" EOL
              "STAT name=sense n=1 min_us=1000 avg_us=1000 max_us=1000" EOL
              "STAT name=track n=1 min_us=3000 avg_us=3000 max_us=3000" EOL
              "STAT name=late n=1 min_us=0 avg_us=0 max_us=0" EOL
              "STAT name=period n=1 min_us=50000 avg_us=50000 max_us=50000" EOL
              "STAT name=cmd n=0 min_us=0 avg_us=0 max_us=0" EOL
              "STAT name=logflush n=0 min_us=0 avg_us=0 max_us=0" EOL
              "STAT name=stow n=0 min_us=0 avg_us=0 max_us=0" EOL
              "STAT load window_ms=54 busy_ms=3 load_pm=55" EOL
              "STAT count runs=1 missed=0 rx_bytes=6 rx_errors=0 passes=7" EOL
              "STAT rx init=0 dropped=0 ring_max=0 errors=0 isr_n=0 isr_avg=0 isr_max=0" EOL
              "STAT mem ram=12288 static=3668 heap_used=0 heap_res=512 stack_used=312"
              " stack_res=1024 stack_free=8316" EOL
              "CMD stats OK" EOL);
}

static void test_cmd_stats_before_tracking_shows_an_empty_loop_and_no_load(void)
{
    start();
    CHECK_STR(run("stats\n"),
              "STAT name=loop n=0 min_us=0 avg_us=0 max_us=0" EOL
              "STAT name=sense n=0 min_us=0 avg_us=0 max_us=0" EOL
              "STAT name=track n=0 min_us=0 avg_us=0 max_us=0" EOL
              "STAT name=late n=0 min_us=0 avg_us=0 max_us=0" EOL
              "STAT name=period n=0 min_us=0 avg_us=0 max_us=0" EOL
              "STAT name=cmd n=0 min_us=0 avg_us=0 max_us=0" EOL
              "STAT name=logflush n=0 min_us=0 avg_us=0 max_us=0" EOL
              "STAT name=stow n=0 min_us=0 avg_us=0 max_us=0" EOL
              "STAT load window_ms=0 busy_ms=0 load_pm=0" EOL
              "STAT count runs=0 missed=0 rx_bytes=6 rx_errors=0 passes=0" EOL
              "STAT rx init=0 dropped=0 ring_max=0 errors=0 isr_n=0 isr_avg=0 isr_max=0" EOL
              "STAT mem ram=12288 static=3668 heap_used=0 heap_res=512 stack_used=312"
              " stack_res=1024 stack_free=8316" EOL
              "CMD stats OK" EOL);
}

static void test_init_starts_the_receive_buffer_and_clears_its_statistics(void)
{
    start();
    CHECK_EQ(fake_uart_rx_starts(), 1);
    CHECK_EQ(fake_uart_rx_resets(), 1);
    CHECK_EQ(app.rx_init, PLAT_OK);
}

static void test_a_failed_receive_start_is_kept_and_shown(void)
{
    world();
    fake_uart_fail_rx_start(PLAT_ERR_BUS);
    app_init(&app);
    CHECK_EQ(app.rx_init, PLAT_ERR_BUS);
    CHECK(strstr(run("stats\n"), "STAT rx init=3 dropped=0") != NULL);
}

static void test_stats_report_the_receive_buffer_with_the_average_interrupt_time(void)
{
    plat_uart_rx_info_t ri = {3U, 17U, 2U, 10U, 700U, 90U};

    start();
    fake_uart_set_rx_info(&ri);
    CHECK(strstr(run("stats\n"),
                 "STAT rx init=0 dropped=3 ring_max=17 errors=2 isr_n=10 isr_avg=70 isr_max=90" EOL) != NULL);
}

static void test_stats_report_a_receive_info_that_cannot_be_read(void)
{
    start();
    fake_uart_fail_rx_info(PLAT_ERR_BUS);
    CHECK(strstr(run("stats\n"), "STAT rx FAIL init=0" EOL) != NULL);
}

static void test_a_new_window_clears_the_receive_statistics(void)
{
    start();
    CHECK_EQ(fake_uart_rx_resets(), 1);
    run("stats reset\n");
    CHECK_EQ(fake_uart_rx_resets(), 2);
    run("load 0 100\n");
    CHECK_EQ(fake_uart_rx_resets(), 3);
}

static void test_the_load_never_shows_more_than_one_thousand_permille(void)
{
    start_tracking();
    stats_step(PERIOD, 0U);
    app.meter.busy = 100U * PERIOD;
    CHECK(strstr(run("stats\n"), "STAT load window_ms=50 busy_ms=5000 load_pm=1000" EOL) != NULL);
}

static void test_a_clock_below_one_kilohertz_gives_no_window_and_no_division(void)
{
    start();
    fake_time_set_hz(999U);
    app.meter.busy = 5U;
    CHECK(strstr(run("stats\n"), "STAT load window_ms=0 busy_ms=0 load_pm=0" EOL) != NULL);
}

static void test_cmd_stats_shows_the_longest_memory_line_in_full(void)
{
    plat_mem_info_t big;

    start();
    big.ram_total = UINT32_MAX;
    big.static_used = UINT32_MAX;
    big.heap_reserved = UINT32_MAX;
    big.heap_used = UINT32_MAX;
    big.stack_reserved = UINT32_MAX;
    big.stack_used = UINT32_MAX;
    big.stack_free = UINT32_MAX;
    fake_mem_set(&big);
    CHECK(strstr(run("stats\n"),
                 "STAT mem ram=4294967295 static=4294967295 heap_used=4294967295"
                 " heap_res=4294967295 stack_used=4294967295 stack_res=4294967295"
                 " stack_free=4294967295" EOL "CMD stats OK" EOL) != NULL);
}

static void test_a_failing_memory_report_does_not_stop_the_rest(void)
{
    start();
    fake_mem_fail(PLAT_ERR_BUS);
    CHECK(strstr(run("stats\n"), "STAT mem FAIL status=3" EOL "CMD stats OK" EOL) != NULL);
}

static void test_cmd_stats_rejects_bad_forms(void)
{
    start();
    CHECK_STR(run("stats x\n"), "CMD stats FAIL reason=usage" EOL);
    CHECK_STR(run("stats reset now\n"), "CMD stats FAIL reason=usage" EOL);
    CHECK_STR(run("stats 1 2 3\n"), "CMD stats FAIL reason=usage" EOL);
}

static void test_stats_reset_starts_a_new_window(void)
{
    start_tracking();
    log_on();
    stats_step(PERIOD, 12000U);
    tick(4U * PERIOD);
    press_at(4U * PERIOD + 100U, 4U * PERIOD + 7300U);
    CHECK_EQ(app.track_missed, 2);
    CHECK_EQ(app.stow_latency.count, 1);
    CHECK_STR(run("log flush\n"), "CMD log-flush OK stored=3 blocks=0 us=0" EOL);
    CHECK_EQ(app.log_flush.count, 1);
    fake_uart_feed_error();
    app_poll(&app);
    CHECK_EQ(app.meter.rx_errors, 1);
    CHECK(app.meter.busy > 0U);
    CHECK_STR(run("stats reset\n"), "CMD stats-reset OK" EOL);
    CHECK_EQ(app.passes, 0);
    CHECK_EQ(app.pass_max, 0);
    CHECK_EQ(app.pass_min, UINT32_MAX);
    CHECK_EQ(app.meter.late.count, 0);
    CHECK_EQ(app.meter.period.count, 0);
    CHECK_EQ(app.meter.sense.count, 0);
    CHECK_EQ(app.meter.track.count, 0);
    CHECK_EQ(app.rx_bytes, 0);
    CHECK_EQ(app.meter.rx_errors, 0);
    CHECK_EQ(app.meter.busy, 0);
    CHECK_EQ(app.meter.start_ms, app.clock.ms);
    CHECK_EQ(app.stow_latency.count, 0);
    CHECK_EQ(app.log_flush.count, 0);
    CHECK_STR(app.stow_latency.name, "stow");
    CHECK_STR(app.log_flush.name, "logflush");
    CHECK_STR(app.meter.late.name, "late");
    CHECK_EQ(app.track_missed, 2);
    CHECK_EQ(app.track_runs, 2);
    stats_step(7U * PERIOD, 12000U);
    run("stats\n");
    CHECK(strstr(fake_uart_output(), "STAT load window_ms=154 busy_ms=3 load_pm=19" EOL) != NULL);
    CHECK(strstr(fake_uart_output(), "STAT count runs=1 missed=2 ") != NULL);
}

static void test_cmd_load_without_arguments_reports_the_state(void)
{
    start();
    CHECK_STR(run("load\n"), "CMD load OK us=0 active=0" EOL);
}

static void test_cmd_load_opens_a_window_and_reports_it(void)
{
    start_tracking();
    CHECK_STR(run("load 40 500\n"), "CMD load OK us=40 ms=500" EOL);
    CHECK_EQ(app.load_active, 1);
    CHECK_EQ(app.load_us, 40);
    CHECK_EQ(app.load_cycles, 480);
    CHECK_EQ(app.load_end, 6000000);
    fake_time_set_step(100U);
    CHECK_STR(run("load\n"), "CMD load OK us=40 active=1" EOL);
}

static void test_a_new_window_restarts_the_50ms_grid_at_the_command(void)
{
    start_tracking();
    fake_time_set(250000U);
    run("load 0 500\n");
    CHECK_EQ(app.track_next, 250000U + PERIOD);
    CHECK_EQ(app.meter.track_last, 250000U);
    fake_time_set(PERIOD + 1U);
    app_poll(&app);
    CHECK_EQ(app.track_runs, 0);
    fake_time_set(250000U + PERIOD);
    app_poll(&app);
    CHECK_EQ(app.track_runs, 1);
    CHECK_EQ(app.meter.late.count, 1);
    CHECK_EQ(app.meter.late.max, 0);
    CHECK_EQ(app.meter.period.max, PERIOD);
    CHECK_EQ(app.track_missed, 0);
}

static void test_cmd_load_rejects_bad_input_and_opens_nothing(void)
{
    start();
    CHECK_STR(run("load 10\n"), "CMD load FAIL reason=usage" EOL);
    CHECK_STR(run("load 10 20 30\n"), "CMD load FAIL reason=usage" EOL);
    CHECK_STR(run("load x 100\n"), "CMD load FAIL reason=usage" EOL);
    CHECK_STR(run("load 10 y\n"), "CMD load FAIL reason=usage" EOL);
    CHECK_STR(run("load -1 100\n"), "CMD load FAIL reason=usage" EOL);
    CHECK_STR(run("load 50001 100\n"), "CMD load FAIL reason=range" EOL);
    CHECK_STR(run("load 10 0\n"), "CMD load FAIL reason=range" EOL);
    CHECK_STR(run("load 10 60001\n"), "CMD load FAIL reason=range" EOL);
    CHECK_EQ(app.load_active, 0);
    CHECK_EQ(app.load_cycles, 0);
    CHECK_STR(run("load 50000 60000\n"), "CMD load OK us=50000 ms=60000" EOL);
    CHECK_EQ(app.load_active, 1);
}

static void test_a_pass_without_a_window_does_not_read_the_clock(void)
{
    start();
    fake_time_set_step(10U);
    app_poll(&app);
    CHECK_EQ(fake_time_calls(), 0);
}

static void test_every_pass_of_a_window_burns_the_requested_time_and_counts_it_as_busy(void)
{
    unsigned before;

    start_tracking();
    run("load 10 1000\n");
    CHECK_EQ(app.meter.busy, 0);
    fake_time_set_step(10U);
    before = fake_time_calls();
    app_poll(&app);
    CHECK_EQ(fake_time_calls() - before, 14);
    CHECK_EQ(app.meter.busy, 120);
    app_poll(&app);
    CHECK_EQ(app.meter.busy, 240);
}

static void test_the_burn_lasts_at_least_as_long_as_requested_when_the_clock_steps_unevenly(void)
{
    unsigned before;

    start_tracking();
    run("load 10 1000\n");
    fake_time_set_step(7U);
    before = fake_time_calls();
    app_poll(&app);
    CHECK_EQ(fake_time_calls() - before, 20);
    CHECK_EQ(app.meter.busy, 126);
}

static void test_a_window_of_zero_microseconds_only_measures(void)
{
    unsigned before;

    start_tracking();
    run("load 0 1000\n");
    fake_time_set_step(10U);
    before = fake_time_calls();
    app_poll(&app);
    CHECK_EQ(fake_time_calls() - before, 2);
    CHECK_EQ(app.meter.busy, 0);
    CHECK_EQ(app.load_active, 1);
}

static void test_the_window_ends_with_the_statistics_and_a_closing_line_last(void)
{
    static const char tail[] = "EVT load done us=10 ms=5" EOL;
    const char *out;
    unsigned before;
    int guard = 0;

    start_tracking();
    run("load 10 5\n");
    fake_time_set_step(1000U);
    fake_uart_clear_output();
    while (app.load_active != 0U && guard < 100) {
        app_poll(&app);
        guard++;
    }
    out = fake_uart_output();
    CHECK(guard < 100);
    CHECK_EQ(app.load_active, 0);
    CHECK_EQ(app.load_cycles, 0);
    CHECK(strstr(out, "STAT name=loop") != NULL);
    CHECK(strstr(out, "STAT count runs=") != NULL);
    CHECK(strstr(out, "STAT mem ram=") != NULL);
    CHECK(strlen(out) > strlen(tail));
    CHECK_STR(out + strlen(out) - strlen(tail), tail);
    before = fake_time_calls();
    app_poll(&app);
    CHECK_EQ(fake_time_calls() - before, 1);
}

static void test_the_end_of_the_window_is_found_across_the_counter_wrap(void)
{
    static const char tail[] = "EVT load done us=0 ms=5" EOL;
    const char *out;
    int guard = 0;

    start();
    fake_time_set(0xFFFFF000U);
    run("load 0 5\n");
    fake_time_set_step(1000U);
    fake_uart_clear_output();
    while (app.load_active != 0U && guard < 100) {
        app_poll(&app);
        guard++;
    }
    out = fake_uart_output();
    CHECK(guard < 100);
    CHECK(guard > 1);
    CHECK_EQ(app.load_active, 0);
    CHECK_STR(out + strlen(out) - strlen(tail), tail);
}

static void test_a_new_load_command_restarts_the_window_and_the_statistics(void)
{
    start_tracking();
    run("load 10 1000\n");
    fake_time_set_step(100U);
    run("help\n");
    CHECK_EQ(app.meter.cmd.count, 2);
    run("load 20 2000\n");
    CHECK_EQ(app.load_active, 1);
    CHECK_EQ(app.load_us, 20);
    CHECK_EQ(app.load_cycles, 240);
    CHECK_EQ(app.meter.cmd.count, 1);
}

static void test_a_clock_below_one_kilohertz_opens_a_window_that_closes_at_once(void)
{
    start();
    fake_time_set_hz(999U);
    CHECK_STR(run("load 40 500\n"), "CMD load OK us=40 ms=500" EOL);
    CHECK_EQ(app.load_cycles, 0);
    CHECK_EQ(app.load_end, 0);
    fake_time_set_step(1U);
    app_poll(&app);
    CHECK_EQ(app.load_active, 0);
}

static void test_init_starts_the_service_pins_and_leaves_them_alone_afterwards(void)
{
    start();
    CHECK_EQ(fake_probe_inits(), 1);
    CHECK_EQ(fake_probe_events(), 0);
    CHECK_EQ(fake_probe_level(PLAT_PROBE_T2), 0);
    CHECK_EQ(fake_probe_level(PLAT_PROBE_EVENT), 0);
    app_start(&app);
    run("help\n");
    CHECK_EQ(fake_probe_inits(), 1);
    CHECK_EQ(fake_probe_events(), 0);
}

static void test_a_run_holds_the_t2_pin_high_from_the_start_to_the_last_servo_write(void)
{
    start_tracking();
    tick(PERIOD - 1U);
    CHECK_EQ(fake_probe_events(), 0);
    tick(PERIOD);
    CHECK_EQ(fake_probe_events(), 2);
    CHECK_EQ(fake_probe_event(0)->probe, PLAT_PROBE_T2);
    CHECK_EQ(fake_probe_event(0)->level, 1);
    CHECK_EQ(fake_probe_event(0)->adc_calls, 0);
    CHECK_EQ(fake_probe_event(0)->pwm_calls, 2);
    CHECK_EQ(fake_probe_event(1)->probe, PLAT_PROBE_T2);
    CHECK_EQ(fake_probe_event(1)->level, 0);
    CHECK_EQ(fake_probe_event(1)->adc_calls, 4);
    CHECK_EQ(fake_probe_event(1)->pwm_calls, 4);
    CHECK_EQ(fake_probe_level(PLAT_PROBE_T2), 0);
    CHECK_EQ(fake_probe_level(PLAT_PROBE_EVENT), 0);
}

static void test_the_t2_pin_pulses_once_per_run_and_not_between_runs(void)
{
    start_tracking();
    tick(PERIOD);
    tick(PERIOD + 5U);
    tick(2U * PERIOD - 1U);
    CHECK_EQ(fake_probe_sets(PLAT_PROBE_T2), 1);
    CHECK_EQ(fake_probe_clears(PLAT_PROBE_T2), 1);
    tick(2U * PERIOD);
    CHECK_EQ(fake_probe_sets(PLAT_PROBE_T2), 2);
    CHECK_EQ(fake_probe_clears(PLAT_PROBE_T2), 2);
    tick(5U * PERIOD);
    CHECK_EQ(fake_probe_sets(PLAT_PROBE_T2), 3);
    CHECK_EQ(fake_probe_clears(PLAT_PROBE_T2), 3);
    CHECK_EQ(fake_probe_level(PLAT_PROBE_T2), 0);
}

static void test_the_t2_pin_still_ends_low_when_the_run_fails(void)
{
    start_tracking();
    fake_adc_fail_channel(2U, PLAT_ERR_TIMEOUT);
    tick(PERIOD);
    CHECK_EQ(app.track_errors, 1);
    CHECK_EQ(fake_probe_sets(PLAT_PROBE_T2), 1);
    CHECK_EQ(fake_probe_clears(PLAT_PROBE_T2), 1);
    CHECK_EQ(fake_probe_level(PLAT_PROBE_T2), 0);
}

static void test_the_t2_pin_is_low_again_before_the_log_flush_starts(void)
{
    start_tracking();
    log_on();
    run_n(1U, 16U);
    CHECK_EQ(fake_sd_write_count(), 1);
    CHECK_EQ(fake_probe_sets(PLAT_PROBE_T2), 16);
    CHECK_EQ(fake_probe_clears(PLAT_PROBE_T2), 16);
    CHECK_EQ(fake_probe_events(), 32);
    CHECK_EQ(fake_probe_event(31)->level, 0);
    CHECK_EQ(fake_probe_event(31)->sd_writes, 0);
}

static void test_a_press_holds_the_event_pin_high_from_the_start_of_the_stow_to_its_last_servo_write(void)
{
    start();
    press_at(1000U, 2000U);
    CHECK_EQ(fake_probe_events(), 2);
    CHECK_EQ(fake_probe_event(0)->probe, PLAT_PROBE_EVENT);
    CHECK_EQ(fake_probe_event(0)->level, 1);
    CHECK_EQ(fake_probe_event(0)->pwm_calls, 2);
    CHECK_EQ(fake_probe_event(1)->probe, PLAT_PROBE_EVENT);
    CHECK_EQ(fake_probe_event(1)->level, 0);
    CHECK_EQ(fake_probe_event(1)->pwm_calls, 4);
    CHECK_EQ(fake_probe_level(PLAT_PROBE_EVENT), 0);
    CHECK_EQ(fake_probe_sets(PLAT_PROBE_T2), 0);
}

static void test_a_press_inside_the_lockout_leaves_the_event_pin_alone(void)
{
    start();
    press_at(1000U, 2000U);
    press_at(1000U + LOCKOUT / 2U, 1300000U);
    CHECK_EQ(app.button_ignored, 1);
    CHECK_EQ(fake_probe_sets(PLAT_PROBE_EVENT), 1);
    CHECK_EQ(fake_probe_clears(PLAT_PROBE_EVENT), 1);
    press_at(1000U + LOCKOUT, 2500000U);
    CHECK_EQ(fake_probe_sets(PLAT_PROBE_EVENT), 2);
    CHECK_EQ(fake_probe_clears(PLAT_PROBE_EVENT), 2);
    CHECK_EQ(fake_probe_level(PLAT_PROBE_EVENT), 0);
}

static void test_a_failed_stow_still_ends_the_event_pulse(void)
{
    start();
    fake_pwm_fail_next(PLAT_ERR_BUS);
    press_at(1000U, 2000U);
    CHECK_STR(fake_uart_output(), "EVT stow FAIL status=3" EOL);
    CHECK_EQ(fake_probe_sets(PLAT_PROBE_EVENT), 1);
    CHECK_EQ(fake_probe_clears(PLAT_PROBE_EVENT), 1);
    CHECK_EQ(fake_probe_level(PLAT_PROBE_EVENT), 0);
}

static void test_a_press_and_a_due_run_in_one_pass_give_one_pulse_each_event_first(void)
{
    start_tracking();
    press_at(PERIOD, PERIOD + 600U);
    CHECK_EQ(fake_probe_events(), 4);
    CHECK_EQ(fake_probe_event(0)->probe, PLAT_PROBE_EVENT);
    CHECK_EQ(fake_probe_event(0)->level, 1);
    CHECK_EQ(fake_probe_event(1)->probe, PLAT_PROBE_EVENT);
    CHECK_EQ(fake_probe_event(1)->level, 0);
    CHECK_EQ(fake_probe_event(2)->probe, PLAT_PROBE_T2);
    CHECK_EQ(fake_probe_event(2)->level, 1);
    CHECK_EQ(fake_probe_event(3)->probe, PLAT_PROBE_T2);
    CHECK_EQ(fake_probe_event(3)->level, 0);
}

static void test_line_crlf_terminator_runs_the_command_once(void)
{
    start();
    CHECK_STR(run("help\r\n"), "CMD help OK commands=help,selftest,adc,servo,sd,time,track,deadband,zero,stow,resume,button,log,stats,load" EOL);
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
    CHECK_STR(run("help\n"), "CMD help OK commands=help,selftest,adc,servo,sd,time,track,deadband,zero,stow,resume,button,log,stats,load" EOL);
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
    RUN(test_init_remembers_a_failed_button_start);
    RUN(test_a_press_stows_the_panel_and_reports_the_reaction_time);
    RUN(test_a_press_is_handled_before_a_due_tracking_run);
    RUN(test_a_press_works_before_tracking_has_started);
    RUN(test_presses_inside_the_lockout_are_ignored_and_counted);
    RUN(test_edges_that_arrive_together_count_as_one_press);
    RUN(test_a_servo_failure_during_the_stow_is_reported);
    RUN(test_reaction_times_are_collected_for_every_accepted_press);
    RUN(test_cmd_button_reports_the_counters);
    RUN(test_cmd_log_starts_off_and_reports_empty_counters);
    RUN(test_cmd_log_switches_on_and_off_and_rejects_bad_forms);
    RUN(test_nothing_is_logged_while_the_log_is_off);
    RUN(test_a_run_that_moves_a_servo_stores_one_record);
    RUN(test_a_run_that_moves_nothing_stores_nothing);
    RUN(test_the_time_stamp_keeps_counting_past_the_wrap_of_the_cycle_counter);
    RUN(test_the_sixteenth_record_writes_the_block_in_the_same_pass);
    RUN(test_a_run_to_the_end_stops_gives_ninety_records_and_then_the_log_goes_quiet);
    RUN(test_a_lone_record_is_written_when_it_is_ten_seconds_old);
    RUN(test_nothing_is_written_while_the_log_is_off_even_if_records_are_waiting);
    RUN(test_flushing_an_empty_log_does_not_touch_the_card_or_the_statistics);
    RUN(test_the_flush_time_is_measured_with_the_cycle_counter);
    RUN(test_parking_is_logged_even_when_the_servos_do_not_move);
    RUN(test_a_stow_press_is_logged_with_the_edge_time_of_the_reaction);
    RUN(test_a_press_is_not_logged_while_the_log_is_off_or_when_the_servo_fails);
    RUN(test_cmd_log_flush_writes_a_partial_block_and_reports_the_time);
    RUN(test_cmd_log_flush_reports_card_failures);
    RUN(test_a_missing_card_is_retried_once_a_second_not_in_every_pass);
    RUN(test_cmd_log_get_reads_a_stored_record_back_from_the_card);
    RUN(test_cmd_log_get_refuses_what_is_not_on_the_card);
    RUN(test_cmd_log_get_reports_card_and_check_errors);
    RUN(test_cmd_sd_init_runs_the_whole_sequence_each_time);
    RUN(test_cmd_sd_usage);
    RUN(test_every_tracking_pass_records_the_gap_since_the_previous_one);
    RUN(test_the_first_gap_and_the_first_period_start_at_app_start);
    RUN(test_the_loop_line_shows_the_shortest_the_average_and_the_longest_pass);
    RUN(test_passes_are_not_counted_before_tracking_has_started);
    RUN(test_the_start_lateness_and_the_real_period_of_each_run_are_recorded);
    RUN(test_sense_and_track_times_are_read_from_the_cycle_counter);
    RUN(test_a_failed_conversion_leaves_the_sense_statistic_alone);
    RUN(test_a_dead_converter_is_not_timed_at_all);
    RUN(test_a_command_is_timed_and_counts_as_busy_time);
    RUN(test_a_log_flush_in_the_task_loop_counts_as_busy_time);
    RUN(test_received_bytes_and_bus_errors_are_counted_separately);
    RUN(test_cmd_stats_prints_every_statistic_and_ends_with_the_verdict);
    RUN(test_cmd_stats_before_tracking_shows_an_empty_loop_and_no_load);
    RUN(test_init_starts_the_receive_buffer_and_clears_its_statistics);
    RUN(test_a_failed_receive_start_is_kept_and_shown);
    RUN(test_stats_report_the_receive_buffer_with_the_average_interrupt_time);
    RUN(test_stats_report_a_receive_info_that_cannot_be_read);
    RUN(test_a_new_window_clears_the_receive_statistics);
    RUN(test_the_load_never_shows_more_than_one_thousand_permille);
    RUN(test_a_clock_below_one_kilohertz_gives_no_window_and_no_division);
    RUN(test_cmd_stats_shows_the_longest_memory_line_in_full);
    RUN(test_a_failing_memory_report_does_not_stop_the_rest);
    RUN(test_cmd_stats_rejects_bad_forms);
    RUN(test_stats_reset_starts_a_new_window);
    RUN(test_cmd_load_without_arguments_reports_the_state);
    RUN(test_cmd_load_opens_a_window_and_reports_it);
    RUN(test_a_new_window_restarts_the_50ms_grid_at_the_command);
    RUN(test_cmd_load_rejects_bad_input_and_opens_nothing);
    RUN(test_a_pass_without_a_window_does_not_read_the_clock);
    RUN(test_every_pass_of_a_window_burns_the_requested_time_and_counts_it_as_busy);
    RUN(test_the_burn_lasts_at_least_as_long_as_requested_when_the_clock_steps_unevenly);
    RUN(test_a_window_of_zero_microseconds_only_measures);
    RUN(test_the_window_ends_with_the_statistics_and_a_closing_line_last);
    RUN(test_the_end_of_the_window_is_found_across_the_counter_wrap);
    RUN(test_a_new_load_command_restarts_the_window_and_the_statistics);
    RUN(test_a_clock_below_one_kilohertz_opens_a_window_that_closes_at_once);
    RUN(test_init_starts_the_service_pins_and_leaves_them_alone_afterwards);
    RUN(test_a_run_holds_the_t2_pin_high_from_the_start_to_the_last_servo_write);
    RUN(test_the_t2_pin_pulses_once_per_run_and_not_between_runs);
    RUN(test_the_t2_pin_still_ends_low_when_the_run_fails);
    RUN(test_the_t2_pin_is_low_again_before_the_log_flush_starts);
    RUN(test_a_press_holds_the_event_pin_high_from_the_start_of_the_stow_to_its_last_servo_write);
    RUN(test_a_press_inside_the_lockout_leaves_the_event_pin_alone);
    RUN(test_a_failed_stow_still_ends_the_event_pulse);
    RUN(test_a_press_and_a_due_run_in_one_pass_give_one_pulse_each_event_first);
    RUN(test_line_crlf_terminator_runs_the_command_once);
    RUN(test_line_empty_and_blank_lines_are_ignored);
    RUN(test_line_waits_for_the_terminator);
    RUN(test_line_spaces_and_tabs_are_separators);
    RUN(test_line_length_limit_and_recovery);
    RUN(test_null_and_oversized_arguments_are_harmless);
    return REPORT();
}
