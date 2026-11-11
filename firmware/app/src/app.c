#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "logger.h"
#include "plat_adc.h"
#include "plat_button.h"
#include "plat_mem.h"
#include "plat_probe.h"
#include "plat_pwm.h"
#include "perf.h"
#include "plat_spi.h"
#include "plat_time.h"
#include "plat_uart.h"

#define EOL                "\r\n"
#define POLL_TIMEOUT_MS    5U
#define MAX_ARGS           4U
#define SELFTEST_VERSION   1U
#define TIME_PROBE_PAIRS   20000U

static void say(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) {
        if ((size_t)n >= sizeof buf) {
            n = (int)sizeof buf - 1;
        }
        (void)plat_uart_write((const uint8_t *)buf, (size_t)n);
    }
}

static const char *sd_type_name(sd_type_t t)
{
    switch (t) {
    case SD_TYPE_V1:    return "V1";
    case SD_TYPE_V2:    return "V2";
    case SD_TYPE_V2_HC: return "V2HC";
    default:            return "NONE";
    }
}

static void meter_reset(app_t *app)
{
    app_meter_t *m = &app->meter;

    perf_stat_init(&m->sense, "sense");
    perf_stat_init(&m->track, "track");
    perf_stat_init(&m->late, "late");
    perf_stat_init(&m->period, "period");
    perf_stat_init(&m->cmd, "cmd");
    perf_stat_init(&app->stow_latency, "stow");
    perf_stat_init(&app->log_flush, "logflush");
    m->busy = 0U;
    m->start_ms = app->clock.ms;
    m->missed0 = app->track_missed;
    app->passes = 0U;
    app->pass_min = UINT32_MAX;
    app->pass_max = 0U;
    app->rx_bytes = 0U;
    m->rx_errors = 0U;
}


void app_init(app_t *app)
{
    if (app == NULL) {
        return;
    }
    memset(app, 0, sizeof *app);
    plat_probe_init();
    app->adc_init = plat_adc_init();
    app->spi_init = plat_spi_init();
    app->pwm_init = plat_pwm_init();
    if (app->pwm_init == PLAT_OK) {
        app->pwm_init = servo_init(&app->servo[0], PLAT_PWM_CH_SERVO1);
    }
    if (app->pwm_init == PLAT_OK) {
        app->pwm_init = servo_init(&app->servo[1], PLAT_PWM_CH_SERVO2);
    }
    tracker_init(&app->tracker);
    logger_init(&app->logger);
    meter_reset(app);
    app->button_init = plat_button_init();    
}

void app_start(app_t *app)
{
    uint32_t now;
    if (app == NULL) {
        return;
    }
    app->track_period = (plat_time_hz() / 1000U) * APP_TRACK_PERIOD_MS;
    if (app->track_period == 0U) {
        return;
    }
    now = plat_time_cycles();
    app->track_next = now + app->track_period;
    perf_clock_init(&app->clock, now);
    app->pass_last = now;
    app->meter.track_last = now;
    app->tracking = 1U;
}

static int check_adc(app_t *app)
{
    int at = -1;
    plat_status_t st = app->adc_init;

    if (st == PLAT_OK) {
        st = pots_read_all(&app->pots, &at);
    }
    if (st != PLAT_OK) {
        say("SELFTEST adc FAIL status=%d at=%d" EOL, (int)st, at);
        return 0;
    }
    say("SELFTEST adc OK raw=%u,%u,%u,%u" EOL, (unsigned)app->pots.raw[0], (unsigned)app->pots.raw[1],
        (unsigned)app->pots.raw[2], (unsigned)app->pots.raw[3]);
    return 1;
}

static int check_spi(const app_t *app)
{
    if (app->spi_init != PLAT_OK) {
        say("SELFTEST spi FAIL status=%d" EOL, (int)app->spi_init);
        return 0;
    }
    say("SELFTEST spi OK" EOL);
    return 1;
}

