#ifndef ECHO_H_MISSION_SERVICE_H
#define ECHO_H_MISSION_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#define H_MISSION_LINE_SENSOR_COUNT 8U

typedef enum {
    H_MISSION_LINE_LAP = 0U,
    H_MISSION_BALL_STEP,
    H_MISSION_AB_CENTER,
    H_MISSION_LAP_CENTER,
    H_MISSION_LAP_HOLD,
    H_MISSION_COUNT
} h_mission_id_t;

typedef enum {
    H_LINE_RUNTIME_CAL_IDLE = 0U,
    H_LINE_RUNTIME_CAL_READY,
    H_LINE_RUNTIME_CAL_CAPTURE_WHITE,
    H_LINE_RUNTIME_CAL_CAPTURE_BLACK,
    H_LINE_RUNTIME_CAL_COMPLETE,
    H_LINE_RUNTIME_CAL_FAILED
} h_line_runtime_calibration_state_t;

typedef struct {
    uint32_t start_count[H_MISSION_COUNT];
    uint32_t service_count[H_MISSION_COUNT];
    uint32_t stop_count[H_MISSION_COUNT];
    uint32_t line_scan_count;
    uint32_t line_command_accepted_count;
    uint32_t line_command_busy_count;
    uint32_t line_command_rejected_count;
    uint32_t line_lost_stop_count;
    uint32_t line_finish_count;
    uint32_t line_timeout_stop_count;
    uint32_t line_calibration_save_count;
    uint32_t line_calibration_save_failure_count;
    uint32_t line_runtime_calibration_count;
    uint32_t line_runtime_calibration_failure_count;
    uint32_t line_ambiguous_scan_count;
    uint32_t ab_pass_count;
    uint32_t ab_timeout_count;
    uint32_t ab_elapsed_ms;
    uint32_t balance_lap_timeout_count;
    uint32_t balance_lap_elapsed_ms;
    uint32_t line_last_scan_ms;
    float line_progress_mm;
    float line_clockwise_yaw_deg;
    uint16_t line_calibration_black[H_MISSION_LINE_SENSOR_COUNT];
    uint16_t line_calibration_white[H_MISSION_LINE_SENSOR_COUNT];
    uint16_t line_strength_permille[H_MISSION_LINE_SENSOR_COUNT];
    int16_t line_position_milli;
    int16_t line_filtered_position_milli;
    int16_t line_derivative_milli;
    int16_t line_steering_error_milli;
    int16_t line_correction_deci_rpm;
    int16_t line_left_target_deci_rpm;
    int16_t line_right_target_deci_rpm;
    int16_t h5_feedforward_millidegrees;
    int16_t h5_planned_accel_mm_s2;
    int16_t h5_imu_accel_mm_s2;
    int16_t h5_encoder_accel_mm_s2;
    uint16_t h5_feedback_scale_permille;
    uint8_t active_mission;
    uint8_t initialized;
    uint8_t line_calibration_mask;
    uint8_t line_active_mask;
    uint8_t line_selected_mask;
    uint8_t line_cluster_count;
    uint8_t line_active_count;
    uint8_t line_valid;
    uint8_t line_lost_streak;
    uint8_t line_finish_streak;
    uint8_t line_start_clear_streak;
    uint8_t line_start_cleared;
    uint8_t line_finish_armed;
    uint8_t line_terminal_status;
    uint8_t line_calibration_loaded;
    uint8_t line_calibration_saved;
    uint8_t line_runtime_calibration_state;
    uint8_t line_runtime_calibration_mask;
    uint16_t line_runtime_calibration_samples;
    uint8_t line_runtime_calibration_applied;
    uint8_t line_runtime_white_captured;
    uint8_t line_runtime_black_captured;
    uint8_t line_yaw_valid;
    int16_t line_base_target_deci_rpm;
    uint8_t line_speed_phase;
    uint8_t line_finish_window_mask;
    uint8_t line_finish_evidence_count;
    uint8_t line_braking;
    uint8_t ab_passed;
    uint8_t h5_drive_permitted;
} h_mission_diagnostics_t;

extern volatile h_mission_diagnostics_t g_h_mission_diag;

void HMissionService_Init(void);
void HMissionService_ProcessReflectance(
    const uint16_t raw[H_MISSION_LINE_SENSOR_COUNT],
    uint32_t scan_sequence, uint32_t now_ms);
bool HMissionService_BeginRuntimeCalibration(void);
bool HMissionService_CaptureRuntimeWhite(uint32_t now_ms);
bool HMissionService_CaptureRuntimeBlack(uint32_t now_ms);
void HMissionService_AbortRuntimeCalibration(void);
bool HMissionService_RuntimeCalibrationActive(void);
const char *HMissionService_Code(uint8_t slot);
const char *HMissionService_Name(uint8_t slot);

#endif
