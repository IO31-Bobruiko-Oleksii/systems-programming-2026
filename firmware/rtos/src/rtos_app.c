#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "fmt.h"
#include "logger.h"
#include "plat_button.h"
#include "plat_probe.h"
#include "plat_time.h"
#include "plat_uart.h"
#include "rtos_app.h"

void xPortSysTickHandler(void);

#define EOL                "\r\n"
#define MAX_TASKS          8U
#define LOG_QUEUE_LEN      16U
#define LOGGER_WAKE_MS     100U
#define LOAD_PASS_MS       5U
#define RUNTIME_SHIFT      4U

#define PRIO_EVENT         5U
#define PRIO_TRACK         4U
#define PRIO_CMD           3U
#define PRIO_LOGGER        2U
#define PRIO_LOAD          1U

#define WORDS_EVENT        160U
#define WORDS_TRACK        160U
#define WORDS_CMD          256U
#define WORDS_LOGGER       128U
#define WORDS_LOAD         192U

typedef struct {
    uint32_t t_ms;
    uint16_t raw[LOGGER_SENSORS];
    uint8_t  az;
    uint8_t  tilt;
    uint8_t  flags;
} log_rec_t;

typedef struct {
    TaskHandle_t handle;
    const char  *name;
    uint16_t     words;
    uint8_t      prio;
} task_info_t;

static app_t *g_app;
static SemaphoreHandle_t uart_mutex;
static SemaphoreHandle_t state_mutex;
static SemaphoreHandle_t sd_mutex;
static QueueHandle_t log_q;
static task_info_t tasks[MAX_TASKS];
static unsigned task_count;
static SemaphoreHandle_t sem_event;
static SemaphoreHandle_t sem_cmd;
static SemaphoreHandle_t sem_load;

static volatile uint32_t switches;
static volatile uint32_t log_dropped;
static uint32_t base_run[MAX_TASKS];
static uint32_t base_now;
static uint32_t base_switches;
static uint32_t base_ticks;

#ifdef RTOS_BENCH
static volatile uint32_t tick_probe;
static uint32_t tick_n;
static uint32_t tick_sum;
static uint32_t tick_max;

static void tick_reset(void)
{
    tick_n = 0U;
    tick_sum = 0U;
    tick_max = 0U;
}
#endif

static void fatal_text(const char *text)
{
    portDISABLE_INTERRUPTS();
    (void)plat_uart_write((const uint8_t *)text, strlen(text));
    for (;;) {
    }
}

void rtos_assert(unsigned line)
{
    char buf[40];
    int n = fmt_snprintf(buf, sizeof buf, "FATAL assert line=%u" EOL, line);

    portDISABLE_INTERRUPTS();
    if (n > 0) {
        (void)plat_uart_write((const uint8_t *)buf, (size_t)n);
    }
    for (;;) {
    }
}

void vApplicationMallocFailedHook(void)
{
    fatal_text("FATAL malloc failed" EOL);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    char buf[48];
    int n = fmt_snprintf(buf, sizeof buf, "FATAL stack overflow in %s" EOL, name);

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
#ifdef RTOS_BENCH
    uint32_t t0;
    uint32_t d;

    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        return;
    }
    if (tick_probe == 0U) {
        xPortSysTickHandler();
        return;
    }
    t0 = plat_time_cycles();
    xPortSysTickHandler();
    d = plat_time_cycles() - t0;
    tick_n++;
    tick_sum += d;
    if (d > tick_max) {
        tick_max = d;
    }
#else
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
#endif
}

void rtos_trace_switched_in(void)
{
    switches++;
    if (xTaskGetCurrentTaskHandle() == xTaskGetIdleTaskHandle()) {
        plat_probe_clear(PLAT_PROBE_BUSY);
    } else {
        plat_probe_set(PLAT_PROBE_BUSY);
    }
}

static void hook_enter(void)
{
    taskENTER_CRITICAL();
}

static void hook_leave(void)
{
    taskEXIT_CRITICAL();
}

static void hook_lock(void)
{
    (void)xSemaphoreTake(state_mutex, portMAX_DELAY);
}

