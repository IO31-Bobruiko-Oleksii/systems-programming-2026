/* Host soak: the real app.c on fake time, S2-like load, hours of virtual time.
 *
 * usage: soak [hours]      (default 2; the log of 4096 blocks fills after about 109 minutes)
 *
 * Every 4 s the pot of the TR channel changes and four short commands arrive (the S2 scenario of
 * tools/lr3_gen.py), every 32 s a stow press and `resume`. Time is virtual: one idle pass of the
 * loop is 6 ms (the 5 ms console wait plus a tick), a pass that consumes a byte is 50 us.
 * The cycle counter is 32 bits at 12 MHz and wraps every 357.9 s, so 2 h run through 20 wraps.
 *
 * What is checked: no missed T2 run, every reply, the clock against virtual time across the
 * wraps, every stow press accepted, and the log read back from the blocks the fake card
 * received: checksum, sequence, time stamps, flags, and the behaviour when the log is full.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "logger.h"
#include "fake_uart.h"
#include "fake_pwm.h"
#include "fake_adc.h"
#include "fake_sdcard.h"
#include "fake_time.h"
#include "fake_button.h"
#include "fake_mem.h"
#include "fake_probe.h"

#define HZ          12000000ULL
#define MS          12000ULL
#define IDLE_PASS   (6ULL * MS)
#define BYTE_PASS   600ULL
#define TICK_MS     4000ULL
#define REPORT_S    600ULL

static app_t app;
static uint64_t vt;                         /* virtual time in cycles */
static uint8_t card[LOGGER_BLOCKS][SDCARD_BLOCK_SIZE];
static uint8_t seen[LOGGER_BLOCKS];
static unsigned writes_seen;
static unsigned writes_outside;
static uint32_t max_index;
static unsigned reply_errors;
static unsigned presses;
static uint64_t full_at_ms;
static unsigned failures;

static void fail(const char *what)
{
    printf("FAIL: %s\n", what);
    failures++;
}

static uint64_t now_ms(void)
{
    return vt / MS;
}

static void advance(uint64_t cycles)
{
    vt += cycles;
    fake_time_set((uint32_t)vt);
}

static void capture_card(void)
{
    while (writes_seen < fake_sd_write_count()) {
        uint32_t idx = fake_sd_stored_index();

        writes_seen++;
        if (idx < LOGGER_FIRST_BLOCK || idx >= LOGGER_FIRST_BLOCK + LOGGER_BLOCKS) {
            writes_outside++;
            continue;
        }
        memcpy(card[idx - LOGGER_FIRST_BLOCK], fake_sd_stored_block(), SDCARD_BLOCK_SIZE);
        seen[idx - LOGGER_FIRST_BLOCK] = 1U;
        if (idx > max_index) {
            max_index = idx;
        }
    }
    if (full_at_ms == 0U && app.logger.blocks >= LOGGER_BLOCKS) {
        full_at_ms = now_ms();
    }
}

static void poll_once(uint64_t cycles)
{
    fake_uart_clear_output();
    app_poll(&app);
    capture_card();
    advance(cycles);
}

static void idle_ms(uint64_t ms)
{
    uint64_t end = vt + ms * MS;

    while (vt < end) {
        poll_once(IDLE_PASS);
    }
}

static char reply[FAKE_UART_OUT_CAPACITY];

static void send(const char *cmd, const char *expect)
{
    char line[64];

    snprintf(line, sizeof line, "%s\n", cmd);
    if (fake_uart_in_pending() == 0U) {
        fake_uart_reset();          /* the fake input buffer is only 1024 bytes in total: start again */
    }
    fake_uart_clear_output();
    fake_uart_feed(line);
    while (fake_uart_in_pending() > 0U) {
        app_poll(&app);
        advance(BYTE_PASS);
    }
    snprintf(reply, sizeof reply, "%s", fake_uart_output());
    capture_card();
    if (strstr(reply, expect) == NULL) {
        reply_errors++;
        if (reply_errors <= 3U) {
            printf("reply to `%s` does not contain `%s`: %.80s\n", cmd, expect, reply);
        }
    }
    idle_ms(20);
}

