#include "system_health.h"

#include <limits.h>
#include <string.h>

#include "FreeRTOS.h"
#include "bsp_i2c.h"
#include "bsp_reset.h"
#include "bsp_time.h"
#include "parameter_service.h"
#include "rtos_diagnostics.h"
#include "serial_rx.h"
#include "serial_tx.h"
#include "ssd1306.h"
#include "task.h"
#include "telemetry.h"

#define SYSTEM_HEALTH_CONTROL_STALE_TICKS   pdMS_TO_TICKS(50U)
#define SYSTEM_HEALTH_TELEMETRY_STALE_TICKS pdMS_TO_TICKS(250U)
#define SYSTEM_HEALTH_DISPLAY_STALE_TICKS   pdMS_TO_TICKS(2500U)
#define SYSTEM_HEALTH_EVENT_HOLD_TICKS      pdMS_TO_TICKS(2000U)
#define SYSTEM_HEALTH_STACK_WARN_WORDS      64U
#define SYSTEM_HEALTH_STACK_FAULT_WORDS     32U
#define SYSTEM_HEALTH_HEAP_WARN_BYTES       512U
#define SYSTEM_HEALTH_VALID_ISSUE_MASK \
    ((1UL << ((uint32_t) SYSTEM_HEALTH_ISSUE_COUNT - 1UL)) - 1UL)

typedef struct {
    uint32_t deadline_miss_count;
    uint32_t serial_rx_overflow_count;
    uint32_t serial_tx_drop_count;
    uint32_t uart_dma_stall_count;
    uint32_t telemetry_drop_count;
    uint32_t i2c_error_count;
    uint32_t parameter_ack_drop_count;
} system_health_event_baseline_t;

static const system_health_source_descriptor_t s_source_descriptors[] = {
    { SYSTEM_HEALTH_SOURCE_RTOS, "RTOS", "RTOS diagnostics" },
    { SYSTEM_HEALTH_SOURCE_CONTROL, "CONTROL", "SystemTask" },
    { SYSTEM_HEALTH_SOURCE_COMMUNICATIONS, "COMM", "Serial/Telemetry" },
    { SYSTEM_HEALTH_SOURCE_PARAMETERS, "PARAM", "ParameterService" },
    { SYSTEM_HEALTH_SOURCE_DISPLAY, "DISPLAY", "DisplayTask/SSD1306" },
    { SYSTEM_HEALTH_SOURCE_I2C, "I2C", "BSP I2C" },
    { SYSTEM_HEALTH_SOURCE_STORAGE, "STORAGE", "deferred" },
    { SYSTEM_HEALTH_SOURCE_SENSOR, "SENSOR", "future sensor service" },
    { SYSTEM_HEALTH_SOURCE_ACTUATOR, "ACTUATOR", "future arbiter" }
};

