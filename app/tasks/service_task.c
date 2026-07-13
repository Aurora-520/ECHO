#include "service_task.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "bsp_led.h"
#include "queue.h"
#include "rtos_diagnostics.h"
#include "rtos_hooks.h"
#include "task.h"

#define SERVICE_HEARTBEAT_TIMEOUT pdMS_TO_TICKS(1500U)

void ServiceTask_Entry(void *context)
{
    QueueHandle_t heartbeat_queue = (QueueHandle_t) context;
    uint32_t heartbeat_sequence;

    configASSERT(heartbeat_queue != NULL);

    for (;;) {
        if (xQueueReceive(heartbeat_queue, &heartbeat_sequence,
                SERVICE_HEARTBEAT_TIMEOUT) != pdPASS) {
            RtosFault_Halt(RTOS_FAULT_HEARTBEAT_TIMEOUT,
                xTaskGetCurrentTaskHandle(), "Service", 0);
        }

        g_rtos_diag.service_task_run_count++;
        g_rtos_diag.queue_receive_count++;
        g_rtos_diag.last_heartbeat_sequence = heartbeat_sequence;
        g_rtos_diag.service_task_last_wake_tick = xTaskGetTickCount();

        BSP_LED_Toggle();
        g_rtos_diag.led_toggle_count++;
        g_rtos_diag.last_led_toggle_tick = xTaskGetTickCount();

        RtosDiagnostics_Refresh();
    }
}
