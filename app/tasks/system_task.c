#include "system_task.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "bsp_time.h"
#include "queue.h"
#include "rtos_diagnostics.h"
#include "rtos_hooks.h"
#include "task.h"

#define SYSTEM_TASK_PERIOD \
    pdMS_TO_TICKS(RTOS_DIAGNOSTICS_SYSTEM_PERIOD_US / 1000U)
#define SYSTEM_HEARTBEAT_PERIOD pdMS_TO_TICKS(1000U)

void SystemTask_Entry(void *context)
{
    QueueHandle_t heartbeat_queue = (QueueHandle_t) context;
    TickType_t last_wake_time      = xTaskGetTickCount();
    TickType_t last_heartbeat_time = last_wake_time;
    uint32_t heartbeat_sequence    = 0U;

    configASSERT(heartbeat_queue != NULL);
    g_rtos_diag.scheduler_started = 1U;

    for (;;) {
        BaseType_t delayed;
        TickType_t now;
        uint32_t start_us;
        uint32_t finish_us;
        bool schedule_resynchronized;

        delayed = xTaskDelayUntil(&last_wake_time, SYSTEM_TASK_PERIOD);
        now = xTaskGetTickCount();
        schedule_resynchronized = (delayed == pdFALSE);

        if (schedule_resynchronized) {
            last_wake_time = now;
        }

        start_us = BSP_Time_GetUs();
        g_rtos_diag.system_task_run_count++;
        g_rtos_diag.system_task_last_wake_tick = now;

        if ((TickType_t) (now - last_heartbeat_time) >=
            SYSTEM_HEARTBEAT_PERIOD) {
            last_heartbeat_time = now;
            heartbeat_sequence++;

            if (xQueueOverwrite(heartbeat_queue, &heartbeat_sequence) !=
                pdPASS) {
                g_rtos_diag.queue_send_fail_count++;
                RtosFault_Halt(RTOS_FAULT_QUEUE_SEND,
                    xTaskGetCurrentTaskHandle(), "System", 0);
            }
            g_rtos_diag.queue_send_count++;
        }

        finish_us = BSP_Time_GetUs();
        RtosDiagnostics_RecordSystemTiming(
            start_us, finish_us, schedule_resynchronized);
    }
}