static const system_health_issue_descriptor_t s_issue_descriptors[] = {
    { SYSTEM_HEALTH_ISSUE_NONE, SYSTEM_HEALTH_SOURCE_RTOS,
      SYSTEM_HEALTH_OK, 0U, "none", "NONE", "none", 0U },
    { SYSTEM_HEALTH_ISSUE_RTOS_FATAL, SYSTEM_HEALTH_SOURCE_RTOS,
      SYSTEM_HEALTH_FAULT, 1U, "fault", "RTOS",
      "inspect RTOS fault record and reset safely", 0U },
    { SYSTEM_HEALTH_ISSUE_CONTROL_DEADLINE, SYSTEM_HEALTH_SOURCE_CONTROL,
      SYSTEM_HEALTH_FAULT, 1U, "new miss", "DEADLINE",
      "inspect control timing before enabling outputs", 0U },
    { SYSTEM_HEALTH_ISSUE_CONTROL_STALE, SYSTEM_HEALTH_SOURCE_CONTROL,
      SYSTEM_HEALTH_FAULT, SYSTEM_HEALTH_CONTROL_STALE_TICKS, "ticks",
      "CTRL STALE", "keep actuator output disabled", 0U },
    { SYSTEM_HEALTH_ISSUE_STACK_LOW, SYSTEM_HEALTH_SOURCE_RTOS,
      SYSTEM_HEALTH_DEGRADED, SYSTEM_HEALTH_STACK_WARN_WORDS, "words",
      "STACK LOW", "review task stack margin", 1U },
    { SYSTEM_HEALTH_ISSUE_STACK_CRITICAL, SYSTEM_HEALTH_SOURCE_RTOS,
      SYSTEM_HEALTH_FAULT, SYSTEM_HEALTH_STACK_FAULT_WORDS, "words",
      "STACK CRIT", "stop and increase task stack", 0U },
    { SYSTEM_HEALTH_ISSUE_HEAP_LOW, SYSTEM_HEALTH_SOURCE_RTOS,
      SYSTEM_HEALTH_DEGRADED, SYSTEM_HEALTH_HEAP_WARN_BYTES, "bytes",
      "HEAP LOW", "review heap use and minimum-ever value", 1U },
    { SYSTEM_HEALTH_ISSUE_SERIAL_RX_OVERFLOW,
      SYSTEM_HEALTH_SOURCE_COMMUNICATIONS, SYSTEM_HEALTH_DEGRADED,
      1U, "new overflow", "RX OVERFLOW",
      "reduce host burst or inspect RX servicing", 1U },
    { SYSTEM_HEALTH_ISSUE_SERIAL_TX_DROP,
      SYSTEM_HEALTH_SOURCE_COMMUNICATIONS, SYSTEM_HEALTH_DEGRADED,
      1U, "new drop", "TX DROP",
      "inspect host read rate and TX ring", 1U },
    { SYSTEM_HEALTH_ISSUE_UART_DMA_STALL,
      SYSTEM_HEALTH_SOURCE_COMMUNICATIONS, SYSTEM_HEALTH_DEGRADED,
      1U, "new stall", "DMA STALL",
      "inspect UART DMA completion and recovery", 1U },
    { SYSTEM_HEALTH_ISSUE_TELEMETRY_DROP,
      SYSTEM_HEALTH_SOURCE_COMMUNICATIONS, SYSTEM_HEALTH_DEGRADED,
      1U, "new drop", "TLM DROP",
      "inspect telemetry queue and transport", 1U },
    { SYSTEM_HEALTH_ISSUE_TELEMETRY_STALE,
      SYSTEM_HEALTH_SOURCE_COMMUNICATIONS, SYSTEM_HEALTH_DEGRADED,
      SYSTEM_HEALTH_TELEMETRY_STALE_TICKS, "ticks", "TLM STALE",
      "inspect TelemetryTask scheduling", 1U },
    { SYSTEM_HEALTH_ISSUE_I2C_ERROR, SYSTEM_HEALTH_SOURCE_I2C,
      SYSTEM_HEALTH_DEGRADED, 1U, "new error", "I2C",
      "inspect wiring and timeout recovery", 1U },
    { SYSTEM_HEALTH_ISSUE_OLED_OFFLINE, SYSTEM_HEALTH_SOURCE_DISPLAY,
      SYSTEM_HEALTH_DEGRADED, 1U, "failed init", "OLED OFF",
      "continue headless and inspect OLED wiring", 1U },
    { SYSTEM_HEALTH_ISSUE_DISPLAY_STALE, SYSTEM_HEALTH_SOURCE_DISPLAY,
      SYSTEM_HEALTH_DEGRADED, SYSTEM_HEALTH_DISPLAY_STALE_TICKS, "ticks",
      "DSP STALE", "inspect DisplayTask scheduling", 1U },
    { SYSTEM_HEALTH_ISSUE_PARAMETER_ACK_DROP,
      SYSTEM_HEALTH_SOURCE_PARAMETERS, SYSTEM_HEALTH_DEGRADED,
      1U, "new drop", "ACK DROP",
      "retry transaction and inspect telemetry queue", 1U }
};

volatile system_health_snapshot_t g_system_health_snapshot;
volatile system_health_diagnostics_t g_system_health_diag;
volatile uint32_t g_system_health_debug_inject_mask;
volatile uint32_t g_system_health_debug_clear_recoverable_request;

static system_health_event_baseline_t s_event_baseline;
static TickType_t s_event_last_tick[SYSTEM_HEALTH_ISSUE_COUNT];
static uint8_t s_event_seen[SYSTEM_HEALTH_ISSUE_COUNT];

static uint32_t SystemHealth_I2cErrorCount(void)
{
    return g_bsp_i2c_diag.nack_count +
        g_bsp_i2c_diag.arbitration_lost_count +
        g_bsp_i2c_diag.bus_busy_timeout_count +
        g_bsp_i2c_diag.transfer_timeout_count +
        g_bsp_i2c_diag.mutex_timeout_count +
        g_bsp_i2c_diag.fifo_error_count;
}

static uint32_t SystemHealth_ParameterErrorCount(void)
{
    return g_parameter_service_diag.crc_error_count +
        g_parameter_service_diag.bad_length_count +
        g_parameter_service_diag.bad_type_count +
        g_parameter_service_diag.bad_value_count +
        g_parameter_service_diag.frame_timeout_count +
        g_parameter_service_diag.overflow_reset_count;
}

static uint32_t SystemHealth_TelemetryDropCount(void)
{
    return g_telemetry_diag.publish_dropped_count +
        g_telemetry_diag.ack_dropped_count +
        g_telemetry_diag.health_dropped_count +
        g_telemetry_diag.transport_dropped_count;
}

static uint32_t SystemHealth_SerialTxDropCount(void)
{
    return g_serial_tx_diag.write_dropped_count +
        g_serial_tx_diag.dma_start_fail_count;
}

static uint16_t SystemHealth_StackValue(
    configSTACK_DEPTH_TYPE value, configSTACK_DEPTH_TYPE allocated)
{
    if (value > allocated || value > (configSTACK_DEPTH_TYPE) UINT16_MAX) {
        return UINT16_MAX;
    }
    return (uint16_t) value;
}