static int check_sd(app_t *app)
{
    plat_status_t st = app->spi_init;

    if (st == PLAT_OK) {
        st = sdcard_init(&app->sd);
    }
    if (st != PLAT_OK) {
        say("SELFTEST sd FAIL step=init status=%d" EOL, (int)st);
        return 0;
    }
    st = sdcard_read_block(&app->sd, 0U, app->block);
    if (st != PLAT_OK) {
        say("SELFTEST sd FAIL step=read status=%d" EOL, (int)st);
        return 0;
    }
    if (!sdcard_boot_signature_ok(app->block)) {
        say("SELFTEST sd FAIL step=signature sig=%02X%02X" EOL, (unsigned)app->block[510], (unsigned)app->block[511]);
        return 0;
    }
    say("SELFTEST sd OK type=%s sig=%02X%02X" EOL, sd_type_name(app->sd.type), (unsigned)app->block[510],
        (unsigned)app->block[511]);
    return 1;
}

static int check_servo(app_t *app, unsigned index)
{
    plat_status_t st = app->pwm_init;

    if (st == PLAT_OK) {
        st = servo_set_angle(&app->servo[index], SERVO_NEUTRAL_DEG);
    }
    if (st != PLAT_OK) {
        say("SELFTEST servo%u FAIL status=%d" EOL, index + 1U, (int)st);
        return 0;
    }
    say("SELFTEST servo%u OK pulse_us=%u" EOL, index + 1U, (unsigned)app->servo[index].pulse_us);
    return 1;
}

int app_selftest(app_t *app)
{
    int passed = 0;

    if (app == NULL) {
        return -1;
    }
    say("SELFTEST BEGIN version=%u" EOL, SELFTEST_VERSION);
    passed += check_adc(app);
    passed += check_spi(app);
    passed += check_sd(app);
    passed += check_servo(app, 0U);
    passed += check_servo(app, 1U);
    say("SELFTEST END total=%u passed=%d failed=%d" EOL, APP_SELFTEST_CHECKS, passed,
        (int)APP_SELFTEST_CHECKS - passed);
    return (int)APP_SELFTEST_CHECKS - passed;
}

static unsigned tokenize(char *line, char *argv[], unsigned max)
{
    unsigned argc = 0U;
    char *p = line;

    while (argc < max) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        argv[argc++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return argc;
}

static int parse_uint(const char *s, uint32_t max, uint32_t *out)
{
    uint32_t v = 0U;

    if (*s == '\0') {
        return -1;
    }
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9') {
            return -1;
        }
        if (v > (UINT32_MAX - (uint32_t)(*s - '0')) / 10U) {
            return -1;
        }
        v = v * 10U + (uint32_t)(*s - '0');
    }
    if (v > max) {
        return -1;
    }
    *out = v;
    return 0;
}

static void cmd_help(void)
{
    say("CMD help OK commands=help,selftest,adc,servo,sd,time,track,deadband,zero,stow,resume,button,log,stats,load" EOL);
}

static void cmd_selftest(app_t *app)
{
    int failed = app_selftest(app);

    say("CMD selftest %s failed=%d" EOL, (failed == 0) ? "OK" : "FAIL", failed);
}

static void cmd_adc(app_t *app)
{
    int at = -1;
    plat_status_t st = app->adc_init;

    if (st == PLAT_OK) {
        st = pots_read_all(&app->pots, &at);
    }
    if (st != PLAT_OK) {
        say("CMD adc FAIL status=%d at=%d" EOL, (int)st, at);
        return;
    }
    say("CMD adc OK raw=%u,%u,%u,%u" EOL, (unsigned)app->pots.raw[0], (unsigned)app->pots.raw[1],
        (unsigned)app->pots.raw[2], (unsigned)app->pots.raw[3]);
}

static void cmd_servo(app_t *app, unsigned argc, char *argv[])
{
    uint32_t ch;
    uint32_t angle;
    plat_status_t st;

    if (argc != 3U || parse_uint(argv[1], UINT32_MAX, &ch) != 0 || (ch != 1U && ch != 2U) ||
        parse_uint(argv[2], UINT32_MAX, &angle) != 0) {
        say("CMD servo FAIL reason=usage" EOL);
        return;
    }
    if (angle > (uint32_t)SERVO_ANGLE_MAX) {
        say("CMD servo FAIL reason=range" EOL);
        return;
    }
    st = app->pwm_init;
    if (st == PLAT_OK) {
        st = servo_set_angle(&app->servo[ch - 1U], (int16_t)angle);
    }
    if (st != PLAT_OK) {
        say("CMD servo FAIL status=%d" EOL, (int)st);
        return;
    }
    if (ch == 1U) {
        app->tracker.az = (uint8_t)angle;
    } else {
        app->tracker.tilt = (uint8_t)angle;
    }
    say("CMD servo OK ch=%u angle=%u pulse_us=%u" EOL, (unsigned)ch, (unsigned)angle,
        (unsigned)app->servo[ch - 1U].pulse_us);
}

