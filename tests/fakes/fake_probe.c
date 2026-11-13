#include "fake_adc.h"
#include "fake_probe.h"
#include "fake_pwm.h"
#include "fake_sdcard.h"

static unsigned           init_count;
static unsigned           level[PLAT_PROBE_COUNT];
static unsigned           set_count[PLAT_PROBE_COUNT];
static unsigned           clear_count[PLAT_PROBE_COUNT];
static fake_probe_event_t log_event[FAKE_PROBE_LOG];
static size_t             log_len;

static void note(plat_probe_t probe, unsigned value)
{
    if (log_len < FAKE_PROBE_LOG) {
        log_event[log_len].probe = probe;
        log_event[log_len].level = value;
        log_event[log_len].adc_calls = fake_adc_calls();
        log_event[log_len].pwm_calls = fake_pwm_calls();
        log_event[log_len].sd_writes = fake_sd_write_count();
    }
    log_len++;
}

void fake_probe_reset(void)
{
    unsigned i;

    init_count = 0U;
    for (i = 0U; i < (unsigned)PLAT_PROBE_COUNT; i++) {
        level[i] = 0U;
        set_count[i] = 0U;
        clear_count[i] = 0U;
    }
    log_len = 0U;
}

unsigned fake_probe_inits(void)
{
    return init_count;
}

unsigned fake_probe_level(plat_probe_t probe)
{
    return level[probe];
}

unsigned fake_probe_sets(plat_probe_t probe)
{
    return set_count[probe];
}

unsigned fake_probe_clears(plat_probe_t probe)
{
    return clear_count[probe];
}

size_t fake_probe_events(void)
{
    return log_len;
}

const fake_probe_event_t *fake_probe_event(size_t index)
{
    return &log_event[index];
}

void plat_probe_init(void)
{
    init_count++;
}

void plat_probe_set(plat_probe_t probe)
{
    if ((unsigned)probe >= (unsigned)PLAT_PROBE_COUNT) {
        return;
    }
    level[probe] = 1U;
    set_count[probe]++;
    note(probe, 1U);
}

void plat_probe_clear(plat_probe_t probe)
{
    if ((unsigned)probe >= (unsigned)PLAT_PROBE_COUNT) {
        return;
    }
    level[probe] = 0U;
    clear_count[probe]++;
    note(probe, 0U);
}