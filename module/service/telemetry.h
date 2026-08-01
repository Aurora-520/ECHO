#ifndef ECHO_TELEMETRY_H
#define ECHO_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "attitude_estimator.h"
#include "bsp_reflectance.h"
#include "bsp_supply_voltage.h"
#include "esp_uart_link_test.h"
#include "ball_balance_service.h"
#include "imu_service.h"
#include "motor_profile.h"
#include "system_health.h"
#include "task.h"
#include "tfmini_s.h"
#include "zdt_protocol.h"

#define TELEMETRY_PROTOCOL_VERSION        1U
#define TELEMETRY_FRAME_TYPE_CONTROL      1U
#define TELEMETRY_FRAME_TYPE_PARAMETER_SET 2U
#define TELEMETRY_FRAME_TYPE_PARAMETER_ACK 3U
#define TELEMETRY_FRAME_TYPE_HEALTH       4U
#define TELEMETRY_FRAME_TYPE_ACTUATOR_COMMAND 5U
#define TELEMETRY_FRAME_TYPE_ACTUATOR_ACK 6U
#define TELEMETRY_FRAME_TYPE_MOTOR_PROFILE 7U
#define TELEMETRY_FRAME_TYPE_REFLECTANCE  8U
#define TELEMETRY_FRAME_TYPE_SUPPLY_VOLTAGE 9U
#define TELEMETRY_FRAME_TYPE_TFMINI       10U
#define TELEMETRY_FRAME_TYPE_IMU           11U
#define TELEMETRY_FRAME_TYPE_ESP_LINK      12U
#define TELEMETRY_FRAME_TYPE_ATTITUDE      13U
#define TELEMETRY_FRAME_TYPE_ZDT_COMMAND   14U
#define TELEMETRY_FRAME_TYPE_ZDT_ACK       15U
#define TELEMETRY_FRAME_TYPE_BALL_BALANCE  16U
#define TELEMETRY_CONTROL_PAYLOAD_BYTES   96U
#define TELEMETRY_CONTROL_FRAME_BYTES     112U
#define TELEMETRY_PARAMETER_ACK_PAYLOAD_BYTES 16U
#define TELEMETRY_PARAMETER_ACK_FRAME_BYTES   32U
#define TELEMETRY_ACTUATOR_ACK_PAYLOAD_BYTES 16U
#define TELEMETRY_ACTUATOR_ACK_FRAME_BYTES   32U
#define TELEMETRY_MOTOR_PROFILE_PAYLOAD_BYTES 36U
#define TELEMETRY_MOTOR_PROFILE_FRAME_BYTES   52U
#define TELEMETRY_REFLECTANCE_PAYLOAD_BYTES   36U
#define TELEMETRY_REFLECTANCE_FRAME_BYTES     52U
#define TELEMETRY_SUPPLY_VOLTAGE_PAYLOAD_BYTES 24U
#define TELEMETRY_SUPPLY_VOLTAGE_FRAME_BYTES   40U
#define TELEMETRY_TFMINI_PAYLOAD_BYTES     64U
#define TELEMETRY_TFMINI_FRAME_BYTES       80U
#define TELEMETRY_IMU_PAYLOAD_BYTES        88U
#define TELEMETRY_IMU_FRAME_BYTES         104U
#define TELEMETRY_ESP_LINK_PAYLOAD_BYTES   96U
#define TELEMETRY_ESP_LINK_FRAME_BYTES     112U
#define TELEMETRY_ATTITUDE_PAYLOAD_BYTES    64U
#define TELEMETRY_ATTITUDE_FRAME_BYTES      80U
#define TELEMETRY_ZDT_AXIS_SNAPSHOT_BYTES     40U
#define TELEMETRY_ZDT_ACK_PAYLOAD_BYTES      156U
#define TELEMETRY_ZDT_ACK_FRAME_BYTES        172U
#define TELEMETRY_BALL_BALANCE_PAYLOAD_BYTES 72U
#define TELEMETRY_BALL_BALANCE_FRAME_BYTES   88U
#define TELEMETRY_HEALTH_PAYLOAD_BYTES    116U
#define TELEMETRY_HEALTH_FRAME_BYTES      132U
#define TELEMETRY_MAX_FRAME_BYTES         TELEMETRY_ZDT_ACK_FRAME_BYTES
#define TELEMETRY_TASK_STACK_WORDS ((configSTACK_DEPTH_TYPE) 256U)

#define TELEMETRY_CONTROL_FLAG_TEST_SIGNAL (1UL << 0)
#define TELEMETRY_CONTROL_FLAG_LEFT_ENCODER_RAW (1UL << 1)
#define TELEMETRY_CONTROL_FLAG_RIGHT_ENCODER_RAW_X1 (1UL << 2)
#define TELEMETRY_CONTROL_FLAG_MOTOR_LEFT_ACTIVE (1UL << 3)
#define TELEMETRY_CONTROL_FLAG_MOTOR_RIGHT_ACTIVE (1UL << 4)
#define TELEMETRY_CONTROL_FLAG_SPEED_CLOSED_LOOP (1UL << 5)
#define TELEMETRY_CONTROL_FLAG_SPEED_BOOST       (1UL << 6)
#define TELEMETRY_CONTROL_FLAG_SPEED_TRACKING    (1UL << 7)
#define TELEMETRY_CONTROL_FLAG_HEADING_CLOSED_LOOP (1UL << 8)
#define TELEMETRY_CONTROL_FLAG_DISTANCE_CLOSED_LOOP (1UL << 9)
#define TELEMETRY_CONTROL_FLAG_H_MISSION_SHIFT      10U
#define TELEMETRY_CONTROL_FLAG_H_MISSION_MASK       (0x07UL << 10)
#define TELEMETRY_CONTROL_FLAG_H_LINE_VALID         (1UL << 13)
#define TELEMETRY_CONTROL_FLAG_H_FINISH_ARMED       (1UL << 14)
#define TELEMETRY_CONTROL_FLAG_H_FINISH_DETECTED    (1UL << 15)
#define TELEMETRY_CONTROL_FLAG_H_TERMINAL_SHIFT     16U
#define TELEMETRY_CONTROL_FLAG_H_TERMINAL_MASK      (0x03UL << 16)
#define TELEMETRY_CONTROL_FLAG_H_LINE_LOST_SHIFT    18U
#define TELEMETRY_CONTROL_FLAG_H_LINE_LOST_MASK     (0x1FUL << 18)
#define TELEMETRY_CONTROL_FLAG_H_FINISH_EVID_SHIFT  23U
#define TELEMETRY_CONTROL_FLAG_H_FINISH_EVID_MASK   (0x07UL << 23)
#define TELEMETRY_CONTROL_FLAG_H_START_CLEARED      (1UL << 26)

