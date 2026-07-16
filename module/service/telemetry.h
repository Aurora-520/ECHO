#ifndef ECHO_TELEMETRY_H
#define ECHO_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "motor_profile.h"
#include "system_health.h"
#include "task.h"

#define TELEMETRY_PROTOCOL_VERSION        1U
#define TELEMETRY_FRAME_TYPE_CONTROL      1U
#define TELEMETRY_FRAME_TYPE_PARAMETER_SET 2U
#define TELEMETRY_FRAME_TYPE_PARAMETER_ACK 3U
#define TELEMETRY_FRAME_TYPE_HEALTH       4U
#define TELEMETRY_FRAME_TYPE_ACTUATOR_COMMAND 5U
#define TELEMETRY_FRAME_TYPE_ACTUATOR_ACK 6U
#define TELEMETRY_FRAME_TYPE_MOTOR_PROFILE 7U
#define TELEMETRY_CONTROL_PAYLOAD_BYTES   40U
#define TELEMETRY_CONTROL_FRAME_BYTES     56U
#define TELEMETRY_PARAMETER_ACK_PAYLOAD_BYTES 16U
#define TELEMETRY_PARAMETER_ACK_FRAME_BYTES   32U
#define TELEMETRY_ACTUATOR_ACK_PAYLOAD_BYTES 16U
#define TELEMETRY_ACTUATOR_ACK_FRAME_BYTES   32U
#define TELEMETRY_MOTOR_PROFILE_PAYLOAD_BYTES 36U
#define TELEMETRY_MOTOR_PROFILE_FRAME_BYTES   52U
#define TELEMETRY_HEALTH_PAYLOAD_BYTES    116U
#define TELEMETRY_HEALTH_FRAME_BYTES      132U
#define TELEMETRY_MAX_FRAME_BYTES         TELEMETRY_HEALTH_FRAME_BYTES
#define TELEMETRY_TASK_STACK_WORDS ((configSTACK_DEPTH_TYPE) 256U)

#define TELEMETRY_CONTROL_FLAG_TEST_SIGNAL (1UL << 0)
#define TELEMETRY_CONTROL_FLAG_LEFT_ENCODER_RAW (1UL << 1)
#define TELEMETRY_CONTROL_FLAG_RIGHT_ENCODER_RAW_X1 (1UL << 2)
#define TELEMETRY_CONTROL_FLAG_MOTOR_LEFT_ACTIVE (1UL << 3)
#define TELEMETRY_CONTROL_FLAG_MOTOR_RIGHT_ACTIVE (1UL << 4)
#define TELEMETRY_CONTROL_FLAG_SPEED_CLOSED_LOOP (1UL << 5)
#define TELEMETRY_CONTROL_FLAG_SPEED_BOOST       (1UL << 6)
#define TELEMETRY_CONTROL_FLAG_SPEED_TRACKING    (1UL << 7)

/* Actuator/encoder diagnostic mapping: setpoint=active electrical permille,
 * measurement=left x4 delta,
 * control_output=right x1 delta, auxiliary=right x4-equivalent delta. */

typedef struct {
    float setpoint;
    float measurement;
    float control_output;
    float auxiliary;
    uint32_t loop_count;
    uint32_t period_us;
    uint32_t execution_us;
    uint32_t jitter_us;
    uint32_t deadline_miss_count;
    uint32_t flags;
} telemetry_control_sample_t;

typedef struct {
    uint32_t transaction_id;
    uint16_t parameter_id;
    uint8_t status;
    uint8_t reserved;
    float applied_value;
    uint32_t apply_sequence;
} telemetry_parameter_ack_t;

typedef struct {
    uint32_t sequence;
    int16_t left_electrical_permille;
    int16_t right_electrical_permille;
    uint16_t duration_ms;
    uint8_t status;
    uint8_t reserved;
    uint32_t accepted_request_count;
} telemetry_actuator_ack_t;

typedef struct {
    uint32_t publish_attempt_count;
    uint32_t publish_accepted_count;
    uint32_t publish_dropped_count;
    uint32_t ack_attempt_count;
    uint32_t ack_accepted_count;
    uint32_t ack_dropped_count;
    uint32_t actuator_ack_attempt_count;
    uint32_t actuator_ack_accepted_count;
    uint32_t actuator_ack_dropped_count;
    uint32_t motor_profile_attempt_count;
    uint32_t motor_profile_accepted_count;
    uint32_t motor_profile_dropped_count;
    uint32_t health_attempt_count;
    uint32_t health_accepted_count;
    uint32_t health_dropped_count;
    uint32_t task_run_count;
    uint32_t frames_encoded_count;
    uint32_t frames_queued_count;
    uint32_t transport_dropped_count;
    uint32_t queue_high_water;
    uint32_t last_sequence;
    uint32_t last_timestamp_us;
    TickType_t last_task_wake_tick;
    uint8_t last_frame_type;
    uint16_t last_crc;
    uint16_t last_frame_length;
    TaskHandle_t task_handle;
    uint8_t initialized;
    uint8_t reserved[3];
} telemetry_diagnostics_t;

extern volatile telemetry_diagnostics_t g_telemetry_diag;

TaskHandle_t Telemetry_CreateTask(UBaseType_t priority);

/*
 * Copies one control sample into a static queue without blocking. A false
 * result means the complete sample was dropped.
 */
bool Telemetry_PublishControl(const telemetry_control_sample_t *sample);
bool Telemetry_PublishParameterAck(
    const telemetry_parameter_ack_t *ack);
bool Telemetry_PublishActuatorAck(const telemetry_actuator_ack_t *ack);
bool Telemetry_PublishMotorProfile(const motor_profile_t *profile);
bool Telemetry_PublishHealth(const system_health_snapshot_t *snapshot);

#endif
