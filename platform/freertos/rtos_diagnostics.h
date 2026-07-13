#ifndef ECHO_RTOS_DIAGNOSTICS_H
#define ECHO_RTOS_DIAGNOSTICS_H

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#define RTOS_DIAGNOSTICS_MAGIC   0x4543484FUL
#define RTOS_DIAGNOSTICS_VERSION 1UL

typedef enum {
    RTOS_FAULT_NONE = 0,
    RTOS_FAULT_ASSERT,
    RTOS_FAULT_MALLOC_FAILED,
    RTOS_FAULT_STACK_OVERFLOW,
    RTOS_FAULT_QUEUE_CREATE,
    RTOS_FAULT_SYSTEM_TASK_CREATE,
    RTOS_FAULT_SERVICE_TASK_CREATE,
    RTOS_FAULT_SCHEDULER_RETURNED,
    RTOS_FAULT_QUEUE_SEND,
    RTOS_FAULT_HEARTBEAT_TIMEOUT
} rtos_fault_code_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t scheduler_started;
    uint32_t heap_initialized;
    uint32_t diagnostics_valid;
    uint32_t diagnostics_update_sequence;

    uint32_t system_task_run_count;
    uint32_t service_task_run_count;
    uint32_t system_deadline_miss_count;
    TickType_t system_last_lateness_ticks;
    TickType_t system_max_lateness_ticks;
    uint32_t queue_send_count;
    uint32_t queue_receive_count;
    uint32_t queue_send_fail_count;
    uint32_t led_toggle_count;
    uint32_t last_heartbeat_sequence;

    TickType_t uptime_ticks;
    TickType_t system_task_last_wake_tick;
    TickType_t service_task_last_wake_tick;
    TickType_t last_led_toggle_tick;
    TickType_t last_diagnostics_update_tick;

    configSTACK_DEPTH_TYPE system_stack_allocated_words;
    configSTACK_DEPTH_TYPE service_stack_allocated_words;
    configSTACK_DEPTH_TYPE idle_stack_allocated_words;
    configSTACK_DEPTH_TYPE timer_stack_allocated_words;
    configSTACK_DEPTH_TYPE system_stack_min_free_words;
    configSTACK_DEPTH_TYPE service_stack_min_free_words;
    configSTACK_DEPTH_TYPE idle_stack_min_free_words;
    configSTACK_DEPTH_TYPE timer_stack_min_free_words;
    configSTACK_DEPTH_TYPE system_stack_max_used_words;
    configSTACK_DEPTH_TYPE service_stack_max_used_words;
    configSTACK_DEPTH_TYPE idle_stack_max_used_words;
    configSTACK_DEPTH_TYPE timer_stack_max_used_words;
    size_t heap_free_bytes;
    size_t heap_min_ever_free_bytes;

    TaskHandle_t system_task_handle;
    TaskHandle_t service_task_handle;
    TaskHandle_t idle_task_handle;
    TaskHandle_t timer_task_handle;
    QueueHandle_t heartbeat_queue_handle;

    uint32_t fault_code;
    uint32_t fault_count;
    int32_t fault_line;
    TaskHandle_t fault_task_handle;
    const char *fault_task_name;
    const char *fault_file;
} rtos_diagnostics_t;

extern volatile rtos_diagnostics_t g_rtos_diag;

void RtosDiagnostics_Init(void);
void RtosDiagnostics_SetObjects(TaskHandle_t system_task,
    TaskHandle_t service_task, QueueHandle_t heartbeat_queue,
    configSTACK_DEPTH_TYPE system_stack_words,
    configSTACK_DEPTH_TYPE service_stack_words);
void RtosDiagnostics_Refresh(void);
void RtosDiagnostics_RecordFault(rtos_fault_code_t code, TaskHandle_t task,
    const char *task_name, const char *file, int line);

#endif