static unsigned long field(const char *text, const char *key)
{
    const char *p = strstr(text, key);

    return (p == NULL) ? 0xFFFFFFFFUL : strtoul(p + strlen(key), NULL, 10);
}

static void report(void)
{
    int64_t skew;

    send("stats", "CMD stats OK");
    skew = (int64_t)(uint32_t)app.clock.ms - (int64_t)(uint32_t)now_ms();
    printf("%6llu s  wraps %2llu  T2 runs %6lu missed %lu  rx errors %lu  clock skew %lld ms  "
           "log seq %lu blocks %lu dropped %lu\n",
           (unsigned long long)(now_ms() / 1000U), (unsigned long long)(vt >> 32),
           field(reply, "STAT count runs="), field(reply, " missed="), field(reply, "rx_errors="),
           (long long)skew, (unsigned long)logger_total(&app.logger),
           (unsigned long)app.logger.blocks, (unsigned long)app.logger.dropped);
    if (field(reply, " missed=") != 0U) {
        fail("T2 runs were missed");
    }
    if (skew < -60 || skew > 60) {
        fail("the application clock does not follow virtual time");
    }
}

static void verify_log(void)
{
    uint32_t total = logger_total(&app.logger);
    uint32_t blocks = (total + LOGGER_RECORDS_PER_BLOCK - 1U) / LOGGER_RECORDS_PER_BLOCK;
    uint32_t last_t = 0U;
    uint32_t bad_sum = 0U;
    uint32_t bad_seq = 0U;
    uint32_t bad_time = 0U;
    uint32_t bad_flags = 0U;
    uint32_t n;
    uint32_t counts[4] = { 0U, 0U, 0U, 0U };

    for (n = 0U; n < total; n++) {
        logger_record_t r;
        uint32_t b = n / LOGGER_RECORDS_PER_BLOCK;

        if (!seen[b]) {
            fail("a block of the log was never written");
            return;
        }
        if (!logger_decode(card[b] + (n % LOGGER_RECORDS_PER_BLOCK) * LOGGER_RECORD_SIZE, &r)) {
            bad_sum++;
            continue;
        }
        if (r.seq != n) {
            bad_seq++;
        }
        if (n != 0U && (int32_t)(r.t_ms - last_t) < 0) {
            bad_time++;
        }
        last_t = r.t_ms;
        if (r.flags == 0U || (r.flags & ~0x0FU) != 0U || r.az > 180U || r.tilt > 180U) {
            bad_flags++;
        }
        counts[0] += (r.flags & LOGGER_F_AZ) != 0U;
        counts[1] += (r.flags & LOGGER_F_TILT) != 0U;
        counts[2] += (r.flags & LOGGER_F_PARK) != 0U;
        counts[3] += (r.flags & LOGGER_F_STOW) != 0U;
    }
    printf("\nlog read back from the card: %lu records in %lu blocks\n", (unsigned long)total,
           (unsigned long)blocks);
    printf("  bad checksum %lu, wrong sequence number %lu, time going back %lu, bad flags/angles %lu\n",
           (unsigned long)bad_sum, (unsigned long)bad_seq, (unsigned long)bad_time,
           (unsigned long)bad_flags);
    printf("  records with az moved %lu, tilt moved %lu, parked %lu, stow %lu\n",
           (unsigned long)counts[0], (unsigned long)counts[1], (unsigned long)counts[2],
           (unsigned long)counts[3]);
    if (bad_sum != 0U || bad_seq != 0U || bad_time != 0U || bad_flags != 0U) {
        fail("the log is damaged");
    }
    if (total != logger_stored(&app.logger)) {
        fail("records are held back in the buffer");
    }
}