static plat_status_t sd_ensure_init(app_t *app)
{
    if (app->sd.type != SD_TYPE_NONE) {
        return PLAT_OK;
    }
    if (app->spi_init != PLAT_OK) {
        return app->spi_init;
    }
    return sdcard_init(&app->sd);
}

static void cmd_sd_init(app_t *app)
{
    plat_status_t st = app->spi_init;

    if (st == PLAT_OK) {
        st = sdcard_init(&app->sd);
    }
    if (st != PLAT_OK) {
        say("CMD sd-init FAIL status=%d" EOL, (int)st);
        return;
    }
    say("CMD sd-init OK type=%s" EOL, sd_type_name(app->sd.type));
}

static void cmd_sd_read(app_t *app, const char *arg)
{
    static const char hex[] = "0123456789ABCDEF";
    char first16[2U * 16U + 1U];
    uint32_t block;
    plat_status_t st;
    unsigned i;

    if (parse_uint(arg, UINT32_MAX, &block) != 0) {
        say("CMD sd-read FAIL reason=usage" EOL);
        return;
    }
    st = sd_ensure_init(app);
    if (st != PLAT_OK) {
        say("CMD sd-read FAIL step=init status=%d" EOL, (int)st);
        return;
    }
    st = sdcard_read_block(&app->sd, block, app->block);
    if (st != PLAT_OK) {
        say("CMD sd-read FAIL step=read status=%d" EOL, (int)st);
        return;
    }
    for (i = 0U; i < 16U; i++) {
        first16[2U * i] = hex[app->block[i] >> 4];
        first16[2U * i + 1U] = hex[app->block[i] & 0x0FU];
    }
    first16[sizeof first16 - 1U] = '\0';
    say("CMD sd-read OK block=%lu sig=%02X%02X first16=%s" EOL, (unsigned long)block,
        (unsigned)app->block[510], (unsigned)app->block[511], first16);
}

static void cmd_sd_write(app_t *app, const char *arg)
{
    uint32_t block;
    uint32_t t0;
    uint32_t cycles;
    plat_status_t st;
    unsigned i;

    if (parse_uint(arg, UINT32_MAX, &block) != 0) {
        say("CMD sd-write FAIL reason=usage" EOL);
        return;
    }
    if (block < APP_SD_WRITE_MIN_BLOCK) {
        say("CMD sd-write FAIL reason=protected" EOL);
        return;
    }
    st = sd_ensure_init(app);
    if (st != PLAT_OK) {
        say("CMD sd-write FAIL step=init status=%d" EOL, (int)st);
        return;
    }
    for (i = 0U; i < SDCARD_BLOCK_SIZE; i++) {
        app->block[i] = (uint8_t)(i + block);
    }
    t0 = plat_time_cycles();
    st = sdcard_write_block(&app->sd, block, app->block);
    cycles = plat_time_cycles() - t0;
    if (st != PLAT_OK) {
        say("CMD sd-write FAIL step=write status=%d" EOL, (int)st);
        return;
    }
    say("CMD sd-write OK block=%lu us=%lu" EOL, (unsigned long)block,
        (unsigned long)perf_cycles_to_us(cycles, plat_time_hz()));
}

static void cmd_sd(app_t *app, unsigned argc, char *argv[])
{
    if (argc == 2U && strcmp(argv[1], "init") == 0) {
        cmd_sd_init(app);
    } else if (argc == 3U && strcmp(argv[1], "read") == 0) {
        cmd_sd_read(app, argv[2]);
    } else if (argc == 3U && strcmp(argv[1], "write") == 0) {
        cmd_sd_write(app, argv[2]);
    } else {
        say("CMD sd FAIL reason=usage" EOL);
    }
}

