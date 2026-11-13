#include <stddef.h>
#include "plat_ring.h"

void plat_ring_init(plat_ring_t *r)
{
    r->head = 0U;
    r->tail = 0U;
    r->dropped = 0U;
    r->high = 0U;
}

int plat_ring_put(plat_ring_t *r, uint8_t byte)
{
    uint16_t used = (uint16_t)(r->head - r->tail);

    if (used >= PLAT_RING_SIZE) {
        r->dropped++;
        return -1;
    }
    r->data[r->head & (PLAT_RING_SIZE - 1U)] = byte;
    r->head = (uint16_t)(r->head + 1U);
    used++;
    if (used > r->high) {
        r->high = used;
    }
    return 0;
}

int plat_ring_get(plat_ring_t *r, uint8_t *byte)
{
    if (r->head == r->tail) {
        return -1;
    }
    *byte = r->data[r->tail & (PLAT_RING_SIZE - 1U)];
    r->tail = (uint16_t)(r->tail + 1U);
    return 0;
}

uint16_t plat_ring_count(const plat_ring_t *r)
{
    return (uint16_t)(r->head - r->tail);
}

void plat_ring_reset_stats(plat_ring_t *r)
{
    r->dropped = 0U;
    r->high = plat_ring_count(r);
}
