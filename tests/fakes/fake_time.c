#include "fake_time.h"
#include "plat_time.h"

static struct {
    uint32_t now;
    uint32_t step;
    uint32_t hz;
    unsigned calls;
} m;

void fake_time_reset(void)
{
    m.now = 0U;
    m.step = 0U;
    m.hz = 12000000U;
    m.calls = 0U;
}

void fake_time_set(uint32_t cycles)            { m.now = cycles; }
void fake_time_set_step(uint32_t cycles_per_call) { m.step = cycles_per_call; }
void fake_time_set_hz(uint32_t hz)             { m.hz = hz; }
unsigned fake_time_calls(void)                 { return m.calls; }

uint32_t plat_time_cycles(void)
{
    uint32_t value = m.now;

    m.now += m.step;
    m.calls++;
    return value;
}

uint32_t plat_time_hz(void)
{
    return m.hz;
}