static uint16_t SystemHealth_MinimumStackWords(
    const system_health_snapshot_t *snapshot)
{
    const uint16_t stacks[] = {
        snapshot->system_stack_free_words,
        snapshot->service_stack_free_words,
        snapshot->telemetry_stack_free_words,
        snapshot->display_stack_free_words,
        snapshot->idle_stack_free_words,
        snapshot->timer_stack_free_words
    };
    uint16_t minimum = UINT16_MAX;
    size_t index;

    for (index = 0U; index < (sizeof(stacks) / sizeof(stacks[0])); index++) {
        if (stacks[index] < minimum) {
            minimum = stacks[index];
        }
    }
    return minimum;
}

static void SystemHealth_AddIssue(
    uint32_t *mask, system_health_issue_t issue)
{
    if (issue > SYSTEM_HEALTH_ISSUE_NONE &&
        issue < SYSTEM_HEALTH_ISSUE_COUNT) {
        *mask |= SYSTEM_HEALTH_ISSUE_MASK(issue);
    }
}

static bool SystemHealth_HasNewEvent(uint32_t current, uint32_t previous)
{
    return (uint32_t) (current - previous) != 0U;
}

static void SystemHealth_RecordEvent(system_health_issue_t issue,
    uint32_t current, uint32_t previous, TickType_t now)
{
    if (SystemHealth_HasNewEvent(current, previous)) {
        s_event_seen[issue] = 1U;
        s_event_last_tick[issue] = now;
    }
}

static bool SystemHealth_EventIsActive(
    system_health_issue_t issue, TickType_t now)
{
    return (s_event_seen[issue] != 0U) &&
        ((TickType_t) (now - s_event_last_tick[issue]) <=
            SYSTEM_HEALTH_EVENT_HOLD_TICKS);
}

static system_health_issue_t SystemHealth_SelectIssue(
    uint32_t mask, bool fault_only)
{
    system_health_issue_t issue;

    for (issue = (system_health_issue_t) 1U;
         issue < SYSTEM_HEALTH_ISSUE_COUNT;
         issue = (system_health_issue_t) ((uint32_t) issue + 1U)) {
        const system_health_issue_descriptor_t *descriptor =
            SystemHealth_GetIssueDescriptor(issue);

        if ((mask & SYSTEM_HEALTH_ISSUE_MASK(issue)) == 0U) {
            continue;
        }
        if (!fault_only || descriptor->level == SYSTEM_HEALTH_FAULT) {
            return issue;
        }
    }
    return SYSTEM_HEALTH_ISSUE_NONE;
}

static system_health_level_t SystemHealth_LevelForMask(uint32_t mask)
{
    if (SystemHealth_SelectIssue(mask, true) != SYSTEM_HEALTH_ISSUE_NONE) {
        return SYSTEM_HEALTH_FAULT;
    }
    return (mask != 0U) ? SYSTEM_HEALTH_DEGRADED : SYSTEM_HEALTH_OK;
}

static uint32_t SystemHealth_TaskAge(
    TickType_t now, TickType_t last_wake_tick)
{
    if (last_wake_tick == 0U) {
        return UINT32_MAX;
    }
    return (uint32_t) ((TickType_t) (now - last_wake_tick));
}

static bool SystemHealth_TaskIsStale(TickType_t now,
    TickType_t last_wake_tick, TickType_t stale_ticks)
{
    if (last_wake_tick == 0U) {
        return now > stale_ticks;
    }
    return (TickType_t) (now - last_wake_tick) > stale_ticks;
}

