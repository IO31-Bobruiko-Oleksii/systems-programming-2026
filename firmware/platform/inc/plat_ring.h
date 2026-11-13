#ifndef PLAT_RING_H
#define PLAT_RING_H

#include <stdint.h>

#define PLAT_RING_SIZE  256U

typedef struct {
    uint8_t           data[PLAT_RING_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint32_t dropped;
    volatile uint16_t high;
} plat_ring_t;

void plat_ring_init(plat_ring_t *r);

int plat_ring_put(plat_ring_t *r, uint8_t byte);

int plat_ring_get(plat_ring_t *r, uint8_t *byte);

uint16_t plat_ring_count(const plat_ring_t *r);

void plat_ring_reset_stats(plat_ring_t *r);

#endif /* PLAT_RING_H */
