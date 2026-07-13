#include "rtos_diagnostics.h"

#include "timers.h"

volatile rtos_diagnostics_t g_rtos_diag;

static configSTACK_DEPTH_TYPE RtosDiagnostics_StackUsed(
    configSTACK_DEPTH_TYPE allocated, configSTACK_DEPTH_TYPE free_words)
{
    return (free_words <= allocated) ? (allocated - free_words) : 0U;
}

void RtosDiagnostics_Init(void)
{
    void *heap_probe;

    g_rtos_diag.magic = RTOS_DIAGNOSTICS_MAGIC;
    g_rtos_diag.version = RTOS_DIAGNOSTICS_VERSION;

    heap_probe = pvPortMalloc(1U);
    if (heap_probe != NULL) {
        vPortFree(heap_probe);
        xPortResetHeapMinimumEverFreeHeapSize();
        g_rtos_diag.heap_initialized = 1U;
        g_rtos_diag.heap_free_bytes = xPortGetFreeHeapSize();
        g_rtos_diag.heap_min_ever_free_bytes =
            xPortGetMinimumEverFreeHeapSize();
    }
}

void RtosDiagnostics_SetObjects(TaskHandle_t system_task,
    TaskHandle_t service_task, QueueHandle_t heartbeat_queue,
    configSTACK_DEPTH_TYPE system_stack_words,
    configSTACK_DEPTH_TYPE service_stack_words)
{
    configSTACK_DEPTH_TYPE invalid =
        (configSTACK_DEPTH_TYPE) ~(configSTACK_DEPTH_TYPE) 0U;

    g_rtos_diag.system_task_handle = system_task;
    g_rtos_diag.service_task_handle = service_task;
    g_rtos_diag.heartbeat_queue_handle = heartbeat_queue;

    g_rtos_diag.system_stack_allocated_words = system_stack_words;
    g_rtos_diag.service_stack_allocated_words = service_stack_words;
    g_rtos_diag.idle_stack_allocated_words = configIDLE_TASK_STACK_DEPTH;
    g_rtos_diag.timer_stack_allocated_words = configTIMER_TASK_STACK_DEPTH;

    g_rtos_diag.system_stack_min_free_words = invalid;
    g_rtos_diag.service_stack_min_free_words = invalid;
    g_rtos_diag.idle_stack_min_free_words = invalid;
    g_rtos_diag.timer_stack_min_free_words = invalid;
}

void RtosDiagnostics_Refresh(void)
{
    g_rtos_diag.diagnostics_update_sequence++;

    g_rtos_diag.idle_task_handle = xTaskGetIdleTaskHandle();
    g_rtos_diag.timer_task_handle = xTimerGetTimerDaemonTaskHandle();

    g_rtos_diag.system_stack_min_free_words =
        uxTaskGetStackHighWaterMark2(g_rtos_diag.system_task_handle);
    g_rtos_diag.service_stack_min_free_words =
        uxTaskGetStackHighWaterMark2(g_rtos_diag.service_task_handle);
    g_rtos_diag.idle_stack_min_free_words =
        uxTaskGetStackHighWaterMark2(g_rtos_diag.idle_task_handle);
    g_rtos_diag.timer_stack_min_free_words =
        uxTaskGetStackHighWaterMark2(g_rtos_diag.timer_task_handle);

    g_rtos_diag.system_stack_max_used_words = RtosDiagnostics_StackUsed(
        g_rtos_diag.system_stack_allocated_words,
        g_rtos_diag.system_stack_min_free_words);
    g_rtos_diag.service_stack_max_used_words = RtosDiagnostics_StackUsed(
        g_rtos_diag.service_stack_allocated_words,
        g_rtos_diag.service_stack_min_free_words);
    g_rtos_diag.idle_stack_max_used_words = RtosDiagnostics_StackUsed(
        g_rtos_diag.idle_stack_allocated_words,
        g_rtos_diag.idle_stack_min_free_words);
    g_rtos_diag.timer_stack_max_used_words = RtosDiagnostics_StackUsed(
        g_rtos_diag.timer_stack_allocated_words,
        g_rtos_diag.timer_stack_min_free_words);

    g_rtos_diag.heap_free_bytes = xPortGetFreeHeapSize();
    g_rtos_diag.heap_min_ever_free_bytes =
        xPortGetMinimumEverFreeHeapSize();
    g_rtos_diag.uptime_ticks = xTaskGetTickCount();
    g_rtos_diag.last_diagnostics_update_tick = g_rtos_diag.uptime_ticks;
    g_rtos_diag.diagnostics_valid = 1U;

    g_rtos_diag.diagnostics_update_sequence++;
}

void RtosDiagnostics_RecordFault(rtos_fault_code_t code, TaskHandle_t task,
    const char *task_name, const char *file, int line)
{
    g_rtos_diag.fault_count++;
    if (g_rtos_diag.fault_code == (uint32_t) RTOS_FAULT_NONE) {
        g_rtos_diag.fault_task_handle = task;
        g_rtos_diag.fault_task_name = task_name;
        g_rtos_diag.fault_file = file;
        g_rtos_diag.fault_line = line;
        g_rtos_diag.fault_code = (uint32_t) code;
    }
}