static void SystemHealth_CaptureMetrics(system_health_snapshot_t *snapshot)
{
    TickType_t now = xTaskGetTickCount();

    snapshot->timestamp_us = BSP_Time_GetUs();
    snapshot->uptime_ticks = (uint32_t) now;
    snapshot->reset_cause_raw = g_bsp_reset_diag.raw_cause;
    snapshot->system_period_us = g_rtos_diag.system_last_period_us;
    snapshot->system_execution_us = g_rtos_diag.system_last_execution_us;
    snapshot->system_jitter_us = g_rtos_diag.system_last_jitter_us;
    snapshot->system_deadline_miss_count =
        g_rtos_diag.system_deadline_miss_count;
    snapshot->heap_free_bytes = (uint32_t) g_rtos_diag.heap_free_bytes;
    snapshot->heap_min_ever_free_bytes =
        (uint32_t) g_rtos_diag.heap_min_ever_free_bytes;

    snapshot->telemetry_publish_drop_count =
        g_telemetry_diag.publish_dropped_count +
        g_telemetry_diag.ack_dropped_count +
        g_telemetry_diag.health_dropped_count;
    snapshot->telemetry_transport_drop_count =
        g_telemetry_diag.transport_dropped_count;
    snapshot->serial_tx_drop_count = SystemHealth_SerialTxDropCount();
    snapshot->serial_rx_overflow_count = g_serial_rx_diag.overflow_count;
    snapshot->parameter_error_count = SystemHealth_ParameterErrorCount();
    snapshot->parameter_apply_sequence =
        g_control_tuning_params.update_sequence;
    snapshot->parameter_last_transaction_id =
        g_parameter_service_diag.last_transaction_id;
    snapshot->i2c_success_count = g_bsp_i2c_diag.write_success_count;
    snapshot->i2c_error_count = SystemHealth_I2cErrorCount();
    snapshot->display_refresh_count = g_ssd1306_diag.refresh_count;
    snapshot->quiet_acquired_count =
        g_serial_tx_diag.quiet_window_acquired_count;
    snapshot->quiet_released_count =
        g_serial_tx_diag.quiet_window_release_count;
    snapshot->max_quiet_window_us = g_serial_tx_diag.max_quiet_window_us;

    snapshot->source_updated_tick[SYSTEM_HEALTH_SOURCE_RTOS] =
        (uint32_t) g_rtos_diag.last_diagnostics_update_tick;
    snapshot->source_updated_tick[SYSTEM_HEALTH_SOURCE_CONTROL] =
        (uint32_t) g_rtos_diag.system_task_last_wake_tick;
    snapshot->source_updated_tick[SYSTEM_HEALTH_SOURCE_COMMUNICATIONS] =
        (uint32_t) g_telemetry_diag.last_task_wake_tick;
    snapshot->source_updated_tick[SYSTEM_HEALTH_SOURCE_PARAMETERS] =
        (uint32_t) g_rtos_diag.service_task_last_wake_tick;
    snapshot->source_updated_tick[SYSTEM_HEALTH_SOURCE_DISPLAY] =
        (uint32_t) g_rtos_diag.display_task_last_wake_tick;
    snapshot->source_updated_tick[SYSTEM_HEALTH_SOURCE_I2C] =
        (uint32_t) g_rtos_diag.display_task_last_wake_tick;

    snapshot->task_age_ticks[SYSTEM_HEALTH_TASK_SYSTEM] =
        SystemHealth_TaskAge(now, g_rtos_diag.system_task_last_wake_tick);
    snapshot->task_age_ticks[SYSTEM_HEALTH_TASK_SERVICE] =
        SystemHealth_TaskAge(now, g_rtos_diag.service_task_last_wake_tick);
    snapshot->task_age_ticks[SYSTEM_HEALTH_TASK_TELEMETRY] =
        SystemHealth_TaskAge(now, g_telemetry_diag.last_task_wake_tick);
    snapshot->task_age_ticks[SYSTEM_HEALTH_TASK_DISPLAY] =
        SystemHealth_TaskAge(now, g_rtos_diag.display_task_last_wake_tick);

    snapshot->system_stack_free_words = SystemHealth_StackValue(
        g_rtos_diag.system_stack_min_free_words,
        g_rtos_diag.system_stack_allocated_words);
    snapshot->service_stack_free_words = SystemHealth_StackValue(
        g_rtos_diag.service_stack_min_free_words,
        g_rtos_diag.service_stack_allocated_words);
    snapshot->telemetry_stack_free_words = SystemHealth_StackValue(
        g_rtos_diag.telemetry_stack_min_free_words,
        g_rtos_diag.telemetry_stack_allocated_words);
    snapshot->display_stack_free_words = SystemHealth_StackValue(
        g_rtos_diag.display_stack_min_free_words,
        g_rtos_diag.display_stack_allocated_words);
    snapshot->idle_stack_free_words = SystemHealth_StackValue(
        g_rtos_diag.idle_stack_min_free_words,
        g_rtos_diag.idle_stack_allocated_words);
    snapshot->timer_stack_free_words = SystemHealth_StackValue(
        g_rtos_diag.timer_stack_min_free_words,
        g_rtos_diag.timer_stack_allocated_words);
    snapshot->serial_ring_high_water_bytes =
        g_serial_tx_diag.ring_high_water_bytes;

    snapshot->oled_online = g_ssd1306_diag.online;
    snapshot->oled_address = g_ssd1306_diag.address;
    snapshot->parameter_pending = g_parameter_service_diag.pending;
    snapshot->parameter_last_status = g_parameter_service_diag.last_status;
    snapshot->quiet_window_active = g_serial_tx_diag.quiet_window_active;

    /* Phase 1F has no actuator path; these safety flags remain false. */
    snapshot->actuator_armed = 0U;
    snapshot->actuator_output_permitted = 0U;
    snapshot->reset_reason = g_bsp_reset_diag.reason;
    snapshot->reset_reason_valid = g_bsp_reset_diag.valid;
    snapshot->boot_count_valid = 0U;
}

