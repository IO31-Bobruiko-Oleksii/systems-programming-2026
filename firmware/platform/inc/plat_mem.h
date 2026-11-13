#ifndef PLAT_MEM_H
#define PLAT_MEM_H

#include <stdint.h>
#include "plat_status.h"

typedef struct {
    uint32_t ram_total;
    uint32_t static_used;
    uint32_t heap_reserved;
    uint32_t heap_used;
    uint32_t stack_reserved;
    uint32_t stack_used;
    uint32_t stack_free;
} plat_mem_info_t;

void plat_mem_init(void);

plat_status_t plat_mem_info(plat_mem_info_t *info);

#endif /* PLAT_MEM_H */