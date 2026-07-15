#ifndef ECHO_SYSTEM_HEALTH_H
#define ECHO_SYSTEM_HEALTH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SYSTEM_HEALTH_MAGIC       0x484C5448UL
#define SYSTEM_HEALTH_VERSION     1U
#define SYSTEM_HEALTH_BUILD_PHASE 0x010FU
#define SYSTEM_HEALTH_BUILD_NAME  "P1F"

typedef enum {
    SYSTEM_HEALTH_UNKNOWN = 0U,
    SYSTEM_HEALTH_OK = 1U,
    SYSTEM_HEALTH_DEGRADED = 2U,
    SYSTEM_HEALTH_FAULT = 3U
} system_health_level_t;

typedef enum {
    SYSTEM_HEALTH_SOURCE_RTOS = 0U,
    SYSTEM_HEALTH_SOURCE_CONTROL,
    SYSTEM_HEALTH_SOURCE_COMMUNICATIONS,
    SYSTEM_HEALTH_SOURCE_PARAMETERS,
    SYSTEM_HEALTH_SOURCE_DISPLAY,
    SYSTEM_HEALTH_SOURCE_I2C,
    SYSTEM_HEALTH_SOURCE_STORAGE,
    SYSTEM_HEALTH_SOURCE_SENSOR,
    SYSTEM_HEALTH_SOURCE_ACTUATOR,
    SYSTEM_HEALTH_SOURCE_COUNT
} system_health_source_t;

typedef enum {
    SYSTEM_HEALTH_TASK_SYSTEM = 0U,
    SYSTEM_HEALTH_TASK_SERVICE,
    SYSTEM_HEALTH_TASK_TELEMETRY,
    SYSTEM_HEALTH_TASK_DISPLAY,
    SYSTEM_HEALTH_TASK_COUNT
} system_health_task_t;

typedef enum {
    SYSTEM_HEALTH_ISSUE_NONE = 0U,
    SYSTEM_HEALTH_ISSUE_RTOS_FATAL = 1U,
    SYSTEM_HEALTH_ISSUE_CONTROL_DEADLINE = 2U,
    SYSTEM_HEALTH_ISSUE_CONTROL_STALE = 3U,
    SYSTEM_HEALTH_ISSUE_STACK_LOW = 4U,
    SYSTEM_HEALTH_ISSUE_STACK_CRITICAL = 5U,
    SYSTEM_HEALTH_ISSUE_HEAP_LOW = 6U,
    SYSTEM_HEALTH_ISSUE_SERIAL_RX_OVERFLOW = 7U,
    SYSTEM_HEALTH_ISSUE_SERIAL_TX_DROP = 8U,
    SYSTEM_HEALTH_ISSUE_UART_DMA_STALL = 9U,
    SYSTEM_HEALTH_ISSUE_TELEMETRY_DROP = 10U,
    SYSTEM_HEALTH_ISSUE_TELEMETRY_STALE = 11U,
    SYSTEM_HEALTH_ISSUE_I2C_ERROR = 12U,
    SYSTEM_HEALTH_ISSUE_OLED_OFFLINE = 13U,
    SYSTEM_HEALTH_ISSUE_DISPLAY_STALE = 14U,
    SYSTEM_HEALTH_ISSUE_PARAMETER_ACK_DROP = 15U,
    SYSTEM_HEALTH_ISSUE_COUNT
} system_health_issue_t;

#define SYSTEM_HEALTH_ISSUE_MASK(issue) \
    (1UL << ((uint32_t) (issue) - 1UL))

typedef struct {
    system_health_source_t source;
    const char *name;
    const char *owner;
} system_health_source_descriptor_t;

typedef struct {
    system_health_issue_t issue;
    system_health_source_t source;
    system_health_level_t level;
    uint32_t threshold;
    const char *threshold_units;
    const char *short_name;
    const char *action;
    uint8_t recoverable;
} system_health_issue_descriptor_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size_bytes;
    uint16_t build_phase;
    uint16_t reserved_identity;
    uint32_t update_sequence;
    uint32_t timestamp_us;
    uint32_t uptime_ticks;
    uint32_t reset_cause_raw;
    uint32_t active_issue_mask;
    uint32_t sticky_issue_mask;
    uint32_t injected_issue_mask;

    uint32_t system_period_us;
    uint32_t system_execution_us;
    uint32_t system_jitter_us;
    uint32_t system_deadline_miss_count;
    uint32_t heap_free_bytes;
    uint32_t heap_min_ever_free_bytes;

    uint32_t telemetry_publish_drop_count;
    uint32_t telemetry_transport_drop_count;
    uint32_t serial_tx_drop_count;
    uint32_t serial_rx_overflow_count;
    uint32_t parameter_error_count;
    uint32_t parameter_apply_sequence;
    uint32_t parameter_last_transaction_id;
    uint32_t i2c_success_count;
    uint32_t i2c_error_count;
    uint32_t display_refresh_count;
    uint32_t quiet_acquired_count;
    uint32_t quiet_released_count;
    uint32_t max_quiet_window_us;

    uint32_t source_updated_tick[SYSTEM_HEALTH_SOURCE_COUNT];
    uint32_t task_age_ticks[SYSTEM_HEALTH_TASK_COUNT];

    uint16_t system_stack_free_words;
    uint16_t service_stack_free_words;
    uint16_t telemetry_stack_free_words;
    uint16_t display_stack_free_words;
    uint16_t idle_stack_free_words;
    uint16_t timer_stack_free_words;
    uint16_t serial_ring_high_water_bytes;

    uint8_t level;
    uint8_t active_issue;
    uint8_t first_fault_issue;
    uint8_t first_fault_source;
    uint8_t first_fault_valid;
    uint8_t oled_online;
    uint8_t oled_address;
    uint8_t parameter_pending;
    uint8_t parameter_last_status;
    uint8_t quiet_window_active;
    uint8_t actuator_armed;
    uint8_t actuator_output_permitted;
    uint8_t reset_reason;
    uint8_t reset_reason_valid;
    uint8_t boot_count_valid;
    uint8_t source_level[SYSTEM_HEALTH_SOURCE_COUNT];
    uint8_t task_level[SYSTEM_HEALTH_TASK_COUNT];
} system_health_snapshot_t;

typedef struct {
    uint32_t refresh_count;
    uint32_t snapshot_read_count;
    uint32_t clear_request_count;
    uint32_t injection_refresh_count;
    uint32_t first_refresh_tick;
    uint32_t last_refresh_tick;
    uint32_t last_active_issue_mask;
    uint32_t last_sticky_issue_mask;
    uint8_t initialized;
    uint8_t reserved[3];
} system_health_diagnostics_t;

/* Watch/debug readers must treat this as read-only. */
extern volatile system_health_snapshot_t g_system_health_snapshot;
extern volatile system_health_diagnostics_t g_system_health_diag;

/* Debugger-only validation controls, consumed by ServiceTask refreshes. */
extern volatile uint32_t g_system_health_debug_inject_mask;
extern volatile uint32_t g_system_health_debug_clear_recoverable_request;

void SystemHealth_Init(void);
void SystemHealth_ServiceRefresh(void);
bool SystemHealth_GetSnapshot(system_health_snapshot_t *snapshot);
void SystemHealth_RequestClearRecoverable(void);
const system_health_issue_descriptor_t *SystemHealth_GetIssueDescriptor(
    system_health_issue_t issue);
const system_health_source_descriptor_t *SystemHealth_GetSourceDescriptor(
    system_health_source_t source);
const char *SystemHealth_LevelName(system_health_level_t level);

#endif