static uint32_t SystemHealth_BuildActiveMask(
    const system_health_snapshot_t *snapshot)
{
    uint32_t mask = 0U;
    uint32_t telemetry_drop_count = SystemHealth_TelemetryDropCount();
    uint32_t serial_tx_drop_count = SystemHealth_SerialTxDropCount();
    uint32_t i2c_error_count = SystemHealth_I2cErrorCount();
    uint16_t minimum_stack_words = SystemHealth_MinimumStackWords(snapshot);
    TickType_t now = (TickType_t) snapshot->uptime_ticks;

    SystemHealth_RecordEvent(SYSTEM_HEALTH_ISSUE_CONTROL_DEADLINE,
        g_rtos_diag.system_deadline_miss_count,
        s_event_baseline.deadline_miss_count, now);
    SystemHealth_RecordEvent(SYSTEM_HEALTH_ISSUE_SERIAL_RX_OVERFLOW,
        g_serial_rx_diag.overflow_count,
        s_event_baseline.serial_rx_overflow_count, now);
    SystemHealth_RecordEvent(SYSTEM_HEALTH_ISSUE_SERIAL_TX_DROP,
        serial_tx_drop_count, s_event_baseline.serial_tx_drop_count, now);
    SystemHealth_RecordEvent(SYSTEM_HEALTH_ISSUE_UART_DMA_STALL,
        g_serial_tx_diag.dma_stall_count,
        s_event_baseline.uart_dma_stall_count, now);
    SystemHealth_RecordEvent(SYSTEM_HEALTH_ISSUE_TELEMETRY_DROP,
        telemetry_drop_count, s_event_baseline.telemetry_drop_count, now);
    SystemHealth_RecordEvent(SYSTEM_HEALTH_ISSUE_I2C_ERROR,
        i2c_error_count, s_event_baseline.i2c_error_count, now);
    SystemHealth_RecordEvent(SYSTEM_HEALTH_ISSUE_PARAMETER_ACK_DROP,
        g_parameter_service_diag.ack_publish_drop_count,
        s_event_baseline.parameter_ack_drop_count, now);

    if (g_rtos_diag.fault_code != (uint32_t) RTOS_FAULT_NONE) {
        SystemHealth_AddIssue(&mask, SYSTEM_HEALTH_ISSUE_RTOS_FATAL);
    }
    if (g_rtos_diag.scheduler_started != 0U &&
        SystemHealth_TaskIsStale(now,
            g_rtos_diag.system_task_last_wake_tick,
            SYSTEM_HEALTH_CONTROL_STALE_TICKS)) {
        SystemHealth_AddIssue(&mask, SYSTEM_HEALTH_ISSUE_CONTROL_STALE);
    }
    if (g_telemetry_diag.initialized != 0U &&
        SystemHealth_TaskIsStale(now, g_telemetry_diag.last_task_wake_tick,
            SYSTEM_HEALTH_TELEMETRY_STALE_TICKS)) {
        SystemHealth_AddIssue(&mask, SYSTEM_HEALTH_ISSUE_TELEMETRY_STALE);
    }
    if (g_rtos_diag.display_task_handle != NULL &&
        SystemHealth_TaskIsStale(now,
            g_rtos_diag.display_task_last_wake_tick,
            SYSTEM_HEALTH_DISPLAY_STALE_TICKS)) {
        SystemHealth_AddIssue(&mask, SYSTEM_HEALTH_ISSUE_DISPLAY_STALE);
    }
    if (g_rtos_diag.diagnostics_valid != 0U) {
        if (minimum_stack_words < SYSTEM_HEALTH_STACK_FAULT_WORDS) {
            SystemHealth_AddIssue(&mask,
                SYSTEM_HEALTH_ISSUE_STACK_CRITICAL);
        } else if (minimum_stack_words < SYSTEM_HEALTH_STACK_WARN_WORDS) {
            SystemHealth_AddIssue(&mask, SYSTEM_HEALTH_ISSUE_STACK_LOW);
        }
        if (snapshot->heap_min_ever_free_bytes <
            SYSTEM_HEALTH_HEAP_WARN_BYTES) {
            SystemHealth_AddIssue(&mask, SYSTEM_HEALTH_ISSUE_HEAP_LOW);
        }
    }
    if (g_ssd1306_diag.init_attempt_count != 0U &&
        g_ssd1306_diag.online == 0U) {
        SystemHealth_AddIssue(&mask, SYSTEM_HEALTH_ISSUE_OLED_OFFLINE);
    }

    if (SystemHealth_EventIsActive(
            SYSTEM_HEALTH_ISSUE_CONTROL_DEADLINE, now)) {
        SystemHealth_AddIssue(&mask, SYSTEM_HEALTH_ISSUE_CONTROL_DEADLINE);
    }
    if (SystemHealth_EventIsActive(
            SYSTEM_HEALTH_ISSUE_SERIAL_RX_OVERFLOW, now)) {
        SystemHealth_AddIssue(&mask,
            SYSTEM_HEALTH_ISSUE_SERIAL_RX_OVERFLOW);
    }
    if (SystemHealth_EventIsActive(
            SYSTEM_HEALTH_ISSUE_SERIAL_TX_DROP, now)) {
        SystemHealth_AddIssue(&mask, SYSTEM_HEALTH_ISSUE_SERIAL_TX_DROP);
    }
    if (SystemHealth_EventIsActive(
            SYSTEM_HEALTH_ISSUE_UART_DMA_STALL, now)) {
        SystemHealth_AddIssue(&mask, SYSTEM_HEALTH_ISSUE_UART_DMA_STALL);
    }
    if (SystemHealth_EventIsActive(
            SYSTEM_HEALTH_ISSUE_TELEMETRY_DROP, now)) {
        SystemHealth_AddIssue(&mask, SYSTEM_HEALTH_ISSUE_TELEMETRY_DROP);
    }
    if (SystemHealth_EventIsActive(SYSTEM_HEALTH_ISSUE_I2C_ERROR, now)) {
        SystemHealth_AddIssue(&mask, SYSTEM_HEALTH_ISSUE_I2C_ERROR);
    }
    if (SystemHealth_EventIsActive(
            SYSTEM_HEALTH_ISSUE_PARAMETER_ACK_DROP, now)) {
        SystemHealth_AddIssue(&mask,
            SYSTEM_HEALTH_ISSUE_PARAMETER_ACK_DROP);
    }

    mask |= g_system_health_debug_inject_mask &
        SYSTEM_HEALTH_VALID_ISSUE_MASK;
    return mask;
}