static void cmd_time(void)
{
    uint32_t smallest = UINT32_MAX;
    uint32_t backsteps = 0U;
    unsigned i;

    for (i = 0U; i < TIME_PROBE_PAIRS; i++) {
        uint32_t a = plat_time_cycles();
        uint32_t b = plat_time_cycles();
        uint32_t d = b - a;

        if ((int32_t)d < 0) {
            backsteps++;
        } else if (d < smallest) {
            smallest = d;
        }
    }
    say("CMD time OK hz=%lu read_cycles=%lu backsteps=%lu" EOL, (unsigned long)plat_time_hz(),
        (unsigned long)smallest, (unsigned long)backsteps);
}

static plat_status_t apply_servos(app_t *app)
{
    plat_status_t az = app->pwm_init;
    plat_status_t tilt;

    if (az != PLAT_OK) {
        return az;
    }
    az = servo_set_angle(&app->servo[0], (int16_t)app->tracker.az);
    tilt = servo_set_angle(&app->servo[1], (int16_t)app->tracker.tilt);
    return (az != PLAT_OK) ? az : tilt;
}

static uint8_t result_flags(tracker_result_t r)
{
    uint8_t flags = 0U;

    if (r.az_changed != 0U) {
        flags |= LOGGER_F_AZ;
    }
    if (r.tilt_changed != 0U) {
        flags |= LOGGER_F_TILT;
    }
    if (r.parked_now != 0U) {
        flags |= LOGGER_F_PARK;
    }
    return flags;
}

static void log_note(app_t *app, uint8_t flags)
{
    if (app->log_on == 0U || flags == 0U) {
        return;
    }
    (void)logger_add(&app->logger, app->clock.ms, app->pots.raw, app->tracker.az,
                     app->tracker.tilt, flags);
}

static plat_status_t log_flush_now(app_t *app)
{
    uint32_t t0;
    plat_status_t st;

    if (logger_total(&app->logger) == logger_stored(&app->logger)) {
        return PLAT_OK;
    }
    st = sd_ensure_init(app);
    if (st != PLAT_OK) {
        logger_failed(&app->logger, app->clock.ms);
        return st;
    }
    t0 = plat_time_cycles();
    st = logger_flush(&app->logger, &app->sd, app->clock.ms);
    if (st == PLAT_OK) {
        perf_stat_add(&app->log_flush, plat_time_cycles() - t0);
    }
    return st;
}

static void log_poll(app_t *app)
{
    uint32_t t0;

    if (app->log_on != 0U && logger_due(&app->logger, app->clock.ms) != 0) {
        t0 = plat_time_cycles();
        (void)log_flush_now(app);
        app->meter.busy += plat_time_cycles() - t0;
    }
}

static void track_task(app_t *app)
{
    int at = -1;
    uint32_t t0;
    plat_status_t st = app->adc_init;

    if (st == PLAT_OK) {
        t0 = plat_time_cycles();
        st = pots_read_all(&app->pots, &at);
        if (st == PLAT_OK) {
            perf_stat_add(&app->meter.sense, plat_time_cycles() - t0);
        }
    }
    if (st == PLAT_OK) {
        log_note(app, result_flags(tracker_update(&app->tracker, app->pots.raw)));
        st = apply_servos(app);
    }
    app->track_runs++;
    if (st != PLAT_OK) {
        app->track_errors++;
    }
}

static void track_due(app_t *app, uint32_t now)
{
    app_meter_t *m = &app->meter;
    uint32_t late;
    uint32_t skipped;
    uint32_t cycles;

    plat_probe_set(PLAT_PROBE_T2);
    (void)perf_clock_advance(&app->clock, now, plat_time_hz());
    late = now - app->track_next;
    perf_stat_add(&m->late, late);
    perf_stat_add(&m->period, now - m->track_last);
    m->track_last = now;
    skipped = late / app->track_period;
    app->track_missed += skipped;
    app->track_next += (skipped + 1U) * app->track_period;
    track_task(app);
    cycles = plat_time_cycles() - now;
    plat_probe_clear(PLAT_PROBE_T2);
    perf_stat_add(&m->track, cycles);
    m->busy += cycles;
    log_poll(app);
}

