#ifndef PERF_H
#define PERF_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    uint32_t    count;
    uint32_t    min;
    uint32_t    max;
    uint64_t    sum;
} perf_stat_t;

void perf_stat_init(perf_stat_t *s, const char *name);

void perf_stat_add(perf_stat_t *s, uint32_t cycles);

uint32_t perf_cycles_to_us(uint32_t cycles, uint32_t hz);

typedef struct {
    uint32_t last;
    uint32_t rem;
    uint32_t ms;
} perf_clock_t;

void perf_clock_init(perf_clock_t *c, uint32_t now);

uint32_t perf_clock_advance(perf_clock_t *c, uint32_t now, uint32_t hz);

int perf_stat_format(const perf_stat_t *s, uint32_t hz, char *buf, size_t size);

#endif /* PERF_H */
