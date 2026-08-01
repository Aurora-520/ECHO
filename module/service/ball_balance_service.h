#ifndef ECHO_BALL_BALANCE_SERVICE_H
#define ECHO_BALL_BALANCE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BALL_BALANCE_STATE_IDLE = 0U,
    BALL_BALANCE_STATE_STARTING,
    BALL_BALANCE_STATE_MOVE_POSITIVE,
    BALL_BALANCE_STATE_MOVE_NEGATIVE,
    BALL_BALANCE_STATE_HOLD_COMPLETE,
    BALL_BALANCE_STATE_LEVELING_FAULT,
    BALL_BALANCE_STATE_STOPPING,
    BALL_BALANCE_STATE_FAULT,
    BALL_BALANCE_STATE_HOLD_TARGET
} ball_balance_state_t;

typedef enum {
    BALL_BALANCE_FAULT_NONE = 0U,
    BALL_BALANCE_FAULT_START_REJECTED,
    BALL_BALANCE_FAULT_VISION_INVALID,
    BALL_BALANCE_FAULT_VISION_TIMEOUT,
    BALL_BALANCE_FAULT_MOTOR_BACKEND,
    BALL_BALANCE_FAULT_MOTOR_OFFLINE,
    BALL_BALANCE_FAULT_MOTOR_COMMAND,
    BALL_BALANCE_FAULT_H3_TIMEOUT
} ball_balance_fault_t;

typedef enum {
    BALL_BALANCE_MISSION_IDLE = 0U,
    BALL_BALANCE_MISSION_RUNNING,
    BALL_BALANCE_MISSION_COMPLETE,
    BALL_BALANCE_MISSION_FAULT
} ball_balance_mission_status_t;

typedef struct {
    uint32_t update_sequence;
    uint32_t elapsed_ms;
    uint32_t accepted_command_count;
    uint32_t rejected_command_count;
    uint32_t vision_invalid_count;
    uint32_t fault_count;
    int32_t control_output_millidegrees;
    int32_t motor_center_millidegrees;
    int32_t motor_target_millidegrees;
    int32_t motor_actual_millidegrees;
    int32_t motor_error_millidegrees;
    int16_t target_position_decimm;
    int16_t measured_position_decimm;
    int16_t velocity_mm_s;
    int16_t position_error_decimm;
    int16_t feedforward_millidegrees;
    int16_t planned_accel_mm_s2;
    int16_t imu_accel_mm_s2;
    int16_t encoder_accel_mm_s2;
    uint16_t vision_sequence;
    uint16_t vision_valid_age_ms;
    uint16_t settle_ms;
    uint8_t vision_flags;
    uint8_t state;
    uint8_t mission_status;
    uint8_t fault;
    uint8_t vision_valid;
    uint8_t saturated;
    uint8_t motor_online;
    uint8_t motor_enabled;
} ball_balance_snapshot_t;

typedef struct {
    ball_balance_snapshot_t snapshot;
    uint32_t start_count;
    uint32_t complete_count;
    uint32_t abort_count;
    uint8_t start_requested;
    uint8_t abort_requested;
    uint8_t initialized;
    uint8_t reserved;
} ball_balance_diagnostics_t;

extern volatile ball_balance_diagnostics_t g_ball_balance_diag;

void BallBalanceService_Init(void);
void BallBalanceService_Service(uint32_t now_us);
bool BallBalanceService_CanStartH3(uint32_t now_us);
bool BallBalanceService_RequestStartH3(void);
bool BallBalanceService_CanStartPositionHold(uint32_t now_us);
bool BallBalanceService_RequestStartPositionHold(int16_t target_decimm);
bool BallBalanceService_RequestStartDrivePositionHold(
    int16_t target_decimm);
bool BallBalanceService_RequestStartDrivePositionHoldCurrent(
    uint32_t now_us, int16_t *latched_target_decimm);
bool BallBalanceService_IsTargetSettled(void);
bool BallBalanceService_IsDriveHoldReady(void);
bool BallBalanceService_SetChassisFeedforward(
    int16_t feedforward_millidegrees, int16_t planned_accel_mm_s2,
    int16_t imu_accel_mm_s2, int16_t encoder_accel_mm_s2);
bool BallBalanceService_SetOpposingFeedbackScale(uint16_t scale_permille);
void BallBalanceService_ClearChassisFeedforward(void);
void BallBalanceService_RequestAbort(void);
ball_balance_mission_status_t BallBalanceService_GetMissionStatus(void);
bool BallBalanceService_GetSnapshot(ball_balance_snapshot_t *snapshot);

#endif
