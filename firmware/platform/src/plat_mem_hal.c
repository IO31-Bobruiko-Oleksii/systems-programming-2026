#include <stddef.h>
#include "stm32c0xx_hal.h"
#include "plat_mem.h"

#define PAINT_WORD  0xA5A5A5A5U
#define PAINT_GAP   16U

extern uint8_t _sdata;
extern uint8_t _end;
extern uint8_t _estack;
extern uint8_t _Min_Heap_Size;
extern uint8_t _Min_Stack_Size;

void *_sbrk(ptrdiff_t incr);

void plat_mem_init(void)
{
    uint32_t primask = __get_PRIMASK();
    uint32_t *p = (uint32_t *)(((uintptr_t)&_end + 3U) & ~(uintptr_t)3U);
    uint32_t *limit;

    __disable_irq();
    limit = (uint32_t *)(((uintptr_t)__get_MSP() - PAINT_GAP) & ~(uintptr_t)3U);
    while (p < limit) {
        *p++ = PAINT_WORD;
    }
    __set_PRIMASK(primask);
}

plat_status_t plat_mem_info(plat_mem_info_t *info)
{
    uintptr_t ram_start = (uintptr_t)&_sdata;
    uintptr_t ram_end = (uintptr_t)&_estack;
    uintptr_t end = (uintptr_t)&_end;
    uintptr_t brk = (uintptr_t)_sbrk(0);
    const uint32_t *p = (const uint32_t *)((brk + 3U) & ~(uintptr_t)3U);
    uintptr_t floor = (uintptr_t)p;

    if (info == NULL) {
        return PLAT_ERR_ARG;
    }
    while ((uintptr_t)p < ram_end && *p == PAINT_WORD) {
        p++;
    }
    info->ram_total = (uint32_t)(ram_end - ram_start);
    info->static_used = (uint32_t)(end - ram_start);
    info->heap_reserved = (uint32_t)(uintptr_t)&_Min_Heap_Size;
    info->heap_used = (uint32_t)(brk - end);
    info->stack_reserved = (uint32_t)(uintptr_t)&_Min_Stack_Size;
    info->stack_used = (uint32_t)(ram_end - (uintptr_t)p);
    info->stack_free = (uint32_t)((uintptr_t)p - floor);
    return PLAT_OK;
}