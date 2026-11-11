#ifndef FAKE_MEM_H
#define FAKE_MEM_H

#include "plat_mem.h"
#include "plat_status.h"

void fake_mem_reset(void);
void fake_mem_set(const plat_mem_info_t *info);
void fake_mem_fail(plat_status_t status);

#endif /* FAKE_MEM_H */