static void SystemHealth_UpdateEventBaseline(void)
{
    s_event_baseline.deadline_miss_count =
        g_rtos_diag.system_deadline_miss_count;
    s_event_baseline.serial_rx_overflow_count =
        g_serial_rx_diag.overflow_count;
    s_event_baseline.serial_tx_drop_count = SystemHealth_SerialTxDropCount();
    s_event_baseline.uart_dma_stall_count =
        g_serial_tx_diag.dma_stall_count;
    s_event_baseline.telemetry_drop_count =
        SystemHealth_TelemetryDropCount();
    s_event_baseline.i2c_error_count = SystemHealth_I2cErrorCount();
    s_event_baseline.parameter_ack_drop_count =
        g_parameter_service_diag.ack_publish_drop_count;
}

static void SystemHealth_UpdateLevels(system_health_snapshot_t *snapshot)
{
    system_health_issue_t issue;

    snapshot->source_level[SYSTEM_HEALTH_SOURCE_RTOS] =
        (g_rtos_diag.diagnostics_valid != 0U) ?
        SYSTEM_HEALTH_OK : SYSTEM_HEALTH_UNKNOWN;
    snapshot->source_level[SYSTEM_HEALTH_SOURCE_CONTROL] =
        (g_rtos_diag.scheduler_started != 0U) ?
        SYSTEM_HEALTH_OK : SYSTEM_HEALTH_UNKNOWN;
    snapshot->source_level[SYSTEM_HEALTH_SOURCE_COMMUNICATIONS] =
        ((g_serial_tx_diag.initialized != 0U) &&
         (g_serial_rx_diag.initialized != 0U) &&
         (g_telemetry_diag.initialized != 0U)) ?
        SYSTEM_HEALTH_OK : SYSTEM_HEALTH_UNKNOWN;
    snapshot->source_level[SYSTEM_HEALTH_SOURCE_PARAMETERS] =
        (g_parameter_service_diag.initialized != 0U) ?
        SYSTEM_HEALTH_OK : SYSTEM_HEALTH_UNKNOWN;
    snapshot->source_level[SYSTEM_HEALTH_SOURCE_DISPLAY] =
        (g_ssd1306_diag.init_attempt_count != 0U) ?
        SYSTEM_HEALTH_OK : SYSTEM_HEALTH_UNKNOWN;
    snapshot->source_level[SYSTEM_HEALTH_SOURCE_I2C] =
        (g_bsp_i2c_diag.initialized != 0U) ?
        SYSTEM_HEALTH_OK : SYSTEM_HEALTH_UNKNOWN;
    snapshot->source_level[SYSTEM_HEALTH_SOURCE_STORAGE] =
        SYSTEM_HEALTH_UNKNOWN;
    snapshot->source_level[SYSTEM_HEALTH_SOURCE_SENSOR] =
        SYSTEM_HEALTH_UNKNOWN;
    snapshot->source_level[SYSTEM_HEALTH_SOURCE_ACTUATOR] =
        SYSTEM_HEALTH_UNKNOWN;

    snapshot->task_level[SYSTEM_HEALTH_TASK_SYSTEM] =
        (g_rtos_diag.scheduler_started != 0U) ?
        SYSTEM_HEALTH_OK : SYSTEM_HEALTH_UNKNOWN;
    snapshot->task_level[SYSTEM_HEALTH_TASK_SERVICE] = SYSTEM_HEALTH_OK;
    snapshot->task_level[SYSTEM_HEALTH_TASK_TELEMETRY] =
        (g_telemetry_diag.initialized != 0U) ?
        SYSTEM_HEALTH_OK : SYSTEM_HEALTH_UNKNOWN;
    snapshot->task_level[SYSTEM_HEALTH_TASK_DISPLAY] =
        (g_rtos_diag.display_task_handle != NULL) ?
        SYSTEM_HEALTH_OK : SYSTEM_HEALTH_UNKNOWN;

    for (issue = (system_health_issue_t) 1U;
         issue < SYSTEM_HEALTH_ISSUE_COUNT;
         issue = (system_health_issue_t) ((uint32_t) issue + 1U)) {
        const system_health_issue_descriptor_t *descriptor;
        uint8_t *source_level;

        if ((snapshot->active_issue_mask &
             SYSTEM_HEALTH_ISSUE_MASK(issue)) == 0U) {
            continue;
        }
        descriptor = SystemHealth_GetIssueDescriptor(issue);
        source_level = &snapshot->source_level[descriptor->source];
        if ((uint8_t) descriptor->level > *source_level) {
            *source_level = (uint8_t) descriptor->level;
        }
    }

    if ((snapshot->active_issue_mask &
         SYSTEM_HEALTH_ISSUE_MASK(SYSTEM_HEALTH_ISSUE_CONTROL_STALE)) !=
        0U) {
        snapshot->task_level[SYSTEM_HEALTH_TASK_SYSTEM] =
            SYSTEM_HEALTH_FAULT;
    }
    if ((snapshot->active_issue_mask & SYSTEM_HEALTH_ISSUE_MASK(
         SYSTEM_HEALTH_ISSUE_TELEMETRY_STALE)) != 0U) {
        snapshot->task_level[SYSTEM_HEALTH_TASK_TELEMETRY] =
            SYSTEM_HEALTH_DEGRADED;
    }
    if ((snapshot->active_issue_mask & SYSTEM_HEALTH_ISSUE_MASK(
         SYSTEM_HEALTH_ISSUE_DISPLAY_STALE)) != 0U) {
        snapshot->task_level[SYSTEM_HEALTH_TASK_DISPLAY] =
            SYSTEM_HEALTH_DEGRADED;
    }
}

