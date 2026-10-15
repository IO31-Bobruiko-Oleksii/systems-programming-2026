#include <stdint.h>
#include "minitest.h"
#include "servo.h"
#include "plat_pwm.h"
#include "fake_pwm.h"

static void test_angle_to_us_vectors(void)
{
    CHECK_EQ(servo_angle_to_us(0),   1000);
    CHECK_EQ(servo_angle_to_us(1),   1006);
    CHECK_EQ(servo_angle_to_us(45),  1250);
    CHECK_EQ(servo_angle_to_us(90),  1500);
    CHECK_EQ(servo_angle_to_us(135), 1750);
    CHECK_EQ(servo_angle_to_us(180), 2000);
}

static void test_angle_to_us_clamps(void)
{
    CHECK_EQ(servo_angle_to_us(-1),       1000);
    CHECK_EQ(servo_angle_to_us(181),      2000);
    CHECK_EQ(servo_angle_to_us(INT16_MIN), 1000);
    CHECK_EQ(servo_angle_to_us(INT16_MAX), 2000);
}

static void test_init_moves_to_neutral(void)
{
    servo_t s;

    fake_pwm_reset();
    CHECK_EQ(servo_init(&s, PLAT_PWM_CH_SERVO2), PLAT_OK);
    CHECK_EQ(fake_pwm_calls(), 1);
    CHECK_EQ(fake_pwm_channel_at(0), PLAT_PWM_CH_SERVO2);
    CHECK_EQ(fake_pwm_pulse_at(0), 1500);
    CHECK_EQ(s.pulse_us, 1500);
}

static void test_command_sequence_reaches_platform_in_order(void)
{
    servo_t s;

    fake_pwm_reset();
    (void)servo_init(&s, PLAT_PWM_CH_SERVO1);
    CHECK_EQ(servo_set_angle(&s, 0), PLAT_OK);
    CHECK_EQ(servo_set_angle(&s, 180), PLAT_OK);
    CHECK_EQ(servo_set_angle(&s, 90), PLAT_OK);

    CHECK_EQ(fake_pwm_calls(), 4);
    CHECK_EQ(fake_pwm_pulse_at(1), 1000);
    CHECK_EQ(fake_pwm_pulse_at(2), 2000);
    CHECK_EQ(fake_pwm_pulse_at(3), 1500);
    CHECK_EQ(fake_pwm_channel_at(3), PLAT_PWM_CH_SERVO1);
    CHECK_EQ(s.pulse_us, 1500);
}

static void test_platform_error_is_returned_and_state_kept(void)
{
    servo_t s;

    fake_pwm_reset();
    (void)servo_init(&s, PLAT_PWM_CH_SERVO1);
    (void)servo_set_angle(&s, 180);

    fake_pwm_fail_next(PLAT_ERR_BUS);
    CHECK_EQ(servo_set_angle(&s, 0), PLAT_ERR_BUS);
    CHECK_EQ(s.pulse_us, 2000);

    CHECK_EQ(servo_set_angle(&s, 0), PLAT_OK);
    CHECK_EQ(s.pulse_us, 1000);
}

static void test_init_reports_platform_error(void)
{
    servo_t s;

    fake_pwm_reset();
    fake_pwm_fail_next(PLAT_ERR_TIMEOUT);
    CHECK_EQ(servo_init(&s, PLAT_PWM_CH_SERVO1), PLAT_ERR_TIMEOUT);
    CHECK_EQ(s.pulse_us, 0);
}

static void test_null_pointer_is_rejected_without_hardware_access(void)
{
    fake_pwm_reset();
    CHECK_EQ(servo_init(NULL, PLAT_PWM_CH_SERVO1), PLAT_ERR_ARG);
    CHECK_EQ(servo_set_angle(NULL, 90), PLAT_ERR_ARG);
    CHECK_EQ(fake_pwm_calls(), 0);
}

int main(void)
{
    RUN(test_angle_to_us_vectors);
    RUN(test_angle_to_us_clamps);
    RUN(test_init_moves_to_neutral);
    RUN(test_command_sequence_reaches_platform_in_order);
    RUN(test_platform_error_is_returned_and_state_kept);
    RUN(test_init_reports_platform_error);
    RUN(test_null_pointer_is_rejected_without_hardware_access);
    return REPORT();
}
