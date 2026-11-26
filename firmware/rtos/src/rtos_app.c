#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "fmt.h"
#include "plat_button.h"
#include "plat_probe.h"
#include "plat_time.h"
#include "plat_uart.h"
#include "rtos_app.h"

void xPortSysTickHandler(void);

#define GATE_BLINK_MS      10U
#define GATE_HOG_MS        30U
#define GATE_REPORT_MS     400U
#define GATE_BLINK_WORDS   96U
#define GATE_EVENT_WORDS   96U
#define GATE_HOG_WORDS     192U

static TaskHandle_t h_event;
static SemaphoreHandle_t uart_mutex;

static volatile uint32_t blink_runs;
static volatile uint32_t blink_gap_min;
static volatile uint32_t blink_gap_max;
static volatile uint32_t evt_n;
static volatile uint32_t evt_max;

static void fatal(const char *text)
{
    portDISABLE_INTERRUPTS();
    (void)plat_uart_write((const uint8_t *)text, strlen(text));
    for (;;) {
    }
}

void rtos_assert(unsigned line)
{
    char buf[40];
    int n = fmt_snprintf(buf, sizeof buf, "FATAL assert line=%u\r\n", line);

    portDISABLE_INTERRUPTS();
    if (n > 0) {
        (void)plat_uart_write((const uint8_t *)buf, (size_t)n);
    }
    for (;;) {
    }
}

void vApplicationMallocFailedHook(void)
{
    static const char msg[] = "FATAL malloc failed\r\n";

    portDISABLE_INTERRUPTS();
    (void)plat_uart_write((const uint8_t *)msg, sizeof msg - 1U);
    for (;;) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    char buf[48];
    int n = fmt_snprintf(buf, sizeof buf, "FATAL stack overflow in %s\r\n", name);

    (void)task;
    portDISABLE_INTERRUPTS();
    if (n > 0) {
        (void)plat_uart_write((const uint8_t *)buf, (size_t)n);
    }
    for (;;) {
    }
}

void rtos_systick(void)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}

static void say(const char *fmt, ...)
{
    char buf[120];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = fmt_vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    if (n >= (int)sizeof buf) {
        n = (int)sizeof buf - 1;
    }
    if (xSemaphoreTake(uart_mutex, portMAX_DELAY) == pdTRUE) {
        (void)plat_uart_write((const uint8_t *)buf, (size_t)n);
        (void)xSemaphoreGive(uart_mutex);
    }
}

static void button_notify(void)
{
    BaseType_t woken = pdFALSE;

    vTaskNotifyGiveFromISR(h_event, &woken);
    portYIELD_FROM_ISR(woken);
}

static void gate_blink(void *arg)
{
    TickType_t last = xTaskGetTickCount();
    uint32_t prev = 0U;
    uint32_t level = 0U;

    (void)arg;
    for (;;) {
        uint32_t now;

        vTaskDelayUntil(&last, pdMS_TO_TICKS(GATE_BLINK_MS));
        now = plat_time_cycles();
        level ^= 1U;
        if (level != 0U) {
            plat_probe_set(PLAT_PROBE_T2);
        } else {
            plat_probe_clear(PLAT_PROBE_T2);
        }
        if (blink_runs != 0U) {
            uint32_t gap = now - prev;

            if (gap < blink_gap_min) {
                blink_gap_min = gap;
            }
            if (gap > blink_gap_max) {
                blink_gap_max = gap;
            }
        }
        prev = now;
        blink_runs++;
    }
}

static void gate_event(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t first = 0U;
        uint32_t cycles;

        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (plat_button_take(&first) == 0U) {
            continue;
        }
        plat_probe_set(PLAT_PROBE_EVENT);
        cycles = plat_time_cycles() - first;
        plat_probe_clear(PLAT_PROBE_EVENT);
        evt_n++;
        if (cycles > evt_max) {
            evt_max = cycles;
        }
    }
}

static void gate_hog(void *arg)
{
    uint32_t pass = 0U;
    uint32_t hz = plat_time_hz();
    uint32_t spin = (hz / 1000U) * GATE_HOG_MS;

    (void)arg;
    for (;;) {
        uint32_t t0 = plat_time_cycles();
        uint32_t gmin;
        uint32_t gmax;

        while ((uint32_t)(plat_time_cycles() - t0) < spin) {
        }
        pass++;
        taskENTER_CRITICAL();
        gmin = blink_gap_min;
        gmax = blink_gap_max;
        blink_gap_min = UINT32_MAX;
        blink_gap_max = 0U;
        taskEXIT_CRITICAL();
        say("GATE pass=%lu blink_runs=%lu gap_min_us=%lu gap_max_us=%lu evt_n=%lu evt_max_us=%lu"
            "\r\n", (unsigned long)pass, (unsigned long)blink_runs,
            (unsigned long)((gmin == UINT32_MAX) ? 0U : gmin / (hz / 1000000U)),
            (unsigned long)(gmax / (hz / 1000000U)), (unsigned long)evt_n,
            (unsigned long)(evt_max / (hz / 1000000U)));
        vTaskDelay(pdMS_TO_TICKS(GATE_REPORT_MS));
    }
}

void rtos_start(app_t *app)
{
    char buf[64];
    BaseType_t ok;
    int n;

    (void)app;
    configASSERT(plat_time_hz() == configCPU_CLOCK_HZ);
    blink_gap_min = UINT32_MAX;
    uart_mutex = xSemaphoreCreateMutex();
    configASSERT(uart_mutex != NULL);
    ok = xTaskCreate(gate_event, "event", GATE_EVENT_WORDS, NULL, 3U, &h_event);
    configASSERT(ok == pdPASS);
    ok = xTaskCreate(gate_blink, "blink", GATE_BLINK_WORDS, NULL, 2U, NULL);
    configASSERT(ok == pdPASS);
    ok = xTaskCreate(gate_hog, "hog", GATE_HOG_WORDS, NULL, 1U, NULL);
    configASSERT(ok == pdPASS);
    plat_button_set_notify(button_notify);
    n = fmt_snprintf(buf, sizeof buf, "GATE up tick_hz=%lu core_hz=%lu heap_free=%lu\r\n",
                     (unsigned long)configTICK_RATE_HZ, (unsigned long)plat_time_hz(),
                     (unsigned long)xPortGetFreeHeapSize());
    if (n > 0) {
        (void)plat_uart_write((const uint8_t *)buf, (size_t)n);
    }
    vTaskStartScheduler();
    fatal("FATAL scheduler returned\r\n");
}