#include <stdint.h>
#include <string.h>
#include "minitest.h"
#include "app.h"
#include "plat_pwm.h"
#include "fake_uart.h"
#include "fake_pwm.h"
#include "fake_adc.h"
#include "fake_sdcard.h"

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
    CHECK_STR(run("help\n"), "CMD help OK commands=help,selftest,adc,servo,sd" EOL);
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

static void test_line_crlf_terminator_runs_the_command_once(void)
{
    start();
    CHECK_STR(run("help\r\n"), "CMD help OK commands=help,selftest,adc,servo,sd" EOL);
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
    CHECK_STR(run("help\n"), "CMD help OK commands=help,selftest,adc,servo,sd" EOL);
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
