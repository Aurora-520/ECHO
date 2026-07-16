#ifndef ECHO_CHASSIS_ACTUATOR_H
#define ECHO_CHASSIS_ACTUATOR_H

#include <stdbool.h>
#include <stdint.h>

#define CHASSIS_ACTUATOR_DEBUG_MAGIC         0x4543484FUL
#define CHASSIS_ACTUATOR_DEBUG_MAGIC_INVERSE 0xBABCB7B0UL
#define CHASSIS_ACTUATOR_TEST_MAX_PERMILLE   500
#define CHASSIS_ACTUATOR_TEST_MAX_DURATION_MS 1000U
#define CHASSIS_ACTUATOR_SPEED_MAX_DURATION_MS 30000U

typedef enum {
    CHASSIS_ACTUATOR_MODE_ELECTRICAL = 0U,
    CHASSIS_ACTUATOR_MODE_SPEED = 1U
} chassis_actuator_mode_t;

typedef enum {
    CHASSIS_SPEED_PHASE_IDLE = 0U,
    CHASSIS_SPEED_PHASE_BOOST,
    CHASSIS_SPEED_PHASE_TRACKING
} chassis_speed_phase_t;

typedef enum {
    CHASSIS_ACTUATOR_STOP_NONE = 0U,
    CHASSIS_ACTUATOR_STOP_COMPLETE,
    CHASSIS_ACTUATOR_STOP_TIMING,
    CHASSIS_ACTUATOR_STOP_REJECTED,
    CHASSIS_ACTUATOR_STOP_EMERGENCY,
    CHASSIS_ACTUATOR_STOP_STALL
} chassis_actuator_stop_reason_t;

typedef enum {
    CHASSIS_ACTUATOR_COMMAND_STAGED = 0U,
    CHASSIS_ACTUATOR_COMMAND_BAD_MAGIC,
    CHASSIS_ACTUATOR_COMMAND_BAD_VALUE,
    CHASSIS_ACTUATOR_COMMAND_BUSY,
    CHASSIS_ACTUATOR_COMMAND_DUPLICATE,
    CHASSIS_ACTUATOR_COMMAND_PROFILE_LOCKED
} chassis_actuator_command_status_t;

typedef struct {
    uint32_t magic;
    uint32_t magic_inverse;
    uint32_t sequence;
    int16_t left_electrical_permille;
    int16_t right_electrical_permille;
    uint16_t duration_ms;
    uint16_t reserved;
} chassis_actuator_debug_request_t;

typedef struct {
    uint32_t accepted_request_count;
    uint32_t speed_retarget_count;
    uint32_t rejected_request_count;
    uint32_t completed_pulse_count;
    uint32_t timing_stop_count;
    uint32_t emergency_stop_count;
    uint32_t speed_update_count;
    uint32_t boost_complete_count;
    uint32_t recovery_boost_count;
    uint32_t recovery_failed_count;
    uint32_t stall_stop_count;
    uint32_t last_request_sequence;
    uint16_t remaining_control_cycles;
    uint16_t boost_elapsed_cycles;
    int16_t applied_left_permille;
    int16_t applied_right_permille;
    int16_t normalized_left_permille;
    int16_t normalized_right_permille;
    int16_t left_target_deci_rpm;
    int16_t right_target_deci_rpm;
    float left_measured_rpm;
    float right_measured_rpm;
    float left_filtered_rpm;
    float right_filtered_rpm;
    uint8_t last_stop_reason;
    uint8_t control_mode;
    uint8_t speed_phase;
    uint8_t initialized;
    uint8_t armed;
    uint8_t output_permitted;
} chassis_actuator_diagnostics_t;

/* Watch/debug readers must treat this as read-only. */
extern volatile chassis_actuator_diagnostics_t g_chassis_actuator_diag;

void ChassisActuator_Init(void);
chassis_actuator_command_status_t ChassisActuator_StageDebugRequest(
    const chassis_actuator_debug_request_t *request);
void ChassisActuator_ServiceAtControlBoundary(
    bool schedule_resynchronized, float left_measured_rpm,
    float right_measured_rpm, uint32_t period_us);
void ChassisActuator_ForceSafe(chassis_actuator_stop_reason_t reason);

#endif