int main(int argc, char **argv)
{
    double hours = (argc > 1) ? atof(argv[1]) : 2.0;
    uint64_t end_ms = (uint64_t)(hours * 3600.0 * 1000.0);
    static const char *const cmds[] = { "track", "adc", "help", "button", "log" };
    static const char *const expect[] = { "CMD track OK", "CMD adc OK", "CMD help OK",
                                          "CMD button OK", "CMD log OK" };
    static const double pot[] = { 0.9, 0.5, 0.1, 0.5 };
    unsigned t;
    unsigned i;
    unsigned next_report = 0U;
    unsigned long runs_expected;

    fake_uart_reset(); fake_pwm_reset(); fake_adc_reset(); fake_sd_reset(); fake_time_reset();
    fake_button_reset(); fake_mem_reset(); fake_probe_reset();
    for (i = 0U; i < 4U; i++) {
        fake_adc_set_raw((uint8_t)i, 2048U);
    }
    app_init(&app);
    app_start(&app);
    send("stow", "CMD stow OK");
    send("resume", "CMD resume OK");
    send("log on", "CMD log OK");
    send("stats reset", "CMD stats-reset OK");
    printf("soak of %.2f h of virtual time: S2 load, idle pass 6 ms, T2 every 50 ms\n", hours);

    for (t = 0U; now_ms() < end_ms; t++) {
        uint64_t tick_start = vt;

        fake_adc_set_raw(1U, (uint16_t)(pot[t % 4U] * 4095.0 + 0.5));
        for (i = 0U; i < 4U; i++) {
            send(cmds[(t * 4U + i) % 5U], expect[(t * 4U + i) % 5U]);
        }
        if (t % 8U == 3U) {
            fake_button_press((uint32_t)(vt - 3U * MS));
            presses++;
            idle_ms(250);
            send("resume", "CMD resume OK");
        }
        while (vt < tick_start + TICK_MS * MS) {
            poll_once(IDLE_PASS);
        }
        if (now_ms() / 1000U >= (uint64_t)next_report * REPORT_S) {
            report();
            next_report++;
        }
    }
    report();
    send("log flush", "CMD log-flush OK");
    send("button", "CMD button OK");
    printf("\nbutton: %u presses, edges %lu accepted %lu ignored %lu\n", presses,
           (unsigned long)app.button_edges, (unsigned long)app.button_accepted,
           (unsigned long)app.button_ignored);
    if (app.button_accepted != presses || app.button_edges != presses) {
        fail("a stow press was lost or counted twice");
    }
    runs_expected = (unsigned long)(now_ms() / 50U);
    printf("T2 runs %lu, expected about %lu, missed %lu, errors %lu\n", (unsigned long)app.track_runs,
           runs_expected, (unsigned long)app.track_missed, (unsigned long)app.track_errors);
    if (app.track_runs + 2U < runs_expected || app.track_runs > runs_expected + 2U) {
        fail("the number of T2 runs does not match the virtual time");
    }
    if (app.track_missed != 0U || app.track_errors != 0U) {
        fail("T2 missed runs or had errors");
    }
    printf("replies that did not match: %u\n", reply_errors);
    if (reply_errors != 0U) {
        fail("a command got a wrong reply");
    }
    verify_log();
    printf("\ncard: %u block writes, %u outside the log area, highest block %lu (the area ends at %lu)\n",
           writes_seen, writes_outside, (unsigned long)max_index,
           (unsigned long)(LOGGER_FIRST_BLOCK + LOGGER_BLOCKS - 1U));
    if (writes_outside != 0U || (max_index != 0U && max_index > LOGGER_FIRST_BLOCK + LOGGER_BLOCKS - 1U)) {
        fail("the log wrote outside its area");
    }
    if (full_at_ms != 0U) {
        printf("the log was full after %llu s (%.1f min); records dropped since: %lu, flush errors %lu\n",
               (unsigned long long)(full_at_ms / 1000U), (double)full_at_ms / 60000.0,
               (unsigned long)app.logger.dropped, (unsigned long)app.logger.errors);
        if (app.logger.dropped == 0U) {
            fail("nothing was dropped although the log is full");
        }
    } else {
        printf("the log did not fill up in this run\n");
    }
    printf("\n%s\n", failures == 0U ? "SOAK PASSED" : "SOAK FAILED");
    return failures == 0U ? 0 : 1;
}