/* The first 44 bytes retain the legacy speed mapping. New fields are
 * append-only so 40-byte and 44-byte host decoders remain usable. */

typedef struct {
    float setpoint;
    float measurement;
    float control_output;
    float auxiliary;
    float right_auxiliary;
    float right_setpoint;
    float left_pid_proportional;
    float left_pid_integrator;
    float left_pid_derivative;
    float left_pid_feedforward;
    float right_pid_proportional;
    float right_pid_integrator;
    float right_pid_derivative;
    float right_pid_feedforward;
    float active_kp;
    float active_ki;
    float active_kd;
    uint32_t parameter_apply_sequence;
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
    tfmini_s_snapshot_t snapshot;
    uint32_t uart_rx_overflow_count;
    uint8_t query_attempt_count;
    uint8_t reserved[3];
} telemetry_tfmini_sample_t;

typedef struct {
    uint32_t tx_command_count;
    uint32_t tx_query_count;
    uint32_t invalid_response_count;
    uint32_t position_reached_count;
    uint32_t speed_lease_expired_count;
    int32_t position_counts;
    int32_t position_millidegrees;
    int16_t speed_rpm;
    uint16_t firmware_version;
    uint16_t hardware_version;
    uint8_t motor_status_flags;
    uint8_t last_function;
    uint8_t last_reply_status;
    uint8_t stalled;
    uint8_t stall_protected;
    uint8_t reserved;
} telemetry_zdt_axis_snapshot_t;

typedef struct {
    uint32_t sequence;
    int32_t value;
    uint32_t gen1_response_count;
    uint32_t gen2_response_count;
    uint16_t gen1_timeout_count;
    uint16_t gen2_timeout_count;
    uint8_t operation;
    uint8_t axis;
    uint8_t status;
    uint8_t flags;
    telemetry_zdt_axis_snapshot_t axis_snapshot[2];
    uint16_t gen2_bus_voltage_mv;
    uint16_t gen2_phase_current_ma;
    uint16_t gen2_encoder_linear;
    int32_t gen2_target_position_counts;
    int32_t gen2_position_error_counts;
    uint8_t gen2_homing_status_flags;
    uint8_t gen2_extended_valid_flags;
    zdt_protocol_driver_config_t gen2_driver_config;
    uint32_t pcb_battery_mv;
    uint16_t pcb_battery_filtered_raw;
    uint8_t pcb_battery_valid;
    uint8_t reserved;
} telemetry_zdt_ack_t;

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
    uint32_t zdt_ack_attempt_count;
    uint32_t zdt_ack_accepted_count;
    uint32_t zdt_ack_dropped_count;
    uint32_t motor_profile_attempt_count;
    uint32_t motor_profile_accepted_count;
    uint32_t motor_profile_dropped_count;
    uint32_t reflectance_attempt_count;
    uint32_t reflectance_accepted_count;
    uint32_t reflectance_dropped_count;
    uint32_t supply_voltage_attempt_count;
    uint32_t supply_voltage_accepted_count;
    uint32_t supply_voltage_dropped_count;
    uint32_t tfmini_attempt_count;
    uint32_t tfmini_accepted_count;
    uint32_t tfmini_dropped_count;
    uint32_t imu_attempt_count;
    uint32_t imu_accepted_count;
    uint32_t imu_dropped_count;
    uint32_t esp_link_attempt_count;
    uint32_t esp_link_accepted_count;
    uint32_t esp_link_dropped_count;
    uint32_t attitude_attempt_count;
    uint32_t attitude_accepted_count;
    uint32_t attitude_dropped_count;
    uint32_t ball_balance_attempt_count;
    uint32_t ball_balance_accepted_count;
    uint32_t ball_balance_dropped_count;
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
bool Telemetry_PublishZdtAck(const telemetry_zdt_ack_t *ack);
bool Telemetry_PublishMotorProfile(const motor_profile_t *profile);
bool Telemetry_PublishReflectance(
    const bsp_reflectance_sample_t *sample);
bool Telemetry_PublishSupplyVoltage(
    const bsp_supply_voltage_sample_t *sample);
bool Telemetry_PublishTfmini(
    const telemetry_tfmini_sample_t *sample);
bool Telemetry_PublishImu(const imu_service_snapshot_t *snapshot);
bool Telemetry_PublishEspLink(
    const esp_uart_link_test_snapshot_t *snapshot);
bool Telemetry_PublishAttitude(
    const attitude_estimator_snapshot_t *snapshot);
bool Telemetry_PublishBallBalance(
    const ball_balance_snapshot_t *snapshot);
bool Telemetry_PublishHealth(const system_health_snapshot_t *snapshot);

#endif
