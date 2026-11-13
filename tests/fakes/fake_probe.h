#ifndef FAKE_PROBE_H
#define FAKE_PROBE_H

#include <stddef.h>
#include "plat_probe.h"

#define FAKE_PROBE_LOG 64U

typedef struct {
    plat_probe_t probe;
    unsigned     level;
    size_t       adc_calls;
    size_t       pwm_calls;
    unsigned     sd_writes;
} fake_probe_event_t;

void     fake_probe_reset(void);
unsigned fake_probe_inits(void);
unsigned fake_probe_level(plat_probe_t probe);
unsigned fake_probe_sets(plat_probe_t probe);
unsigned fake_probe_clears(plat_probe_t probe);
size_t   fake_probe_events(void);
const fake_probe_event_t *fake_probe_event(size_t index);

#endif /* FAKE_PROBE_H */