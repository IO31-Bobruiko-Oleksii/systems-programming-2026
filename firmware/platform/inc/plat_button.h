#ifndef PLAT_BUTTON_H
#define PLAT_BUTTON_H

#include <stdint.h>
#include "plat_status.h"

plat_status_t plat_button_init(void);

uint32_t plat_button_take(uint32_t *first_edge_cycles);

void plat_button_set_notify(void (*cb)(void));

#endif /* PLAT_BUTTON_H */
