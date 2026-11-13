#ifndef APP_H
#define APP_H

#include <stdint.h>
#include "plat_status.h"
#include "pots.h"
#include "servo.h"
#include "sdcard.h"
#include "tracker.h"
#include "perf.h"
#include "logger.h"


#define APP_LINE_MAX         48U
#define APP_SELFTEST_CHECKS  5U

#define APP_SD_WRITE_MIN_BLOCK  8192U
#define APP_TRACK_PERIOD_MS  50U
#define APP_STOW_LOCKOUT_MS  200U
#define APP_LOAD_MAX_US      50000U
#define APP_LOAD_MAX_MS      60000U

typedef struct {
    perf_stat_t sense;
    perf_stat_t track;
    perf_stat_t late;
    perf_stat_t period;
    perf_stat_t cmd;
    uint64_t    busy;
    uint32_t    start_ms;
    uint32_t    missed0;
    uint32_t    track_last;
    uint32_t    rx_errors;
} app_meter_t;

typedef struct {
    uint32_t track_next;
    uint32_t pass_last;
    uint32_t passes;
    uint32_t pass_min;
    uint32_t pass_max;
    uint32_t rx_bytes;
    uint8_t  tracking;
    uint8_t  load_active;
    pots_t   pots;
    servo_t  servo[2];
    sdcard_t sd;
    uint8_t  block[SDCARD_BLOCK_SIZE];
    tracker_t tracker;
    uint32_t track_period;
    uint32_t track_runs;
    uint32_t track_missed;
    uint32_t track_errors;
    uint32_t button_last;
    uint32_t button_edges;
    uint32_t button_accepted;
    uint32_t button_ignored;
    perf_stat_t stow_latency;
    perf_clock_t clock;
    logger_t logger;
    perf_stat_t log_flush;
    uint32_t load_us;
    uint32_t load_cycles;
    uint32_t load_end;
    uint8_t  log_on;
    app_meter_t meter;
    char     line[APP_LINE_MAX + 1U];
    uint8_t  line_len;
    uint8_t  line_overflow;
    plat_status_t adc_init;
    plat_status_t spi_init;
    plat_status_t pwm_init;
    plat_status_t button_init;
    plat_status_t rx_init;
} app_t;

void app_init(app_t *app);

void app_start(app_t *app);

int app_selftest(app_t *app);

void app_poll(app_t *app);

void app_execute(app_t *app, const char *line);

#endif /* APP_H */
