#include <stddef.h>
#include "fake_mem.h"

static plat_mem_info_t shown;
static plat_status_t failure;

void fake_mem_reset(void)
{
    shown.ram_total = 12288U;
    shown.static_used = 3668U;
    shown.heap_reserved = 512U;
    shown.heap_used = 0U;
    shown.stack_reserved = 1024U;
    shown.stack_used = 312U;
    shown.stack_free = 8316U;
    failure = PLAT_OK;
}

void fake_mem_set(const plat_mem_info_t *info)
{
    shown = *info;
}

void fake_mem_fail(plat_status_t status)
{
    failure = status;
}

void plat_mem_init(void)
{
}

plat_status_t plat_mem_info(plat_mem_info_t *info)
{
    if (info == NULL) {
        return PLAT_ERR_ARG;
    }
    if (failure != PLAT_OK) {
        return failure;
    }
    *info = shown;
    return PLAT_OK;
}