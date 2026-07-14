#include "rtos_diagnostics.h"

#include <limits.h>

#include "timers.h"

typedef struct {
    bool initialized;
    uint32_t previous_start_us;
    uint32_t expected_release_us;
} system_timing_state_t;

volatile rtos_diagnostics_t g_rtos_diag;

static system_timing_state_t s_system_timing;

static configSTACK_DEPTH_TYPE RtosDiagnostics_StackUsed(
    configSTACK_DEPTH_TYPE allocated, configSTACK_DEPTH_TYPE free_words)
{
    return (free_words <= allocated) ? (allocated - free_words) : 0U;
}

static uint32_t RtosDiagnostics_Magnitude(int32_t value)
{
    return (value < 0) ? (0U - (uint32_t) value) : (uint32_t) value;
}

static bool RtosDiagnostics_SignedDifference(
    uint32_t actual, uint32_t reference, int32_t *difference)
{
    uint32_t raw = (uint32_t) (actual - reference);

    if (raw == 0x80000000UL) {
        return false;
    }

    if (raw <= (uint32_t) INT32_MAX) {
        *difference = (int32_t) raw;
    } else {
        *difference = -(int32_t) (0U - raw);
    }
    return true;
}

static void RtosDiagnostics_UpdateMinMax(uint32_t value,
    uint32_t sample_count, volatile uint32_t *minimum,
    volatile uint32_t *maximum)
{
    if (sample_count == 0U) {
        *minimum = value;
        *maximum = value;
    } else {
        if (value < *minimum) {
            *minimum = value;
        }
        if (value > *maximum) {
            *maximum = value;
        }
    }
}