static void track_poll(app_t *app)
{
    uint32_t now;
    uint32_t gap;

    if (app->tracking == 0U) {
        return;
    }
    now = plat_time_cycles();
    gap = now - app->pass_last;
    app->pass_last = now;
    app->passes++;
    if (gap < app->pass_min) {
        app->pass_min = gap;
    }
    if (gap > app->pass_max) {
        app->pass_max = gap;
    }
    if ((int32_t)(now - app->track_next) >= 0) {
        track_due(app, now);
    }
}

static void cmd_track(const app_t *app)
{
    const tracker_t *t = &app->tracker;

    say("CMD track OK az=%u tilt=%u band=%u zero=%u,%u parked=%u stowed=%u dark=%u"
        " runs=%lu missed=%lu errors=%lu" EOL,
        (unsigned)t->az, (unsigned)t->tilt, (unsigned)t->deadband, (unsigned)t->zero_az,
        (unsigned)t->zero_tilt, (unsigned)t->parked, (unsigned)t->stowed, (unsigned)t->dark_count,
        (unsigned long)app->track_runs, (unsigned long)app->track_missed,
        (unsigned long)app->track_errors);
}

static void cmd_deadband(app_t *app, unsigned argc, char *argv[])
{
    uint32_t value;

    if (argc != 2U || parse_uint(argv[1], UINT32_MAX, &value) != 0) {
        say("CMD deadband FAIL reason=usage" EOL);
        return;
    }
    if (tracker_set_deadband(&app->tracker, value) != PLAT_OK) {
        say("CMD deadband FAIL reason=range" EOL);
        return;
    }
    say("CMD deadband OK value=%u" EOL, (unsigned)app->tracker.deadband);
}

static void cmd_zero(app_t *app, unsigned argc, char *argv[])
{
    uint32_t az;
    uint32_t tilt;
    plat_status_t st;

    if (argc != 3U || parse_uint(argv[1], UINT32_MAX, &az) != 0 ||
        parse_uint(argv[2], UINT32_MAX, &tilt) != 0) {
        say("CMD zero FAIL reason=usage" EOL);
        return;
    }
    if (tracker_set_zero(&app->tracker, az, tilt) != PLAT_OK) {
        say("CMD zero FAIL reason=range" EOL);
        return;
    }
    st = apply_servos(app);
    if (st != PLAT_OK) {
        say("CMD zero FAIL status=%d" EOL, (int)st);
        return;
    }
    say("CMD zero OK az=%u tilt=%u" EOL, (unsigned)app->tracker.zero_az,
        (unsigned)app->tracker.zero_tilt);
}

static void cmd_stow(app_t *app)
{
    plat_status_t st;

    (void)tracker_stow(&app->tracker);
    st = apply_servos(app);
    if (st != PLAT_OK) {
        say("CMD stow FAIL status=%d" EOL, (int)st);
        return;
    }
    say("CMD stow OK az=%u tilt=%u" EOL, (unsigned)app->tracker.az, (unsigned)app->tracker.tilt);
}

static void cmd_resume(app_t *app)
{
    tracker_resume(&app->tracker);
    say("CMD resume OK" EOL);
}

static void button_poll(app_t *app)
{
    uint32_t first = 0U;
    uint32_t count = plat_button_take(&first);
    uint32_t hz;
    uint32_t lockout;
    uint32_t now;
    uint32_t cycles;
    tracker_result_t moved;
    plat_status_t st;

    if (count == 0U) {
        return;
    }
    hz = plat_time_hz();
    lockout = (hz / 1000U) * APP_STOW_LOCKOUT_MS;
    app->button_edges += count;
    if (app->button_accepted != 0U && (uint32_t)(first - app->button_last) < lockout) {
        app->button_ignored += count;
        return;
    }
    app->button_accepted++;
    app->button_ignored += count - 1U;
    app->button_last = first;
    plat_probe_set(PLAT_PROBE_EVENT);
    moved = tracker_stow(&app->tracker);
    st = apply_servos(app);
    now = plat_time_cycles();
    plat_probe_clear(PLAT_PROBE_EVENT);
    cycles = now - first;
    if (st != PLAT_OK) {
        say("EVT stow FAIL status=%d" EOL, (int)st);
        return;
    }
    perf_stat_add(&app->stow_latency, cycles);
    say("EVT stow t_us=%lu lat_us=%lu" EOL, (unsigned long)perf_cycles_to_us(first, hz),
        (unsigned long)perf_cycles_to_us(cycles, hz));
    (void)perf_clock_advance(&app->clock, now, hz);
    log_note(app, (uint8_t)(result_flags(moved) | LOGGER_F_STOW));
}

