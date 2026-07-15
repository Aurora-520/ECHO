#include "app_tasks.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "display_task.h"
#include "imu_service.h"
#include "queue.h"
#include "rtos_diagnostics.h"
#include "rtos_hooks.h"
#include "service_task.h"
#include "system_task.h"
#include "task.h"
#include "telemetry.h"

#define APP_SYSTEM_TASK_STACK_WORDS ((configSTACK_DEPTH_TYPE) 256U)
#define APP_SERVICE_TASK_STACK_WORDS ((configSTACK_DEPTH_TYPE) 256U)
#define APP_SYSTEM_TASK_PRIORITY     (tskIDLE_PRIORITY + 2U)
#define APP_SERVICE_TASK_PRIORITY    (tskIDLE_PRIORITY + 1U)
#define APP_TELEMETRY_TASK_PRIORITY  (tskIDLE_PRIORITY + 1U)
#define APP_DISPLAY_TASK_PRIORITY    tskIDLE_PRIORITY
#define APP_HEARTBEAT_QUEUE_LENGTH   1U

static StaticTask_t s_system_task_tcb;
static StackType_t s_system_task_stack[APP_SYSTEM_TASK_STACK_WORDS];
static StaticTask_t s_service_task_tcb;
static StackType_t s_service_task_stack[APP_SERVICE_TASK_STACK_WORDS];
static StaticTask_t s_display_task_tcb;
static StackType_t s_display_task_stack[DISPLAY_TASK_STACK_WORDS];

static StaticQueue_t s_heartbeat_queue_control;
static uint32_t s_heartbeat_queue_storage[APP_HEARTBEAT_QUEUE_LENGTH];

void AppTasks_CreateAll(void)
{
    QueueHandle_t heartbeat_queue;
    TaskHandle_t system_task;
    TaskHandle_t service_task;
    TaskHandle_t telemetry_task;
    TaskHandle_t display_task;

    heartbeat_queue = xQueueCreateStatic(APP_HEARTBEAT_QUEUE_LENGTH,
        sizeof(s_heartbeat_queue_storage[0]),
        (uint8_t *) s_heartbeat_queue_storage, &s_heartbeat_queue_control);
    if (heartbeat_queue == NULL) {
        RtosFault_Halt(RTOS_FAULT_QUEUE_CREATE, NULL, NULL, 0);
    }
    vQueueAddToRegistry(heartbeat_queue, "HeartbeatQ");
    DisplayTask_Init();
    ImuService_Init();

    system_task = xTaskCreateStatic(SystemTask_Entry, "System",
        APP_SYSTEM_TASK_STACK_WORDS, heartbeat_queue,
        APP_SYSTEM_TASK_PRIORITY, s_system_task_stack, &s_system_task_tcb);
    if (system_task == NULL) {
        RtosFault_Halt(RTOS_FAULT_SYSTEM_TASK_CREATE, NULL, NULL, 0);
    }

    service_task = xTaskCreateStatic(ServiceTask_Entry, "Service",
        APP_SERVICE_TASK_STACK_WORDS, heartbeat_queue,
        APP_SERVICE_TASK_PRIORITY, s_service_task_stack, &s_service_task_tcb);
    if (service_task == NULL) {
        RtosFault_Halt(RTOS_FAULT_SERVICE_TASK_CREATE, system_task, NULL, 0);
    }

    telemetry_task = Telemetry_CreateTask(APP_TELEMETRY_TASK_PRIORITY);
    if (telemetry_task == NULL) {
        RtosFault_Halt(RTOS_FAULT_TELEMETRY_TASK_CREATE, system_task,
            "Telemetry", 0);
    }

    display_task = xTaskCreateStatic(DisplayTask_Entry, "Display",
        DISPLAY_TASK_STACK_WORDS, NULL, APP_DISPLAY_TASK_PRIORITY,
        s_display_task_stack, &s_display_task_tcb);
    if (display_task == NULL) {
        RtosFault_Halt(RTOS_FAULT_DISPLAY_TASK_CREATE, system_task,
            "Display", 0);
    }

    RtosDiagnostics_SetObjects(system_task, service_task, telemetry_task,
        display_task, heartbeat_queue, APP_SYSTEM_TASK_STACK_WORDS,
        APP_SERVICE_TASK_STACK_WORDS, TELEMETRY_TASK_STACK_WORDS,
        DISPLAY_TASK_STACK_WORDS);
}
