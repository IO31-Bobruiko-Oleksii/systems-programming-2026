#ifndef PLAT_TIME_H
#define PLAT_TIME_H

#include <stdint.h>

uint32_t plat_time_cycles(void);

uint32_t plat_time_hz(void);

static inline uint32_t plat_time_compose(uint32_t ticks, uint32_t period, uint32_t val,
                                         int tick_pending)
{
    uint32_t t = ticks + (tick_pending ? 1U : 0U);

    return t * period + (period - 1U - val);
}

#endif /* PLAT_TIME_H */