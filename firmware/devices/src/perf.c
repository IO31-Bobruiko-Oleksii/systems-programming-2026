#include <stdio.h>
#include <string.h>
#include "perf.h"

void perf_stat_init(perf_stat_t *s, const char *name)
{
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof *s);
    s->name = name;
}

void perf_stat_add(perf_stat_t *s, uint32_t cycles)
{
    if (s == NULL) {
        return;
    }
    if (s->count == 0U || cycles < s->min) {
        s->min = cycles;
    }
    if (cycles > s->max) {
        s->max = cycles;
    }
    s->sum += cycles;
    s->count++;
}

uint32_t perf_cycles_to_us(uint32_t cycles, uint32_t hz)
{
    if (hz == 0U) {
        return 0U;
    }
    return (uint32_t)(((uint64_t)cycles * 1000000U + hz / 2U) / hz);
}

int perf_stat_format(const perf_stat_t *s, uint32_t hz, char *buf, size_t size)
{
    uint32_t avg = 0U;

    if (s == NULL || buf == NULL || size == 0U) {
        return -1;
    }
    if (s->count != 0U) {
        avg = (uint32_t)(s->sum / s->count);
    }
    return snprintf(buf, size, "STAT name=%s n=%lu min_us=%lu avg_us=%lu max_us=%lu",
                    (s->name != NULL) ? s->name : "?", (unsigned long)s->count,
                    (unsigned long)perf_cycles_to_us(s->min, hz),
                    (unsigned long)perf_cycles_to_us(avg, hz),
                    (unsigned long)perf_cycles_to_us(s->max, hz));
}