static uint32_t SystemHealth_ClearRecoveredIssues(
    uint32_t sticky_mask, uint32_t active_mask)
{
    system_health_issue_t issue;

    for (issue = (system_health_issue_t) 1U;
         issue < SYSTEM_HEALTH_ISSUE_COUNT;
         issue = (system_health_issue_t) ((uint32_t) issue + 1U)) {
        uint32_t bit = SYSTEM_HEALTH_ISSUE_MASK(issue);
        const system_health_issue_descriptor_t *descriptor =
            SystemHealth_GetIssueDescriptor(issue);

        if (descriptor->recoverable != 0U &&
            (active_mask & bit) == 0U) {
            sticky_mask &= ~bit;
        }
    }
    return sticky_mask;
}

void SystemHealth_Init(void)
{
    system_health_snapshot_t initial;

    memset(&initial, 0, sizeof(initial));
    memset((void *) &g_system_health_diag, 0,
        sizeof(g_system_health_diag));
    memset(&s_event_baseline, 0, sizeof(s_event_baseline));
    memset(s_event_last_tick, 0, sizeof(s_event_last_tick));
    memset(s_event_seen, 0, sizeof(s_event_seen));

    initial.magic = SYSTEM_HEALTH_MAGIC;
    initial.version = SYSTEM_HEALTH_VERSION;
    initial.size_bytes = (uint16_t) sizeof(initial);
    initial.build_phase = SYSTEM_HEALTH_BUILD_PHASE;
    initial.level = SYSTEM_HEALTH_UNKNOWN;
    taskENTER_CRITICAL();
    g_system_health_snapshot = initial;
    taskEXIT_CRITICAL();

    g_system_health_debug_inject_mask = 0U;
    g_system_health_debug_clear_recoverable_request = 0U;
    g_system_health_diag.initialized = 1U;
}