static void cmd_button(const app_t *app)
{
    if (app->button_init != PLAT_OK) {
        say("CMD button FAIL status=%d" EOL, (int)app->button_init);
        return;
    }
    say("CMD button OK edges=%lu accepted=%lu ignored=%lu" EOL, (unsigned long)app->button_edges,
        (unsigned long)app->button_accepted, (unsigned long)app->button_ignored);
}

static void cmd_log_status(const app_t *app)
{
    const logger_t *lg = &app->logger;

    say("CMD log OK on=%u seq=%lu stored=%lu blocks=%lu dropped=%lu errors=%lu flushes=%lu"
        " flush_max_us=%lu" EOL,
        (unsigned)app->log_on, (unsigned long)logger_total(lg), (unsigned long)logger_stored(lg),
        (unsigned long)lg->blocks, (unsigned long)lg->dropped, (unsigned long)lg->errors,
        (unsigned long)app->log_flush.count,
        (unsigned long)perf_cycles_to_us(app->log_flush.max, plat_time_hz()));
}

static void cmd_log_flush(app_t *app)
{
    uint32_t t0 = plat_time_cycles();
    plat_status_t st = log_flush_now(app);
    uint32_t cycles = plat_time_cycles() - t0;

    if (st != PLAT_OK) {
        say("CMD log-flush FAIL status=%d" EOL, (int)st);
        return;
    }
    say("CMD log-flush OK stored=%lu blocks=%lu us=%lu" EOL,
        (unsigned long)logger_stored(&app->logger), (unsigned long)app->logger.blocks,
        (unsigned long)perf_cycles_to_us(cycles, plat_time_hz()));
}

static void cmd_log_get(app_t *app, const char *arg)
{
    logger_record_t r;
    uint32_t n;
    int valid = 0;
    plat_status_t st;

    if (parse_uint(arg, UINT32_MAX, &n) != 0) {
        say("CMD log-get FAIL reason=usage" EOL);
        return;
    }
    if (n >= logger_stored(&app->logger)) {
        say("CMD log-get FAIL reason=range stored=%lu" EOL,
            (unsigned long)logger_stored(&app->logger));
        return;
    }
    st = sd_ensure_init(app);
    if (st != PLAT_OK) {
        say("CMD log-get FAIL step=init status=%d" EOL, (int)st);
        return;
    }
    st = logger_read(&app->sd, n, app->block, &r, &valid);
    if (st != PLAT_OK) {
        say("CMD log-get FAIL step=read status=%d" EOL, (int)st);
        return;
    }
    say("CMD log-get OK n=%lu t_ms=%lu raw=%u,%u,%u,%u az=%u tilt=%u flags=%u ok=%d" EOL,
        (unsigned long)n, (unsigned long)r.t_ms, (unsigned)r.raw[0], (unsigned)r.raw[1],
        (unsigned)r.raw[2], (unsigned)r.raw[3], (unsigned)r.az, (unsigned)r.tilt,
        (unsigned)r.flags, valid);
}

static void cmd_log(app_t *app, unsigned argc, char *argv[])
{
    if (argc == 1U) {
        cmd_log_status(app);
    } else if (argc == 2U && strcmp(argv[1], "on") == 0) {
        app->log_on = 1U;
        say("CMD log OK on=1" EOL);
    } else if (argc == 2U && strcmp(argv[1], "off") == 0) {
        app->log_on = 0U;
        say("CMD log OK on=0" EOL);
    } else if (argc == 2U && strcmp(argv[1], "flush") == 0) {
        cmd_log_flush(app);
    } else if (argc == 3U && strcmp(argv[1], "get") == 0) {
        cmd_log_get(app, argv[2]);
    } else {
        say("CMD log FAIL reason=usage" EOL);
    }
}

static uint64_t window_cycles(const app_t *app)
{
    return (uint64_t)(app->clock.ms - app->meter.start_ms) * (plat_time_hz() / 1000U);
}

static void print_stat(const perf_stat_t *s)
{
    char buf[96];

    (void)perf_stat_format(s, plat_time_hz(), buf, sizeof buf);
    say("%s" EOL, buf);
}

