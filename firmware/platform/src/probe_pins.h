#ifndef PROBE_PINS_H
#define PROBE_PINS_H

#include "main.h"

#define PROBE_PORT        GPIOB
#define PROBE_T2_PIN      GPIO_PIN_4
#define PROBE_BUSY_PIN    GPIO_PIN_6
#define PROBE_EVENT_PIN   GPIO_PIN_7

#define PROBE_BUSY_ENABLED  1

#if PROBE_BUSY_ENABLED
#define PROBE_BUSY_HIGH()  (PROBE_PORT->BSRR = (uint32_t)PROBE_BUSY_PIN)
#define PROBE_BUSY_LOW()   (PROBE_PORT->BSRR = (uint32_t)PROBE_BUSY_PIN << 16U)
#else
#define PROBE_BUSY_HIGH()  ((void)0)
#define PROBE_BUSY_LOW()   ((void)0)
#endif

#endif /* PROBE_PINS_H */