void RtosDiagnostics_Init(uint32_t timebase_frequency_hz)
{
    void *heap_probe;

    s_system_timing.initialized = false;
    s_system_timing.previous_start_us = 0U;
    s_system_timing.expected_release_us = 0U;

    g_rtos_diag.magic = RTOS_DIAGNOSTICS_MAGIC;
    g_rtos_diag.version = RTOS_DIAGNOSTICS_VERSION;
    g_rtos_diag.configured_cpu_clock_hz = (uint32_t) configCPU_CLOCK_HZ;
    g_rtos_diag.timebase_frequency_hz = timebase_frequency_hz;
    g_rtos_diag.system_period_target_us =
        RTOS_DIAGNOSTICS_SYSTEM_PERIOD_US;
    g_rtos_diag.system_min_period_us = UINT32_MAX;
    g_rtos_diag.system_min_execution_us = UINT32_MAX;

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

void RtosDiagnostics_RecordSystemTiming(
    uint32_t start_us, uint32_t finish_us, bool resynchronize_next_period)
{
    bool first_sample = !s_system_timing.initialized;
    bool period_valid = false;
    bool release_valid = false;
    bool execution_valid;
    bool deadline_valid = false;
    bool sample_valid;
    uint32_t period_us = 0U;
    uint32_t jitter_us = 0U;
    uint32_t execution_us = (uint32_t) (finish_us - start_us);
    uint32_t expected_release_us;
    uint32_t deadline_us;
    uint32_t lateness_us = 0U;
    uint32_t overrun_us = 0U;
    int32_t period_error_us = 0;
    int32_t release_error_us = 0;
    int32_t deadline_error_us = 0;

    if (first_sample) {
        expected_release_us = start_us;
    } else {
        period_us = (uint32_t) (
            start_us - s_system_timing.previous_start_us);
        period_valid = (period_us <= (uint32_t) INT32_MAX);
        expected_release_us = s_system_timing.expected_release_us +
            RTOS_DIAGNOSTICS_SYSTEM_PERIOD_US;

        if (period_valid) {
            if (period_us >= RTOS_DIAGNOSTICS_SYSTEM_PERIOD_US) {
                period_error_us = (int32_t) (
                    period_us - RTOS_DIAGNOSTICS_SYSTEM_PERIOD_US);
            } else {
                period_error_us = -(int32_t) (
                    RTOS_DIAGNOSTICS_SYSTEM_PERIOD_US - period_us);
            }
            jitter_us = RtosDiagnostics_Magnitude(period_error_us);
            release_valid = RtosDiagnostics_SignedDifference(start_us,
                expected_release_us, &release_error_us);
            if (release_valid && release_error_us > 0) {
                lateness_us = (uint32_t) release_error_us;
            }
        }
    }

    execution_valid = (execution_us <= (uint32_t) INT32_MAX);
    deadline_us = expected_release_us +
        RTOS_DIAGNOSTICS_SYSTEM_PERIOD_US;
    if (execution_valid && (first_sample || release_valid)) {
        deadline_valid = RtosDiagnostics_SignedDifference(
            finish_us, deadline_us, &deadline_error_us);
        if (deadline_valid && deadline_error_us > 0) {
            overrun_us = (uint32_t) deadline_error_us;
        }
    }

    sample_valid = execution_valid && deadline_valid &&
        (first_sample || (period_valid && release_valid));

    g_rtos_diag.system_timing_update_sequence++;

    if (resynchronize_next_period) {
        g_rtos_diag.system_delay_no_block_count++;
    }

    if (first_sample) {
        s_system_timing.initialized = true;
        g_rtos_diag.system_timing_initialized = 1U;
    }

    if (!first_sample && period_valid) {
        RtosDiagnostics_UpdateMinMax(period_us,
            g_rtos_diag.system_period_sample_count,
            &g_rtos_diag.system_min_period_us,
            &g_rtos_diag.system_max_period_us);
        g_rtos_diag.system_period_sample_count++;
        g_rtos_diag.system_last_period_us = period_us;
        g_rtos_diag.system_last_period_error_us = period_error_us;
        g_rtos_diag.system_last_jitter_us = jitter_us;
        if (jitter_us > g_rtos_diag.system_max_jitter_us) {
            g_rtos_diag.system_max_jitter_us = jitter_us;
        }
    } else {
        g_rtos_diag.system_last_period_us = 0U;
        g_rtos_diag.system_last_period_error_us = 0;
        g_rtos_diag.system_last_jitter_us = 0U;
    }

    if (!first_sample && release_valid) {
        g_rtos_diag.system_release_sample_count++;
        g_rtos_diag.system_last_release_error_us = release_error_us;
        g_rtos_diag.system_last_lateness_us = lateness_us;
        if (lateness_us > g_rtos_diag.system_max_lateness_us) {
            g_rtos_diag.system_max_lateness_us = lateness_us;
        }
    } else {
        g_rtos_diag.system_last_release_error_us = 0;
        g_rtos_diag.system_last_lateness_us = 0U;
    }

    if (execution_valid) {
        RtosDiagnostics_UpdateMinMax(execution_us,
            g_rtos_diag.system_execution_sample_count,
            &g_rtos_diag.system_min_execution_us,
            &g_rtos_diag.system_max_execution_us);
        g_rtos_diag.system_execution_sample_count++;
        g_rtos_diag.system_last_execution_us = execution_us;
    } else {
        g_rtos_diag.system_last_execution_us = 0U;
    }

    if (deadline_valid) {
        g_rtos_diag.system_last_deadline_overrun_us = overrun_us;
        if (overrun_us > 0U) {
            g_rtos_diag.system_deadline_miss_count++;
            if (overrun_us > g_rtos_diag.system_max_deadline_overrun_us) {
                g_rtos_diag.system_max_deadline_overrun_us = overrun_us;
            }
        }
    } else {
        g_rtos_diag.system_last_deadline_overrun_us = 0U;
    }

    if (!sample_valid) {
        g_rtos_diag.system_timing_invalid_count++;
        expected_release_us = start_us;
        deadline_us = start_us + RTOS_DIAGNOSTICS_SYSTEM_PERIOD_US;
    }

    g_rtos_diag.system_last_sample_valid = sample_valid ? 1U : 0U;
    g_rtos_diag.system_last_start_us = start_us;
    g_rtos_diag.system_last_finish_us = finish_us;
    g_rtos_diag.system_expected_release_us = expected_release_us;
    g_rtos_diag.system_deadline_us = deadline_us;

    s_system_timing.previous_start_us = start_us;
    if (resynchronize_next_period || !sample_valid) {
        s_system_timing.expected_release_us = start_us;
        g_rtos_diag.system_timing_resync_count++;
    } else {
        s_system_timing.expected_release_us = expected_release_us;
    }

    g_rtos_diag.system_timing_update_sequence++;
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
