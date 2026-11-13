#ifndef FAKE_BUTTON_H
#define FAKE_BUTTON_H

#include <stdint.h>
#include "plat_status.h"

void fake_button_reset(void);
void fake_button_fail_init(plat_status_t status);
void fake_button_press(uint32_t edge_cycles);
unsigned fake_button_takes(void);

#endif /* FAKE_BUTTON_H */