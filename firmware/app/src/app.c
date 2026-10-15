#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "plat_adc.h"
#include "plat_pwm.h"
#include "plat_spi.h"
#include "plat_uart.h"

#define EOL                "\r\n"
#define POLL_TIMEOUT_MS    5U
#define MAX_ARGS           4U
#define SELFTEST_VERSION   1U

static void say(const char *fmt, ...)
{
    char buf[128];
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

void app_init(app_t *app)
{
    if (app == NULL) {
        return;
    }
    memset(app, 0, sizeof *app);
    app->adc_init = plat_adc_init();
    app->spi_init = plat_spi_init();
    app->pwm_init = plat_pwm_init();
    if (app->pwm_init == PLAT_OK) {
        app->pwm_init = servo_init(&app->servo[0], PLAT_PWM_CH_SERVO1);
    }
    if (app->pwm_init == PLAT_OK) {
        app->pwm_init = servo_init(&app->servo[1], PLAT_PWM_CH_SERVO2);
    }
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
    say("CMD help OK commands=help,selftest,adc,servo,sd" EOL);
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

static void cmd_sd(app_t *app, unsigned argc, char *argv[])
{
    if (argc == 2U && strcmp(argv[1], "init") == 0) {
        cmd_sd_init(app);
    } else if (argc == 3U && strcmp(argv[1], "read") == 0) {
        cmd_sd_read(app, argv[2]);
    } else {
        say("CMD sd FAIL reason=usage" EOL);
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
    } else {
        say("CMD %s FAIL reason=unknown" EOL, argv[0]);
    }
}

void app_poll(app_t *app)
{
    uint8_t b;

    if (app == NULL || plat_uart_read_byte(&b, POLL_TIMEOUT_MS) != PLAT_OK) {
        return;
    }
    if (b == '\r' || b == '\n') {
        if (app->line_overflow) {
            say("CMD ? FAIL reason=too-long" EOL);
        } else if (app->line_len > 0U) {
            app->line[app->line_len] = '\0';
            app_execute(app, app->line);
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