static void print_loop(const app_t *app)
{
    perf_stat_t s;

    perf_stat_init(&s, "loop");
    if (app->passes != 0U) {
        s.count = app->passes;
        s.min = app->pass_min;
        s.max = app->pass_max;
        s.sum = window_cycles(app);
    }
    print_stat(&s);
}

static void print_load(const app_t *app)
{
    uint64_t window = window_cycles(app);
    uint32_t per_ms = plat_time_hz() / 1000U;
    uint32_t busy_ms = 0U;
    uint32_t load_pm = 0U;

    if (per_ms != 0U) {
        busy_ms = (uint32_t)(app->meter.busy / per_ms);
    }
    if (window != 0U) {
        load_pm = (uint32_t)((app->meter.busy * 1000U) / window);
        if (load_pm > 1000U) {
            load_pm = 1000U;
        }
    }
    say("STAT load window_ms=%lu busy_ms=%lu load_pm=%lu" EOL,
        (unsigned long)(app->clock.ms - app->meter.start_ms), (unsigned long)busy_ms,
        (unsigned long)load_pm);
}

static void print_count(const app_t *app)
{
    const app_meter_t *m = &app->meter;

    say("STAT count runs=%lu missed=%lu rx_bytes=%lu rx_errors=%lu passes=%lu" EOL,
        (unsigned long)m->late.count, (unsigned long)(app->track_missed - m->missed0),
        (unsigned long)app->rx_bytes, (unsigned long)m->rx_errors, (unsigned long)app->passes);
}

static void print_mem(void)
{
    plat_mem_info_t mi;
    plat_status_t st = plat_mem_info(&mi);

    if (st != PLAT_OK) {
        say("STAT mem FAIL status=%d" EOL, (int)st);
        return;
    }
    say("STAT mem ram=%lu static=%lu heap_used=%lu heap_res=%lu stack_used=%lu stack_res=%lu"
        " stack_free=%lu" EOL,
        (unsigned long)mi.ram_total, (unsigned long)mi.static_used, (unsigned long)mi.heap_used,
        (unsigned long)mi.heap_reserved, (unsigned long)mi.stack_used,
        (unsigned long)mi.stack_reserved, (unsigned long)mi.stack_free);
}

static void print_stats(const app_t *app)
{
    print_loop(app);
    print_stat(&app->meter.sense);
    print_stat(&app->meter.track);
    print_stat(&app->meter.late);
    print_stat(&app->meter.period);
    print_stat(&app->meter.cmd);
    print_stat(&app->log_flush);
    print_stat(&app->stow_latency);
    print_load(app);
    print_count(app);
    print_mem();
}

static void cmd_stats(app_t *app, unsigned argc, char *argv[])
{
    if (argc > 2U || (argc == 2U && strcmp(argv[1], "reset") != 0)) {
        say("CMD stats FAIL reason=usage" EOL);
        return;
    }
    (void)perf_clock_advance(&app->clock, plat_time_cycles(), plat_time_hz());
    if (argc == 2U) {
        meter_reset(app);
        say("CMD stats-reset OK" EOL);
        return;
    }
    print_stats(app);
    say("CMD stats OK" EOL);
}

static void load_finish(app_t *app)
{
    (void)perf_clock_advance(&app->clock, plat_time_cycles(), plat_time_hz());
    app->load_active = 0U;
    app->load_cycles = 0U;
    print_stats(app);
    say("EVT load done us=%lu ms=%lu" EOL, (unsigned long)app->load_us,
        (unsigned long)(app->clock.ms - app->meter.start_ms));
}

static void cmd_load(app_t *app, unsigned argc, char *argv[])
{
    uint32_t us;
    uint32_t ms;
    uint32_t now;

    if (argc == 1U) {
        say("CMD load OK us=%lu active=%u" EOL, (unsigned long)app->load_us,
            (unsigned)app->load_active);
        return;
    }
    if (argc != 3U || parse_uint(argv[1], UINT32_MAX, &us) != 0 ||
        parse_uint(argv[2], UINT32_MAX, &ms) != 0) {
        say("CMD load FAIL reason=usage" EOL);
        return;
    }
    if (us > APP_LOAD_MAX_US || ms == 0U || ms > APP_LOAD_MAX_MS) {
        say("CMD load FAIL reason=range" EOL);
        return;
    }
    now = plat_time_cycles();
    (void)perf_clock_advance(&app->clock, now, plat_time_hz());
    meter_reset(app);
    app->load_us = us;
    app->load_cycles = us * (plat_time_hz() / 1000000U);
    app->load_end = now + ms * (plat_time_hz() / 1000U);
    app->load_active = 1U;
    say("CMD load OK us=%lu ms=%lu" EOL, (unsigned long)us, (unsigned long)ms);
}

