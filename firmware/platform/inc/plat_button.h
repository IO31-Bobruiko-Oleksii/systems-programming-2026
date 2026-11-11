#ifndef PLAT_BUTTON_H
#define PLAT_BUTTON_H

#include <stdint.h>
#include "plat_status.h"

plat_status_t plat_button_init(void);

uint32_t plat_button_take(uint32_t *first_edge_cycles);

#endif /* PLAT_BUTTON_H */