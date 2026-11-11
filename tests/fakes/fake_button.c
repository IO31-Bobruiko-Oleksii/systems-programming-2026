#include <stddef.h>
#include "fake_button.h"
#include "plat_button.h"

static struct {
    plat_status_t init_status;
    uint32_t count;
    uint32_t first;
    unsigned takes;
} m;

void fake_button_reset(void)
{
    m.init_status = PLAT_OK;
    m.count = 0U;
    m.first = 0U;
    m.takes = 0U;
}

void fake_button_fail_init(plat_status_t status)
{
    m.init_status = status;
}

void fake_button_press(uint32_t edge_cycles)
{
    if (m.count == 0U) {
        m.first = edge_cycles;
    }
    m.count++;
}

unsigned fake_button_takes(void)
{
    return m.takes;
}

plat_status_t plat_button_init(void)
{
    return m.init_status;
}

uint32_t plat_button_take(uint32_t *first_edge_cycles)
{
    uint32_t count = m.count;

    m.takes++;
    if (count != 0U && first_edge_cycles != NULL) {
        *first_edge_cycles = m.first;
    }
    m.count = 0U;
    return count;
}