#ifndef APP_H
#define APP_H

#include <stdint.h>
#include "plat_status.h"
#include "pots.h"
#include "servo.h"
#include "sdcard.h"


#define APP_LINE_MAX         48U
#define APP_SELFTEST_CHECKS  5U

typedef struct {
    pots_t   pots;
    servo_t  servo[2];
    sdcard_t sd;
    uint8_t  block[SDCARD_BLOCK_SIZE];
    char     line[APP_LINE_MAX + 1U];
    uint8_t  line_len;
    uint8_t  line_overflow;
    plat_status_t adc_init;
    plat_status_t spi_init;
    plat_status_t pwm_init;
} app_t;

void app_init(app_t *app);

int app_selftest(app_t *app);

void app_poll(app_t *app);

void app_execute(app_t *app, const char *line);

#endif /* APP_H */