static void load_poll(app_t *app)
{
    uint32_t t0 = plat_time_cycles();
    uint32_t now = t0;

    while ((uint32_t)(now - t0) < app->load_cycles) {
        now = plat_time_cycles();
    }
    app->meter.busy += now - t0;
    if ((int32_t)(now - app->load_end) >= 0) {
        load_finish(app);
    }
}

void app_execute(app_t *app, const char *line)
{
    char work[APP_LINE_MAX + 1U];
    char *argv[MAX_ARGS];
    unsigned argc;

    if (app == NULL || line == NULL || strlen(line) > APP_LINE_MAX) {
        return;
    }
    strcpy(work, line);
    argc = tokenize(work, argv, MAX_ARGS);
    if (argc == 0U) {
        return;
    }
    if (strcmp(argv[0], "help") == 0) {
        cmd_help();
    } else if (strcmp(argv[0], "selftest") == 0) {
        cmd_selftest(app);
    } else if (strcmp(argv[0], "adc") == 0) {
        cmd_adc(app);
    } else if (strcmp(argv[0], "servo") == 0) {
        cmd_servo(app, argc, argv);
    } else if (strcmp(argv[0], "sd") == 0) {
        cmd_sd(app, argc, argv);
    } else if (strcmp(argv[0], "time") == 0) {
        cmd_time();
    } else if (strcmp(argv[0], "track") == 0) {
        cmd_track(app);
    } else if (strcmp(argv[0], "deadband") == 0) {
        cmd_deadband(app, argc, argv);
    } else if (strcmp(argv[0], "zero") == 0) {
        cmd_zero(app, argc, argv);
    } else if (strcmp(argv[0], "stow") == 0) {
        cmd_stow(app);
    } else if (strcmp(argv[0], "resume") == 0) {
        cmd_resume(app);
    } else if (strcmp(argv[0], "button") == 0) {
        cmd_button(app);
    } else if (strcmp(argv[0], "log") == 0) {
        cmd_log(app, argc, argv);
    } else if (strcmp(argv[0], "stats") == 0) {
        cmd_stats(app, argc, argv);
    } else if (strcmp(argv[0], "load") == 0) {
        cmd_load(app, argc, argv);
    } else {
        say("CMD %s FAIL reason=unknown" EOL, argv[0]);
    }
}

static void run_line(app_t *app)
{
    uint32_t t0 = plat_time_cycles();
    uint32_t cycles;

    app_execute(app, app->line);
    cycles = plat_time_cycles() - t0;
    perf_stat_add(&app->meter.cmd, cycles);
    app->meter.busy += cycles;
}

static void console_poll(app_t *app)
{
    uint8_t b;
    plat_status_t st;

    if (app == NULL) {
        return;
    }
    st = plat_uart_read_byte(&b, POLL_TIMEOUT_MS);
    if (st != PLAT_OK) {
        if (st == PLAT_ERR_BUS) {
            app->meter.rx_errors++;
        }
        return;
    }
    app->rx_bytes++;
    if (b == '\r' || b == '\n') {
        if (app->line_overflow) {
            say("CMD ? FAIL reason=too-long" EOL);
        } else if (app->line_len > 0U) {
            app->line[app->line_len] = '\0';
            run_line(app);
        }
        app->line_len = 0U;
        app->line_overflow = 0U;
        return;
    }
    if (app->line_len < APP_LINE_MAX) {
        app->line[app->line_len++] = (char)b;
    } else {
        app->line_overflow = 1U;
    }
}

void app_poll(app_t *app)
{
    if (app == NULL) {
        return;
    }
    button_poll(app);
    track_poll(app);
    if (app->load_active != 0U) {
        load_poll(app);
    }
    console_poll(app);
}
