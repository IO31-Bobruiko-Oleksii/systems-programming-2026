#ifndef PLAT_PROBE_H
#define PLAT_PROBE_H

typedef enum {
    PLAT_PROBE_T2 = 0,
    PLAT_PROBE_EVENT,
    PLAT_PROBE_COUNT
} plat_probe_t;

void plat_probe_init(void);

void plat_probe_set(plat_probe_t probe);

void plat_probe_clear(plat_probe_t probe);

#endif /* PLAT_PROBE_H */