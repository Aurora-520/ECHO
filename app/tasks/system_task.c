#include "system_task.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "rtos_diagnostics.h"
#include "rtos_hooks.h"
#include "task.h"

#define SYSTEM_TASK_PERIOD      pdMS_TO_TICKS(10U)
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

        delayed = xTaskDelayUntil(&last_wake_time, SYSTEM_TASK_PERIOD);
        now = xTaskGetTickCount();

        if (delayed == pdFALSE) {
            TickType_t lateness = (TickType_t) (now - last_wake_time);

            g_rtos_diag.system_deadline_miss_count++;
            g_rtos_diag.system_last_lateness_ticks = lateness;
            if (lateness > g_rtos_diag.system_max_lateness_ticks) {
                g_rtos_diag.system_max_lateness_ticks = lateness;
            }
            last_wake_time = now;
        }

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
    }
}