static void hook_unlock(void)
{
    (void)xSemaphoreGive(state_mutex);
}

static void hook_sd_lock(void)
{
    (void)xSemaphoreTake(sd_mutex, portMAX_DELAY);
}

static void hook_sd_unlock(void)
{
    (void)xSemaphoreGive(sd_mutex);
}

static void hook_out_lock(void)
{
    (void)xSemaphoreTake(uart_mutex, portMAX_DELAY);
}

static void hook_out_unlock(void)
{
    (void)xSemaphoreGive(uart_mutex);
}

static int hook_log_put(uint32_t t_ms, const uint16_t raw[LOGGER_SENSORS], uint8_t az,
                        uint8_t tilt, uint8_t flags)
{
    log_rec_t rec;
    unsigned i;

    rec.t_ms = t_ms;
    for (i = 0U; i < LOGGER_SENSORS; i++) {
        rec.raw[i] = raw[i];
    }
    rec.az = az;
    rec.tilt = tilt;
    rec.flags = flags;
    if (xQueueSend(log_q, &rec, 0U) != pdPASS) {
        log_dropped++;
        return 0;
    }
    return 1;
}

static void hook_log_drain(void)
{
    log_rec_t rec;

    while (xQueueReceive(log_q, &rec, 0U) == pdPASS) {
        (void)logger_add(&g_app->logger, rec.t_ms, rec.raw, rec.az, rec.tilt, rec.flags);
    }
}

static void hook_load_start(void)
{
    (void)xSemaphoreGive(sem_load);
}

static TaskHandle_t handle_of(unsigned i)
{
    return (i < task_count) ? tasks[i].handle : xTaskGetIdleTaskHandle();
}

static uint32_t runtime_now(void)
{
    return plat_time_cycles() >> RUNTIME_SHIFT;
}

static void hook_stats_reset(void)
{
    unsigned i;

    for (i = 0U; i <= task_count; i++) {
        base_run[i] = ulTaskGetRunTimeCounter(handle_of(i));
    }
    base_now = runtime_now();
    base_switches = switches;
    base_ticks = (uint32_t)xTaskGetTickCount();
    log_dropped = 0U;
#ifdef RTOS_BENCH
    tick_reset();
#endif
}

static uint64_t hook_busy_cycles(uint64_t window_cycles)
{
    uint64_t idle = (uint64_t)(ulTaskGetRunTimeCounter(handle_of(task_count)) -
                               base_run[task_count]) << RUNTIME_SHIFT;

    return (window_cycles > idle) ? (window_cycles - idle) : 0U;
}

static void say(const char *fmt, ...)
{
    char buf[128];
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
    hook_out_lock();
    (void)plat_uart_write((const uint8_t *)buf, (size_t)n);
    hook_out_unlock();
}

static void cmd_tasks(void)
{
    uint32_t window = runtime_now() - base_now;
    uint32_t ticks = (uint32_t)xTaskGetTickCount() - base_ticks;
    uint32_t sw = switches - base_switches;
    uint32_t hz = plat_time_hz();
    uint32_t run[MAX_TASKS];
    unsigned i;

    for (i = 0U; i <= task_count; i++) {
        run[i] = ulTaskGetRunTimeCounter(handle_of(i)) - base_run[i];
    }
    for (i = 0U; i <= task_count; i++) {
        uint32_t stack = (uint32_t)tasks[i].words * 4U;
        uint32_t peak = stack - (uint32_t)uxTaskGetStackHighWaterMark(handle_of(i)) * 4U;

        say("STAT task name=%s prio=%u run_us=%lu cpu_pm=%lu stack=%lu peak=%lu" EOL,
            tasks[i].name, (unsigned)tasks[i].prio,
            (unsigned long)((run[i] << RUNTIME_SHIFT) / (hz / 1000000U)),
            (unsigned long)((window != 0U) ? (uint32_t)(((uint64_t)run[i] * 1000U) / window) : 0U),
            (unsigned long)stack, (unsigned long)peak);
    }
    say("STAT os tick_hz=%lu ticks=%lu switches=%lu log_dropped=%lu" EOL,
        (unsigned long)configTICK_RATE_HZ, (unsigned long)ticks, (unsigned long)sw,
        (unsigned long)log_dropped);
    say("CMD tasks OK count=%u" EOL, task_count + 1U);
}