void SystemHealth_ServiceRefresh(void)
{
    /* ServiceTask is the sole caller; keep full work copies off its stack. */
    static system_health_snapshot_t snapshot;
    static system_health_snapshot_t previous;
    system_health_issue_t first_fault;
    uint32_t next_update_sequence;
    bool clear_recoverable;

    if (g_system_health_diag.initialized == 0U) {
        return;
    }

    taskENTER_CRITICAL();
    previous = g_system_health_snapshot;
    taskEXIT_CRITICAL();

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.magic = SYSTEM_HEALTH_MAGIC;
    snapshot.version = SYSTEM_HEALTH_VERSION;
    snapshot.size_bytes = (uint16_t) sizeof(snapshot);
    snapshot.build_phase = SYSTEM_HEALTH_BUILD_PHASE;
    SystemHealth_CaptureMetrics(&snapshot);
    snapshot.injected_issue_mask = g_system_health_debug_inject_mask &
        SYSTEM_HEALTH_VALID_ISSUE_MASK;
    snapshot.active_issue_mask = SystemHealth_BuildActiveMask(&snapshot);
    snapshot.sticky_issue_mask = previous.sticky_issue_mask |
        snapshot.active_issue_mask;

    clear_recoverable =
        g_system_health_debug_clear_recoverable_request != 0U;
    if (clear_recoverable) {
        g_system_health_debug_clear_recoverable_request = 0U;
        g_system_health_diag.clear_request_count++;
        snapshot.sticky_issue_mask = SystemHealth_ClearRecoveredIssues(
            snapshot.sticky_issue_mask, snapshot.active_issue_mask);
    }

    if (previous.first_fault_valid != 0U &&
        previous.first_fault_issue > (uint8_t) SYSTEM_HEALTH_ISSUE_NONE &&
        previous.first_fault_issue < (uint8_t) SYSTEM_HEALTH_ISSUE_COUNT) {
        snapshot.first_fault_valid = 1U;
        snapshot.first_fault_issue = previous.first_fault_issue;
        snapshot.first_fault_source = previous.first_fault_source;
    } else {
        first_fault = SystemHealth_SelectIssue(
            snapshot.sticky_issue_mask, true);
        if (first_fault != SYSTEM_HEALTH_ISSUE_NONE) {
            snapshot.first_fault_valid = 1U;
            snapshot.first_fault_issue = (uint8_t) first_fault;
            snapshot.first_fault_source = (uint8_t)
                SystemHealth_GetIssueDescriptor(first_fault)->source;
        }
    }

    snapshot.level = (uint8_t)
        SystemHealth_LevelForMask(snapshot.active_issue_mask);
    snapshot.active_issue = (uint8_t) SystemHealth_SelectIssue(
        snapshot.active_issue_mask, true);
    if (snapshot.active_issue == (uint8_t) SYSTEM_HEALTH_ISSUE_NONE) {
        snapshot.active_issue = (uint8_t) SystemHealth_SelectIssue(
            snapshot.active_issue_mask, false);
    }
    SystemHealth_UpdateLevels(&snapshot);
    if (snapshot.level == (uint8_t) SYSTEM_HEALTH_OK &&
        (snapshot.source_level[SYSTEM_HEALTH_SOURCE_RTOS] ==
             SYSTEM_HEALTH_UNKNOWN ||
         snapshot.source_level[SYSTEM_HEALTH_SOURCE_CONTROL] ==
             SYSTEM_HEALTH_UNKNOWN)) {
        snapshot.level = SYSTEM_HEALTH_UNKNOWN;
    }

    if (snapshot.injected_issue_mask != 0U) {
        g_system_health_diag.injection_refresh_count++;
    }
    if (g_system_health_diag.refresh_count == 0U) {
        g_system_health_diag.first_refresh_tick = snapshot.uptime_ticks;
    }
    g_system_health_diag.refresh_count++;
    g_system_health_diag.last_refresh_tick = snapshot.uptime_ticks;
    g_system_health_diag.last_active_issue_mask =
        snapshot.active_issue_mask;
    g_system_health_diag.last_sticky_issue_mask =
        snapshot.sticky_issue_mask;
    SystemHealth_UpdateEventBaseline();

    next_update_sequence = previous.update_sequence + 2U;
    taskENTER_CRITICAL();
    g_system_health_snapshot.update_sequence =
        previous.update_sequence + 1U;
    snapshot.update_sequence = previous.update_sequence + 1U;
    g_system_health_snapshot = snapshot;
    g_system_health_snapshot.update_sequence = next_update_sequence;
    taskEXIT_CRITICAL();
}

bool SystemHealth_GetSnapshot(system_health_snapshot_t *snapshot)
{
    if (snapshot == NULL || g_system_health_diag.initialized == 0U) {
        return false;
    }

    taskENTER_CRITICAL();
    *snapshot = g_system_health_snapshot;
    g_system_health_diag.snapshot_read_count++;
    taskEXIT_CRITICAL();
    return (snapshot->magic == SYSTEM_HEALTH_MAGIC) &&
        (snapshot->version == SYSTEM_HEALTH_VERSION) &&
        ((snapshot->update_sequence & 1U) == 0U);
}

void SystemHealth_RequestClearRecoverable(void)
{
    taskENTER_CRITICAL();
    g_system_health_debug_clear_recoverable_request = 1U;
    taskEXIT_CRITICAL();
}

const system_health_issue_descriptor_t *SystemHealth_GetIssueDescriptor(
    system_health_issue_t issue)
{
    if ((uint32_t) issue >=
        (sizeof(s_issue_descriptors) / sizeof(s_issue_descriptors[0]))) {
        return &s_issue_descriptors[0];
    }
    return &s_issue_descriptors[(uint32_t) issue];
}

const system_health_source_descriptor_t *SystemHealth_GetSourceDescriptor(
    system_health_source_t source)
{
    if ((uint32_t) source >=
        (sizeof(s_source_descriptors) / sizeof(s_source_descriptors[0]))) {
        return &s_source_descriptors[0];
    }
    return &s_source_descriptors[(uint32_t) source];
}

const char *SystemHealth_LevelName(system_health_level_t level)
{
    switch (level) {
        case SYSTEM_HEALTH_OK:
            return "OK";
        case SYSTEM_HEALTH_DEGRADED:
            return "DEGRADED";
        case SYSTEM_HEALTH_FAULT:
            return "FAULT";
        default:
            return "UNKNOWN";
    }
}
