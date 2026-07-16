#include "service_task.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "bsp_led.h"
#include "command_service.h"
#include "motor_profile.h"
#include "queue.h"
#include "rtos_diagnostics.h"
#include "rtos_hooks.h"
#include "serial_tx.h"
#include "system_health.h"
#include "task.h"
#include "telemetry.h"

#define SERVICE_TASK_PERIOD pdMS_TO_TICKS(2U)
#define SERVICE_HEARTBEAT_TIMEOUT pdMS_TO_TICKS(1500U)
#define SERVICE_HEALTH_REFRESH_PERIOD pdMS_TO_TICKS(100U)
#define SERVICE_HEALTH_TELEMETRY_PERIOD pdMS_TO_TICKS(1000U)

void ServiceTask_Entry(void *context)
{
    QueueHandle_t heartbeat_queue = (QueueHandle_t) context;
    TickType_t last_wake_time = xTaskGetTickCount();
    TickType_t last_heartbeat_time = last_wake_time;
    TickType_t last_health_refresh_time = last_wake_time;
    TickType_t last_health_telemetry_time = last_wake_time;
    uint32_t heartbeat_sequence = 0U;

    configASSERT(heartbeat_queue != NULL);

    for (;;) {
        TickType_t now;

        (void) xTaskDelayUntil(&last_wake_time, SERVICE_TASK_PERIOD);
        now = xTaskGetTickCount();
        g_rtos_diag.service_task_run_count++;
        g_rtos_diag.service_task_last_wake_tick = now;

        CommandService_ProcessRx();
        SerialTx_Service();

        if (xQueueReceive(
                heartbeat_queue, &heartbeat_sequence, 0U) == pdPASS) {
            last_heartbeat_time = now;
            g_rtos_diag.queue_receive_count++;
            g_rtos_diag.last_heartbeat_sequence = heartbeat_sequence;

            BSP_LED_Toggle();
            g_rtos_diag.led_toggle_count++;
            g_rtos_diag.last_led_toggle_tick = now;
            RtosDiagnostics_Refresh();
        } else if ((TickType_t) (now - last_heartbeat_time) >=
                   SERVICE_HEARTBEAT_TIMEOUT) {
            RtosFault_Halt(RTOS_FAULT_HEARTBEAT_TIMEOUT,
                xTaskGetCurrentTaskHandle(), "Service", 0);
        }

        if ((TickType_t) (now - last_health_refresh_time) >=
            SERVICE_HEALTH_REFRESH_PERIOD) {
            system_health_snapshot_t health;

            last_health_refresh_time = now;
            SystemHealth_ServiceRefresh();
            if ((TickType_t) (now - last_health_telemetry_time) >=
                SERVICE_HEALTH_TELEMETRY_PERIOD) {
                last_health_telemetry_time = now;
                if (SystemHealth_GetSnapshot(&health)) {
                    (void) Telemetry_PublishHealth(&health);
                }
                (void) Telemetry_PublishMotorProfile(
                    MotorProfile_GetActive());
            }
        }
    }
}