static void cmd_heap(void)
{
    size_t total = configTOTAL_HEAP_SIZE;
    size_t free_now = xPortGetFreeHeapSize();

    say("CMD heap OK total=%lu free=%lu used=%lu" EOL, (unsigned long)total,
        (unsigned long)free_now, (unsigned long)(total - free_now));
}

#ifdef RTOS_BENCH
#define CTX_ROUNDS 200U

static void cmd_ctx(void)
{
    uint32_t over = UINT32_MAX;
    uint32_t sum = 0U;
    uint32_t mn = UINT32_MAX;
    uint32_t mx = 0U;
    unsigned i;

    for (i = 0U; i < 16U; i++) {
        uint32_t a = plat_time_cycles();
        uint32_t b = plat_time_cycles();

        if (b - a < over) {
            over = b - a;
        }
    }
    tick_reset();
    tick_probe = 1U;
    for (i = 0U; i < CTX_ROUNDS; i++) {
        uint32_t t0 = plat_time_cycles();
        uint32_t d;

        (void)xSemaphoreGive(sem_event);
        d = plat_time_cycles() - t0 - over;
        sum += d;
        if (d < mn) {
            mn = d;
        }
        if (d > mx) {
            mx = d;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    tick_probe = 0U;
    say("CMD ctx OK rounds=%u read_cyc=%lu min_cyc=%lu avg_cyc=%lu max_cyc=%lu" EOL, CTX_ROUNDS,
        (unsigned long)over, (unsigned long)mn, (unsigned long)(sum / CTX_ROUNDS),
        (unsigned long)mx);
    if (tick_n != 0U) {
        say("STAT os tick_isr n=%lu avg_cyc=%lu max_cyc=%lu" EOL, (unsigned long)tick_n,
            (unsigned long)(tick_sum / tick_n - over), (unsigned long)(tick_max - over));
    }
}

#endif /* RTOS_BENCH */

static int hook_command(unsigned argc, char *argv[])
{
#ifdef RTOS_BENCH
    if (argc == 1U && strcmp(argv[0], "ctx") == 0) {
        cmd_ctx();
        return 1;
    }
#endif
    if (argc == 1U && strcmp(argv[0], "tasks") == 0) {
        cmd_tasks();
        return 1;
    }
    if (argc == 1U && strcmp(argv[0], "heap") == 0) {
        cmd_heap();
        return 1;
    }
    return 0;
}

static const app_os_t os_hooks = {
    hook_enter, hook_leave, hook_lock, hook_unlock, hook_sd_lock, hook_sd_unlock,
    hook_out_lock, hook_out_unlock, hook_log_put, hook_log_drain, hook_load_start,
    hook_stats_reset, hook_busy_cycles, hook_command
};

static void button_notify(void)
{
    BaseType_t woken = pdFALSE;

    (void)xSemaphoreGiveFromISR(sem_event, &woken);
    portYIELD_FROM_ISR(woken);
}

static void uart_notify(void)
{
    BaseType_t woken = pdFALSE;

    (void)xSemaphoreGiveFromISR(sem_cmd, &woken);
    portYIELD_FROM_ISR(woken);
}

static void event_task(void *arg)
{
    app_t *app = arg;

    for (;;) {
        (void)xSemaphoreTake(sem_event, portMAX_DELAY);
        app_service_button(app);
    }
}

static void track_task(void *arg)
{
    app_t *app = arg;
    const TickType_t period = pdMS_TO_TICKS(APP_TRACK_PERIOD_MS);
    const uint32_t per_tick = plat_time_hz() / (uint32_t)configTICK_RATE_HZ;
    TickType_t last = xTaskGetTickCount();
    TickType_t base_tick = 0U;
    uint32_t base = 0U;
    int have_base = 0;

    for (;;) {
        uint32_t now;
        uint32_t nominal;
        uint32_t lost;

        vTaskDelayUntil(&last, period);
        now = plat_time_cycles();
        if (have_base == 0) {
            base = now - (now % per_tick);
            base_tick = last;
            have_base = 1;
        }
        nominal = base + (uint32_t)(last - base_tick) * per_tick;
        lost = app_service_track(app, now, nominal);
        last += (TickType_t)(lost * (uint32_t)period);
    }
}

static void cmd_task(void *arg)
{
    app_t *app = arg;

    for (;;) {
        int r;

        while ((r = app_console_read(app, 0U)) != 0) {
            if (r == 1) {
                app_run_command(app, app->line, plat_time_cycles());
            }
        }
        (void)xSemaphoreTake(sem_cmd, portMAX_DELAY);
    }
}

static void logger_task(void *arg)
{
    app_t *app = arg;

    for (;;) {
        log_rec_t rec;
        BaseType_t got = xQueueReceive(log_q, &rec, pdMS_TO_TICKS(LOGGER_WAKE_MS));

        hook_sd_lock();
        if (got == pdPASS) {
            (void)logger_add(&app->logger, rec.t_ms, rec.raw, rec.az, rec.tilt, rec.flags);
        }
        hook_log_drain();
        app_service_log(app);
        hook_sd_unlock();
    }
}

static void load_task(void *arg)
{
    app_t *app = arg;

    for (;;) {
        (void)xSemaphoreTake(sem_load, portMAX_DELAY);
        while (app->load_active != 0U) {
            app_service_load(app);
            vTaskDelay(pdMS_TO_TICKS(LOAD_PASS_MS));
        }
    }
}

static void add_task(TaskFunction_t fn, const char *name, uint16_t words, UBaseType_t prio,
                     app_t *app)
{
    TaskHandle_t h = NULL;
    BaseType_t ok = xTaskCreate(fn, name, words, app, prio, &h);

    configASSERT(ok == pdPASS);
    tasks[task_count].handle = h;
    tasks[task_count].name = name;
    tasks[task_count].words = words;
    tasks[task_count].prio = (uint8_t)prio;
    task_count++;
}

void rtos_start(app_t *app)
{
    char buf[80];
    int n;

    configASSERT(plat_time_hz() == configCPU_CLOCK_HZ);
    g_app = app;
    uart_mutex = xSemaphoreCreateMutex();
    state_mutex = xSemaphoreCreateMutex();
    sd_mutex = xSemaphoreCreateMutex();
    sem_event = xSemaphoreCreateBinary();
    sem_cmd = xSemaphoreCreateBinary();
    sem_load = xSemaphoreCreateBinary();
    log_q = xQueueCreate(LOG_QUEUE_LEN, sizeof(log_rec_t));
    configASSERT(uart_mutex != NULL && state_mutex != NULL && sd_mutex != NULL && log_q != NULL);
    configASSERT(sem_event != NULL && sem_cmd != NULL && sem_load != NULL);

    app->os = os_hooks;

    add_task(event_task, "event", WORDS_EVENT, PRIO_EVENT, app);
    add_task(track_task, "track", WORDS_TRACK, PRIO_TRACK, app);
    add_task(cmd_task, "cmd", WORDS_CMD, PRIO_CMD, app);
    add_task(logger_task, "logger", WORDS_LOGGER, PRIO_LOGGER, app);
    add_task(load_task, "load", WORDS_LOAD, PRIO_LOAD, app);
    tasks[task_count].handle = NULL;
    tasks[task_count].name = "IDLE";
    tasks[task_count].words = configMINIMAL_STACK_SIZE;
    tasks[task_count].prio = 0U;

    plat_button_set_notify(button_notify);
    plat_uart_set_rx_notify(uart_notify);
    n = fmt_snprintf(buf, sizeof buf, "RTOS up tick_hz=%lu core_hz=%lu heap_free=%lu tasks=%u" EOL,
                     (unsigned long)configTICK_RATE_HZ, (unsigned long)plat_time_hz(),
                     (unsigned long)xPortGetFreeHeapSize(), (unsigned)task_count);
    if (n > 0) {
        (void)plat_uart_write((const uint8_t *)buf, (size_t)n);
    }
    vTaskStartScheduler();
    fatal_text("FATAL scheduler returned" EOL);
}
