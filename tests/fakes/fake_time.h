#ifndef FAKE_TIME_H
#define FAKE_TIME_H

#include <stdint.h>

void     fake_time_reset(void);
void     fake_time_set(uint32_t cycles);
void     fake_time_set_step(uint32_t cycles_per_call);
void     fake_time_set_hz(uint32_t hz);
unsigned fake_time_calls(void);

#endif /* FAKE_TIME_H */