#ifndef ECHO_DISPLAY_TASK_H
#define ECHO_DISPLAY_TASK_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#define DISPLAY_TASK_STACK_WORDS ((configSTACK_DEPTH_TYPE) 256U)

typedef struct {
    uint32_t run_count;
    uint32_t init_attempt_count;
    uint32_t init_success_count;
    uint32_t refresh_success_count;
    uint32_t refresh_fail_count;
    uint32_t offline_count;
    uint32_t io_window_acquired_count;
    uint32_t io_window_deferred_count;
    uint32_t io_window_skipped_count;
    uint32_t key_event_count;
    TickType_t last_wake_tick;
    uint32_t last_i2c_result;
    uint8_t online;
    uint8_t address;
    uint8_t page_index;
    uint8_t last_key;
} display_task_diagnostics_t;

extern volatile display_task_diagnostics_t g_display_task_diag;

extern volatile uint32_t g_display_debug_refresh_enable;
void DisplayTask_Init(void);
void DisplayTask_Entry(void *context);

#endif
