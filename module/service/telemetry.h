#ifndef ECHO_TELEMETRY_H
#define ECHO_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#define TELEMETRY_PROTOCOL_VERSION        1U
#define TELEMETRY_FRAME_TYPE_CONTROL      1U
#define TELEMETRY_FRAME_TYPE_PARAMETER_ACK 3U
#define TELEMETRY_CONTROL_PAYLOAD_BYTES   40U
#define TELEMETRY_CONTROL_FRAME_BYTES     56U
#define TELEMETRY_PARAMETER_ACK_PAYLOAD_BYTES 16U
#define TELEMETRY_PARAMETER_ACK_FRAME_BYTES   32U
#define TELEMETRY_TASK_STACK_WORDS ((configSTACK_DEPTH_TYPE) 256U)

#define TELEMETRY_CONTROL_FLAG_TEST_SIGNAL (1UL << 0)

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
    uint32_t publish_attempt_count;
    uint32_t publish_accepted_count;
    uint32_t publish_dropped_count;
    uint32_t ack_attempt_count;
    uint32_t ack_accepted_count;
    uint32_t ack_dropped_count;
    uint32_t task_run_count;
    uint32_t frames_encoded_count;
    uint32_t frames_queued_count;
    uint32_t transport_dropped_count;
    uint32_t queue_high_water;
    uint32_t last_sequence;
    uint32_t last_timestamp_us;
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

#endif
