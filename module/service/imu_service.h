#ifndef ECHO_IMU_SERVICE_H
#define ECHO_IMU_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"

#define IMU_SERVICE_SNAPSHOT_VERSION 1U
#define IMU_SERVICE_SAMPLE_RATE_HZ   100U

typedef enum {
    IMU_SERVICE_STATE_PROBE = 0U,
    IMU_SERVICE_STATE_RESET_WAIT,
    IMU_SERVICE_STATE_SETTLING,
    IMU_SERVICE_STATE_CALIBRATING,
    IMU_SERVICE_STATE_READY
} imu_service_state_t;

typedef struct {
    uint16_t version;
    uint16_t size_bytes;
    uint32_t update_sequence;
    uint32_t timestamp_us;
    uint32_t sample_count;
    float accel_g[3];
    float accel_norm_g;
    float gyro_raw_dps[3];
    float gyro_dps[3];
    float gyro_filtered_dps[3];
    float gyro_bias_dps[3];
    float temperature_c;
    float calibration_temperature_c;
    uint16_t calibration_samples;
    uint16_t calibration_target_samples;
    uint8_t state;
    uint8_t address;
    uint8_t who_am_i;
    uint8_t online;
    uint8_t calibrated;
    uint8_t valid;
    uint8_t profile_version;
    uint8_t reserved;
} imu_service_snapshot_t;

typedef struct {
    uint32_t process_count;
    uint32_t state_transition_count;
    uint32_t sample_success_count;
    uint32_t sample_failure_count;
    uint32_t forced_sample_failure_count;
    uint32_t consecutive_sample_failures;
    uint32_t reinitialize_count;
    uint32_t calibration_restart_count;
    uint32_t calibration_complete_count;
    uint32_t last_sample_tick;
    uint32_t last_sample_timestamp_us;
    uint8_t initialized;
    uint8_t state;
    uint8_t online;
    uint8_t ready;
} imu_service_diagnostics_t;

extern volatile imu_service_snapshot_t g_imu_service_snapshot;
extern volatile imu_service_diagnostics_t g_imu_service_diag;
extern volatile uint32_t g_imu_service_debug_forced_sample_failures;

void ImuService_Init(void);
void ImuService_Process(TickType_t now);
bool ImuService_NeedsBusAccess(TickType_t now);
bool ImuService_GetSnapshot(imu_service_snapshot_t *snapshot);
bool ImuService_IsReady(void);
const char *ImuService_StateName(imu_service_state_t state);

#endif
