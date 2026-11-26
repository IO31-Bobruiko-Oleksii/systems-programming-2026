#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>

extern uint32_t plat_time_cycles(void);
extern void rtos_assert(unsigned line);

#ifndef RTOS_TICK_HZ
#define RTOS_TICK_HZ     1000
#endif
#ifndef RTOS_HEAP_BYTES
#define RTOS_HEAP_BYTES  4096
#endif

#define configENABLE_MPU                        0
#define configENABLE_FPU                        0
#define configENABLE_TRUSTZONE                  0
#define configRUN_FREERTOS_SECURE_ONLY          1

#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_16_BIT_TICKS                  0
#define configCPU_CLOCK_HZ                      (12000000UL)
#define configTICK_RATE_HZ                      ((TickType_t)RTOS_TICK_HZ)
#define configMAX_PRIORITIES                    6
#define configMINIMAL_STACK_SIZE                ((uint16_t)64)
#define configMAX_TASK_NAME_LEN                 8

#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           0
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configQUEUE_REGISTRY_SIZE               0
#define configUSE_TIMERS                        0

#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ((size_t)RTOS_HEAP_BYTES)
#define configUSE_NEWLIB_REENTRANT              0

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            1
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configGENERATE_RUN_TIME_STATS           1
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() ((void)0)
#define portGET_RUN_TIME_COUNTER_VALUE()        plat_time_cycles()
#define configASSERT(x)                         do { if (!(x)) { rtos_assert(__LINE__); } } while (0)

#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelete                     0
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_xTaskGetSchedulerState          1

#define SysTick_Handler                         xPortSysTickHandler

#endif /* FREERTOS_CONFIG_H */