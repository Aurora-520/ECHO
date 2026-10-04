#include "ball_balance_service.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "ball_position_controller.h"
#include "ball_vision.h"
#include "h_ball_control_config.h"
#include "task.h"
#include "zdt_stepper.h"

#define BALL_BALANCE_LEVELING_TIMEOUT_US 1500000U
#define BALL_BALANCE_LEVEL_TOLERANCE_MILLIDEGREES 500
#define BALL_BALANCE_ENABLE_RETRY_US 100000U

typedef enum {
    BALL_BALANCE_MODE_H3_STEP = 0U,
    BALL_BALANCE_MODE_POSITION_HOLD
} ball_balance_mode_t;

static ball_position_controller_t s_controller;
static ball_position_controller_t s_drive_controller;
static uint32_t s_run_start_us;
static uint32_t s_state_start_us;
static uint32_t s_last_command_us;
static uint32_t s_last_control_update_us;
static uint32_t s_last_enable_request_us;
static uint32_t s_last_valid_vision_us;
static uint32_t s_settle_start_us;
static uint32_t s_last_vision_update_sequence;
static uint8_t s_motor_center_valid;
static uint8_t s_center_hold_staged;
static uint8_t s_enable_requested;
static uint8_t s_positive_braking_active;
static uint8_t s_requested_mode;
static uint8_t s_active_mode;
static uint8_t s_requested_drive_hold;
static uint8_t s_active_drive_hold;
static int16_t s_requested_target_decimm;
static int32_t s_completed_hold_output_millidegrees;

volatile ball_balance_diagnostics_t g_ball_balance_diag;

static void BallBalance_ClearChassisFeedforwardInternal(void)
{
    BallPositionController_SetFeedforward(&s_drive_controller, 0.0f);
    (void) BallPositionController_SetOpposingFeedbackScale(
        &s_drive_controller, 1.0f);
    g_ball_balance_diag.snapshot.feedforward_millidegrees = 0;
    g_ball_balance_diag.snapshot.planned_accel_mm_s2 = 0;
    g_ball_balance_diag.snapshot.imu_accel_mm_s2 = 0;
    g_ball_balance_diag.snapshot.encoder_accel_mm_s2 = 0;
}

static int32_t BallBalance_AbsI32(int32_t value)
{
    return value < 0 ? -value : value;
}

static int16_t BallBalance_ClampI16(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t) value;
}

static void BallBalance_NormalizeVision(ball_vision_snapshot_t *vision)
{
    if (vision == NULL) {
        return;
    }
    vision->position_decimm = BallBalance_ClampI16(
        (int32_t) vision->position_decimm * H_BALL_VISION_POLARITY);
    vision->velocity_mm_s = BallBalance_ClampI16(
        (int32_t) vision->velocity_mm_s * H_BALL_VISION_POLARITY);
}

static void BallBalance_UpdateIdleVisionDiagnostics(uint32_t now_us)
{
    ball_vision_snapshot_t vision;
    uint32_t age_ms;

    if (!BallVision_GetSnapshot(now_us, &vision)) {
        return;
    }
    BallBalance_NormalizeVision(&vision);

    g_ball_balance_diag.snapshot.measured_position_decimm =
        vision.position_decimm;
    g_ball_balance_diag.snapshot.velocity_mm_s = vision.velocity_mm_s;
    g_ball_balance_diag.snapshot.vision_sequence = vision.packet_sequence;
    g_ball_balance_diag.snapshot.vision_flags = vision.flags;
    g_ball_balance_diag.snapshot.vision_valid =
        (vision.online != 0U && vision.control_valid != 0U) ? 1U : 0U;
    age_ms = vision.receive_age_us / 1000U;
    g_ball_balance_diag.snapshot.vision_valid_age_ms =
        age_ms > UINT16_MAX ? UINT16_MAX : (uint16_t) age_ms;
}

static void BallBalance_SetState(ball_balance_state_t state,
    uint32_t now_us)
{
    g_ball_balance_diag.snapshot.state = (uint8_t) state;
    s_state_start_us = now_us;
    g_ball_balance_diag.snapshot.update_sequence++;
}

static void BallBalance_SetMissionStatus(
    ball_balance_mission_status_t status)
{
    g_ball_balance_diag.snapshot.mission_status = (uint8_t) status;
    g_ball_balance_diag.snapshot.update_sequence++;
}

static void BallBalance_DeselectMotor(void)
{
    if (g_zdt_stepper_diag.backend_selected != 0U) {
        ZdtStepper_DeselectBackupBackend();
    }
}

static void BallBalance_BeginStopping(uint32_t now_us)
{
    zdt_stepper_request_status_t status;

    BallBalance_ClearChassisFeedforwardInternal();
    s_active_drive_hold = 0U;
    BallBalance_SetMissionStatus(BALL_BALANCE_MISSION_IDLE);
    if (s_motor_center_valid == 0U ||
        g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN2].online == 0U) {
        BallBalance_DeselectMotor();
        BallBalance_SetState(BALL_BALANCE_STATE_STOPPING, now_us);
        return;
    }
    status = ZdtStepper_RequestPositionWithInterrupt(
        ZDT_STEPPER_AXIS_GEN2,
        g_ball_balance_diag.snapshot.motor_center_millidegrees,
        H_BALL_MOTOR_SPEED_RPM,
        H_BALL_MOTOR_ACCELERATION_RPM_S,
        ZDT_POSITION_ABSOLUTE, true);
    if (status == ZDT_STEPPER_REQUEST_ACCEPTED ||
        status == ZDT_STEPPER_REQUEST_DUPLICATE) {
        if (status == ZDT_STEPPER_REQUEST_ACCEPTED) {
            g_ball_balance_diag.snapshot.accepted_command_count++;
        }
        s_last_command_us = now_us;
    } else {
        g_ball_balance_diag.snapshot.rejected_command_count++;
        BallBalance_DeselectMotor();
    }
    BallBalance_SetState(BALL_BALANCE_STATE_STOPPING, now_us);
}

static void BallBalance_LatchFault(ball_balance_fault_t fault)
{
    if (g_ball_balance_diag.snapshot.fault ==
            (uint8_t) BALL_BALANCE_FAULT_NONE) {
        g_ball_balance_diag.snapshot.fault = (uint8_t) fault;
        g_ball_balance_diag.snapshot.fault_count++;
    }
}

static void BallBalance_FinishFault(ball_balance_fault_t fault,
    uint32_t now_us)
{
    BallBalance_ClearChassisFeedforwardInternal();
    s_active_drive_hold = 0U;
    BallBalance_LatchFault(fault);
    BallBalance_DeselectMotor();
    BallBalance_SetMissionStatus(BALL_BALANCE_MISSION_FAULT);
    BallBalance_SetState(BALL_BALANCE_STATE_FAULT, now_us);
}

static void BallBalance_BeginLevelingFault(ball_balance_fault_t fault,
    uint32_t now_us)
{
    zdt_stepper_request_status_t status;

    BallBalance_ClearChassisFeedforwardInternal();
    s_active_drive_hold = 0U;
    BallBalance_LatchFault(fault);
    BallBalance_SetMissionStatus(BALL_BALANCE_MISSION_FAULT);
    if (s_motor_center_valid == 0U ||
        g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN2].online == 0U) {
        BallBalance_FinishFault(fault, now_us);
        return;
    }
    status = ZdtStepper_RequestPositionWithInterrupt(
        ZDT_STEPPER_AXIS_GEN2,
        g_ball_balance_diag.snapshot.motor_center_millidegrees,
        H_BALL_MOTOR_SPEED_RPM,
        H_BALL_MOTOR_ACCELERATION_RPM_S,
        ZDT_POSITION_ABSOLUTE, true);
    if (status == ZDT_STEPPER_REQUEST_ACCEPTED ||
        status == ZDT_STEPPER_REQUEST_DUPLICATE ||
        status == ZDT_STEPPER_REQUEST_BUSY) {
        if (status != ZDT_STEPPER_REQUEST_BUSY) {
            g_ball_balance_diag.snapshot.accepted_command_count++;
        } else {
            g_ball_balance_diag.snapshot.rejected_command_count++;
        }
        BallBalance_SetState(BALL_BALANCE_STATE_LEVELING_FAULT, now_us);
    } else {
        g_ball_balance_diag.snapshot.rejected_command_count++;
        BallBalance_FinishFault(fault, now_us);
    }
}

static bool BallBalance_VisionStartReady(uint32_t now_us,
    ball_vision_snapshot_t *vision)
{
    if (!BallVision_GetSnapshot(now_us, vision) ||
        vision->online == 0U || vision->control_valid == 0U ||
        BallBalance_AbsI32(vision->velocity_mm_s) >
            H_BALL_H3_START_VELOCITY_LIMIT_MM_S) {
        return false;
    }
    BallBalance_NormalizeVision(vision);
    return true;
}

static void BallBalance_StartRequested(uint32_t now_us)
{
    ball_vision_snapshot_t vision;

    g_ball_balance_diag.start_requested = 0U;
    if (!BallBalance_VisionStartReady(now_us, &vision) ||
        !ZdtStepper_SelectBackupBackend()) {
        BallBalance_FinishFault(BALL_BALANCE_FAULT_START_REJECTED,
            now_us);
        return;
    }

    s_active_drive_hold = s_requested_drive_hold;
    if (s_active_drive_hold != 0U) {
        BallPositionController_Reset(&s_drive_controller);
    } else {
        BallPositionController_Reset(&s_controller);
    }
    BallBalance_ClearChassisFeedforwardInternal();
    s_run_start_us = now_us;
    s_last_command_us = now_us - H_BALL_COMMAND_PERIOD_US;
    s_last_control_update_us = now_us;
    s_last_enable_request_us = now_us;
    s_last_valid_vision_us = now_us;
    s_settle_start_us = 0U;
    s_last_vision_update_sequence = vision.update_sequence;
    s_motor_center_valid = 0U;
    s_center_hold_staged = 0U;
    s_enable_requested = 0U;
    s_positive_braking_active = 0U;
    s_active_mode = s_requested_mode;
    s_completed_hold_output_millidegrees = 0;
    g_ball_balance_diag.snapshot.fault = BALL_BALANCE_FAULT_NONE;
    g_ball_balance_diag.snapshot.target_position_decimm =
        s_active_mode == (uint8_t) BALL_BALANCE_MODE_POSITION_HOLD ?
            s_requested_target_decimm : H_BALL_H3_POSITIVE_TARGET_DECIMM;
    g_ball_balance_diag.snapshot.measured_position_decimm =
        vision.position_decimm;
    g_ball_balance_diag.snapshot.velocity_mm_s = vision.velocity_mm_s;
    g_ball_balance_diag.snapshot.vision_sequence = vision.packet_sequence;
    g_ball_balance_diag.snapshot.vision_flags = vision.flags;
    g_ball_balance_diag.snapshot.vision_valid = 1U;
    g_ball_balance_diag.start_count++;
    BallBalance_SetMissionStatus(BALL_BALANCE_MISSION_RUNNING);
    BallBalance_SetState(BALL_BALANCE_STATE_STARTING, now_us);
}

static bool BallBalance_SendMotorTarget(int32_t control_output)
{
    int32_t target = g_ball_balance_diag.snapshot.motor_center_millidegrees +
        H_BALL_MOTOR_POLARITY * control_output;
    zdt_stepper_request_status_t status =
        ZdtStepper_RequestPositionWithInterrupt(
            ZDT_STEPPER_AXIS_GEN2, target,
            H_BALL_MOTOR_SPEED_RPM,
            H_BALL_MOTOR_ACCELERATION_RPM_S,
            ZDT_POSITION_ABSOLUTE, true);

    g_ball_balance_diag.snapshot.motor_target_millidegrees = target;
    if (status == ZDT_STEPPER_REQUEST_ACCEPTED ||
        status == ZDT_STEPPER_REQUEST_DUPLICATE) {
        g_ball_balance_diag.snapshot.accepted_command_count++;
        return true;
    }
    g_ball_balance_diag.snapshot.rejected_command_count++;
    return status == ZDT_STEPPER_REQUEST_BUSY;
}

static void BallBalance_UpdatePhase(const ball_vision_snapshot_t *vision,
    uint32_t now_us)
{
    int16_t error = (int16_t) (
        g_ball_balance_diag.snapshot.target_position_decimm -
        vision->position_decimm);
    uint32_t settle_ms;

    g_ball_balance_diag.snapshot.position_error_decimm = error;
    if (g_ball_balance_diag.snapshot.state ==
            (uint8_t) BALL_BALANCE_STATE_HOLD_TARGET) {
        const ball_position_controller_t *controller =
            s_active_drive_hold != 0U ? &s_drive_controller : &s_controller;
        if (BallBalance_AbsI32(error) <= H_BALL_HOLD_TOLERANCE_DECIMM &&
            controller->filtered_velocity_mm_s <=
                H_BALL_HOLD_VELOCITY_MM_S &&
            controller->filtered_velocity_mm_s >=
                -H_BALL_HOLD_VELOCITY_MM_S) {
            if (s_settle_start_us == 0U) {
                s_settle_start_us = now_us;
            }
            settle_ms = (uint32_t) (now_us - s_settle_start_us) / 1000U;
            g_ball_balance_diag.snapshot.settle_ms =
                settle_ms > UINT16_MAX ? UINT16_MAX : (uint16_t) settle_ms;
        } else {
            s_settle_start_us = 0U;
            g_ball_balance_diag.snapshot.settle_ms = 0U;
        }
        return;
    }
    if (g_ball_balance_diag.snapshot.state ==
            (uint8_t) BALL_BALANCE_STATE_MOVE_POSITIVE &&
        BallBalance_AbsI32(error) <=
            H_BALL_H3_POSITIVE_TOLERANCE_DECIMM &&
        s_controller.filtered_velocity_mm_s <=
            H_BALL_H3_POSITIVE_VELOCITY_MM_S &&
        s_controller.filtered_velocity_mm_s >=
            -H_BALL_H3_POSITIVE_VELOCITY_MM_S) {
        g_ball_balance_diag.snapshot.target_position_decimm =
            H_BALL_H3_NEGATIVE_TARGET_DECIMM;
        s_positive_braking_active = 0U;
        BallPositionController_BeginReversal(&s_controller);
        s_settle_start_us = 0U;
        BallBalance_SetState(BALL_BALANCE_STATE_MOVE_NEGATIVE, now_us);
    } else if (g_ball_balance_diag.snapshot.state ==
            (uint8_t) BALL_BALANCE_STATE_MOVE_NEGATIVE) {
        error = (int16_t) (
            H_BALL_H3_NEGATIVE_TARGET_DECIMM - vision->position_decimm);
        if (BallBalance_AbsI32(error) <=
                H_BALL_H3_FINAL_TOLERANCE_DECIMM &&
            s_controller.filtered_velocity_mm_s <=
                H_BALL_H3_FINAL_VELOCITY_MM_S &&
            s_controller.filtered_velocity_mm_s >=
                -H_BALL_H3_FINAL_VELOCITY_MM_S) {
            if (s_settle_start_us == 0U) {
                s_settle_start_us = now_us;
            }
            settle_ms = (uint32_t) (now_us - s_settle_start_us) / 1000U;
            g_ball_balance_diag.snapshot.settle_ms =
                settle_ms > UINT16_MAX ? UINT16_MAX : (uint16_t) settle_ms;
            if ((uint32_t) (now_us - s_settle_start_us) >=
                    H_BALL_H3_FINAL_SETTLE_US) {
                s_completed_hold_output_millidegrees =
                    H_BALL_MOTOR_POLARITY *
                    (g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN2].
                        position_millidegrees -
                     g_ball_balance_diag.snapshot.
                        motor_center_millidegrees);
                g_ball_balance_diag.snapshot.
                    control_output_millidegrees =
                        s_completed_hold_output_millidegrees;
                g_ball_balance_diag.snapshot.elapsed_ms =
                    (uint32_t) (now_us - s_run_start_us) / 1000U;
                g_ball_balance_diag.complete_count++;
                BallBalance_SetMissionStatus(
                    BALL_BALANCE_MISSION_COMPLETE);
                BallBalance_SetState(
                    BALL_BALANCE_STATE_HOLD_COMPLETE, now_us);
            }
        } else {
            s_settle_start_us = 0U;
            g_ball_balance_diag.snapshot.settle_ms = 0U;
        }
    }
}

static void BallBalance_ServiceClosedLoop(uint32_t now_us)
{
    ball_vision_snapshot_t vision;
    bool snapshot_ok = BallVision_GetSnapshot(now_us, &vision);
    uint32_t vision_hold_us = s_active_drive_hold != 0U ?
        H_BALL_DRIVE_VISION_HOLD_US : H_BALL_VISION_HOLD_US;

    if (snapshot_ok) {
        BallBalance_NormalizeVision(&vision);
    }

    if (snapshot_ok && vision.online != 0U &&
        vision.control_valid != 0U) {
        if (vision.update_sequence != s_last_vision_update_sequence) {
            float dt_s = (float) (uint32_t) (
                now_us - s_last_control_update_us) * 0.000001f;
            int32_t output;

            s_last_vision_update_sequence = vision.update_sequence;
            s_last_valid_vision_us = now_us;
            s_last_control_update_us = now_us;
            g_ball_balance_diag.snapshot.vision_valid = 1U;
            g_ball_balance_diag.snapshot.vision_valid_age_ms = 0U;
            g_ball_balance_diag.snapshot.measured_position_decimm =
                vision.position_decimm;
            g_ball_balance_diag.snapshot.velocity_mm_s =
                vision.velocity_mm_s;
            g_ball_balance_diag.snapshot.vision_sequence =
                vision.packet_sequence;
            g_ball_balance_diag.snapshot.vision_flags = vision.flags;
            if (g_ball_balance_diag.snapshot.state ==
                    (uint8_t) BALL_BALANCE_STATE_HOLD_COMPLETE) {
                output = s_completed_hold_output_millidegrees;
            } else {
                if (g_ball_balance_diag.snapshot.state ==
                        (uint8_t) BALL_BALANCE_STATE_MOVE_POSITIVE &&
                    vision.position_decimm >=
                        H_BALL_H3_POSITIVE_BRAKE_DECIMM) {
                    s_positive_braking_active = 1U;
                }
                if (g_ball_balance_diag.snapshot.state ==
                        (uint8_t) BALL_BALANCE_STATE_MOVE_POSITIVE &&
                    s_positive_braking_active != 0U) {
                    float remaining_mm = (float) (
                        H_BALL_H3_POSITIVE_TARGET_DECIMM -
                        vision.position_decimm) * 0.1f;
                    float brake_zone_mm = (float) (
                        H_BALL_H3_POSITIVE_TARGET_DECIMM -
                        H_BALL_H3_POSITIVE_BRAKE_DECIMM) * 0.1f;
                    float target_velocity_mm_s;

                    if (remaining_mm >= 0.0f) {
                        target_velocity_mm_s =
                            H_BALL_H3_POSITIVE_BRAKE_ENTRY_VELOCITY_MM_S *
                            remaining_mm / brake_zone_mm;
                        if (target_velocity_mm_s >
                                H_BALL_H3_POSITIVE_BRAKE_ENTRY_VELOCITY_MM_S) {
                            target_velocity_mm_s =
                                H_BALL_H3_POSITIVE_BRAKE_ENTRY_VELOCITY_MM_S;
                        }
                    } else {
                        target_velocity_mm_s =
                            H_BALL_NEGATIVE_POSITION_VELOCITY_GAIN_PER_S *
                            remaining_mm;
                    }
                    output = BallPositionController_UpdateProfiled(
                        &s_controller,
                        (float) H_BALL_H3_POSITIVE_TARGET_DECIMM * 0.1f,
                        target_velocity_mm_s,
                        H_BALL_H3_POSITIVE_BRAKE_GAIN_MDEG_PER_MM_S,
                        (float) vision.position_decimm * 0.1f,
                        (float) vision.velocity_mm_s, dt_s);
                } else {
                    if (s_active_drive_hold != 0U) {
                        bool inside_hold_band =
                            BallBalance_AbsI32(
                                (int32_t) g_ball_balance_diag.snapshot.
                                    target_position_decimm -
                                (int32_t) vision.position_decimm) <=
                            H_BALL_HOLD_TOLERANCE_DECIMM;
                        output = inside_hold_band ?
                            BallPositionController_UpdateDriveHold(
                                &s_drive_controller,
                                (float) g_ball_balance_diag.snapshot.
                                    target_position_decimm * 0.1f,
                                (float) vision.position_decimm * 0.1f,
                                (float) vision.velocity_mm_s, dt_s) :
                            BallPositionController_UpdateDrive(
                            &s_drive_controller,
                            (float) g_ball_balance_diag.snapshot.
                                target_position_decimm * 0.1f,
                            (float) vision.position_decimm * 0.1f,
                            (float) vision.velocity_mm_s, dt_s);
                    } else {
                        output = BallPositionController_Update(&s_controller,
                            (float) g_ball_balance_diag.snapshot.
                                target_position_decimm * 0.1f,
                            (float) vision.position_decimm * 0.1f,
                            (float) vision.velocity_mm_s, dt_s);
                    }
                }
            }
            g_ball_balance_diag.snapshot.control_output_millidegrees = output;
            g_ball_balance_diag.snapshot.saturated =
                s_active_drive_hold != 0U ? s_drive_controller.saturated :
                    s_controller.saturated;
            if ((uint32_t) (now_us - s_last_command_us) >=
                    H_BALL_COMMAND_PERIOD_US) {
                if (!BallBalance_SendMotorTarget(output)) {
                    BallBalance_BeginLevelingFault(
                        BALL_BALANCE_FAULT_MOTOR_COMMAND, now_us);
                    return;
                }
                s_last_command_us = now_us;
            }
            BallBalance_UpdatePhase(&vision, now_us);
        } else {
            uint32_t valid_age_us =
                (uint32_t) (now_us - s_last_valid_vision_us);

            g_ball_balance_diag.snapshot.vision_valid_age_ms =
                valid_age_us / 1000U > UINT16_MAX ? UINT16_MAX :
                    (uint16_t) (valid_age_us / 1000U);
            if (valid_age_us > vision_hold_us &&
                g_ball_balance_diag.snapshot.state !=
                    (uint8_t) BALL_BALANCE_STATE_HOLD_COMPLETE) {
                g_ball_balance_diag.snapshot.vision_valid = 0U;
                g_ball_balance_diag.snapshot.vision_invalid_count++;
                BallBalance_BeginLevelingFault(
                    BALL_BALANCE_FAULT_VISION_TIMEOUT, now_us);
                return;
            }
        }
    } else {
        uint32_t invalid_age_us =
            (uint32_t) (now_us - s_last_valid_vision_us);

        g_ball_balance_diag.snapshot.vision_valid = 0U;
        g_ball_balance_diag.snapshot.vision_invalid_count++;
        g_ball_balance_diag.snapshot.vision_valid_age_ms =
            invalid_age_us / 1000U > UINT16_MAX ? UINT16_MAX :
                (uint16_t) (invalid_age_us / 1000U);
        if (invalid_age_us > vision_hold_us &&
            g_ball_balance_diag.snapshot.state !=
                (uint8_t) BALL_BALANCE_STATE_HOLD_COMPLETE) {
            BallBalance_BeginLevelingFault(snapshot_ok &&
                    vision.online != 0U ?
                BALL_BALANCE_FAULT_VISION_INVALID :
                BALL_BALANCE_FAULT_VISION_TIMEOUT, now_us);
            return;
        }
    }

    if (s_active_mode == (uint8_t) BALL_BALANCE_MODE_H3_STEP &&
        H_BALL_H3_TIMEOUT_US != 0U &&
        g_ball_balance_diag.snapshot.state !=
            (uint8_t) BALL_BALANCE_STATE_HOLD_COMPLETE &&
        (uint32_t) (now_us - s_run_start_us) >= H_BALL_H3_TIMEOUT_US) {
        BallBalance_BeginLevelingFault(BALL_BALANCE_FAULT_H3_TIMEOUT,
            now_us);
    }
}

void BallBalanceService_Init(void)
{
    const ball_position_controller_config_t config = {
        .position_velocity_gain_per_s =
            H_BALL_POSITION_VELOCITY_GAIN_PER_S,
        .negative_position_velocity_gain_per_s =
            H_BALL_NEGATIVE_POSITION_VELOCITY_GAIN_PER_S,
        .maximum_velocity_mm_s =
            H_BALL_MAXIMUM_VELOCITY_MM_S,
        .search_integral_gain_millidegrees_per_mm_s =
            H_BALL_SEARCH_GAIN_MDEG_PER_MM_S,
        .maximum_search_rate_millidegrees_per_s =
            H_BALL_MAXIMUM_SEARCH_RATE_MDEG_PER_S,
        .velocity_noise_deadband_mm_s =
            H_BALL_VELOCITY_NOISE_DEADBAND_MM_S,
        .motion_detection_velocity_mm_s =
            H_BALL_MOTION_DETECTION_VELOCITY_MM_S,
        .motion_detection_displacement_mm =
            H_BALL_MOTION_DETECTION_DISPLACEMENT_MM,
        .search_velocity_damping_millidegrees_per_mm_s =
            H_BALL_SEARCH_VELOCITY_DAMPING_MDEG_PER_MM_S,
        .velocity_feedback_millidegrees_per_mm_s =
            H_BALL_VELOCITY_FEEDBACK_MDEG_PER_MM_S,
        .reversal_braking_millidegrees_per_mm_s =
            H_BALL_REVERSAL_BRAKING_MDEG_PER_MM_S,
        .moving_bias_decay_millidegrees_per_s =
            H_BALL_MOVING_BIAS_DECAY_MDEG_PER_S,
        .hold_bias_decay_millidegrees_per_s =
            H_BALL_HOLD_BIAS_DECAY_MDEG_PER_S,
        .stall_reacquire_position_error_mm =
            H_BALL_STALL_REACQUIRE_ERROR_MM,
        .integral_limit_millidegrees =
            H_BALL_INTEGRAL_LIMIT_MILLIDEGREES,
        .maximum_output_millidegrees =
            H_BALL_MAXIMUM_OUTPUT_MILLIDEGREES,
        .maximum_slew_millidegrees_per_s =
            H_BALL_MAXIMUM_SLEW_MILLIDEGREES_PER_S,
        .stall_reacquire_confirm_samples =
            H_BALL_STALL_REACQUIRE_SAMPLES
    };

    memset((void *) &g_ball_balance_diag, 0,
        sizeof(g_ball_balance_diag));
    s_completed_hold_output_millidegrees = 0;
    s_requested_mode = (uint8_t) BALL_BALANCE_MODE_H3_STEP;
    s_active_mode = (uint8_t) BALL_BALANCE_MODE_H3_STEP;
    s_requested_drive_hold = 0U;
    s_active_drive_hold = 0U;
    s_requested_target_decimm = 0;
    (void) BallPositionController_Init(&s_controller, &config);
    {
        ball_position_controller_config_t drive_config = config;
        drive_config.integral_limit_millidegrees =
            H_BALL_H5_INTEGRAL_LIMIT_MILLIDEGREES;
        drive_config.maximum_output_millidegrees =
            H_BALL_H5_MAXIMUM_OUTPUT_MILLIDEGREES;
        (void) BallPositionController_Init(&s_drive_controller,
            &drive_config);
    }
    g_ball_balance_diag.snapshot.state = BALL_BALANCE_STATE_IDLE;
    g_ball_balance_diag.snapshot.mission_status =
        BALL_BALANCE_MISSION_IDLE;
    g_ball_balance_diag.initialized = 1U;
}

void BallBalanceService_Service(uint32_t now_us)
{
    const volatile zdt_stepper_axis_diagnostics_t *motor =
        &g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN2];
    uint8_t state;

    if (g_ball_balance_diag.initialized == 0U) {
        return;
    }
    if (g_ball_balance_diag.abort_requested != 0U) {
        g_ball_balance_diag.abort_requested = 0U;
        g_ball_balance_diag.start_requested = 0U;
        g_ball_balance_diag.abort_count++;
        BallBalance_BeginStopping(now_us);
    }
    if (g_ball_balance_diag.start_requested != 0U) {
        BallBalance_StartRequested(now_us);
    }

    state = g_ball_balance_diag.snapshot.state;
    if (state == (uint8_t) BALL_BALANCE_STATE_IDLE ||
        state == (uint8_t) BALL_BALANCE_STATE_FAULT) {
        BallBalance_UpdateIdleVisionDiagnostics(now_us);
    }
    g_ball_balance_diag.snapshot.motor_online = motor->online;
    g_ball_balance_diag.snapshot.motor_enabled = motor->enabled;
    g_ball_balance_diag.snapshot.motor_actual_millidegrees =
        motor->position_millidegrees;
    g_ball_balance_diag.snapshot.motor_error_millidegrees =
        motor->position_error_millidegrees;
    if (state == (uint8_t) BALL_BALANCE_STATE_STARTING ||
        state == (uint8_t) BALL_BALANCE_STATE_MOVE_POSITIVE ||
        state == (uint8_t) BALL_BALANCE_STATE_MOVE_NEGATIVE ||
        state == (uint8_t) BALL_BALANCE_STATE_HOLD_TARGET) {
        g_ball_balance_diag.snapshot.elapsed_ms =
            (uint32_t) (now_us - s_run_start_us) / 1000U;
    }

    if (state == (uint8_t) BALL_BALANCE_STATE_STARTING) {
        if (s_motor_center_valid == 0U && motor->online != 0U &&
            motor->system_status_valid != 0U) {
            zdt_stepper_request_status_t request;

            g_ball_balance_diag.snapshot.motor_center_millidegrees =
                motor->position_millidegrees;
            g_ball_balance_diag.snapshot.motor_target_millidegrees =
                motor->position_millidegrees;
            request = ZdtStepper_RequestPositionWithInterrupt(
                ZDT_STEPPER_AXIS_GEN2, motor->position_millidegrees,
                H_BALL_MOTOR_SPEED_RPM,
                H_BALL_MOTOR_ACCELERATION_RPM_S,
                ZDT_POSITION_ABSOLUTE, true);
            if (request == ZDT_STEPPER_REQUEST_ACCEPTED ||
                request == ZDT_STEPPER_REQUEST_DUPLICATE) {
                s_motor_center_valid = 1U;
                s_center_hold_staged = 1U;
                s_last_command_us = now_us;
                if (request == ZDT_STEPPER_REQUEST_ACCEPTED) {
                    g_ball_balance_diag.snapshot.accepted_command_count++;
                }
            }
        } else if (s_center_hold_staged != 0U &&
            s_enable_requested == 0U &&
            (uint32_t) (now_us - s_last_command_us) >=
                H_BALL_COMMAND_PERIOD_US) {
            zdt_stepper_request_status_t request =
                ZdtStepper_RequestEnable(ZDT_STEPPER_AXIS_GEN2, true);

            if (request == ZDT_STEPPER_REQUEST_ACCEPTED ||
                request == ZDT_STEPPER_REQUEST_DUPLICATE) {
                s_enable_requested = 1U;
                s_last_enable_request_us = now_us;
            } else if (request != ZDT_STEPPER_REQUEST_BUSY) {
                BallBalance_FinishFault(
                    BALL_BALANCE_FAULT_MOTOR_BACKEND, now_us);
                return;
            }
        } else if (s_enable_requested != 0U &&
            motor->enabled != 0U) {
            s_last_control_update_us = now_us;
            BallBalance_SetState(s_active_mode ==
                    (uint8_t) BALL_BALANCE_MODE_POSITION_HOLD ?
                BALL_BALANCE_STATE_HOLD_TARGET :
                BALL_BALANCE_STATE_MOVE_POSITIVE, now_us);
        } else if (s_enable_requested != 0U &&
            (uint32_t) (now_us - s_last_enable_request_us) >=
                BALL_BALANCE_ENABLE_RETRY_US) {
            zdt_stepper_request_status_t request =
                ZdtStepper_RequestEnable(ZDT_STEPPER_AXIS_GEN2, true);

            s_last_enable_request_us = now_us;
            if (request != ZDT_STEPPER_REQUEST_ACCEPTED &&
                request != ZDT_STEPPER_REQUEST_DUPLICATE &&
                request != ZDT_STEPPER_REQUEST_BUSY) {
                BallBalance_FinishFault(
                    BALL_BALANCE_FAULT_MOTOR_BACKEND, now_us);
                return;
            }
        }
        if ((uint32_t) (now_us - s_state_start_us) >=
                H_BALL_STARTUP_TIMEOUT_US) {
            BallBalance_FinishFault(BALL_BALANCE_FAULT_MOTOR_OFFLINE,
                now_us);
        }
    } else if (state == (uint8_t) BALL_BALANCE_STATE_MOVE_POSITIVE ||
        state == (uint8_t) BALL_BALANCE_STATE_MOVE_NEGATIVE ||
        state == (uint8_t) BALL_BALANCE_STATE_HOLD_COMPLETE ||
        state == (uint8_t) BALL_BALANCE_STATE_HOLD_TARGET) {
        if (motor->online == 0U || motor->enabled == 0U) {
            BallBalance_FinishFault(BALL_BALANCE_FAULT_MOTOR_OFFLINE,
                now_us);
            return;
        }
        BallBalance_ServiceClosedLoop(now_us);
    } else if (state == (uint8_t) BALL_BALANCE_STATE_LEVELING_FAULT) {
        if (motor->online != 0U &&
            BallBalance_AbsI32(motor->position_millidegrees -
                g_ball_balance_diag.snapshot.motor_center_millidegrees) >
                BALL_BALANCE_LEVEL_TOLERANCE_MILLIDEGREES &&
            (uint32_t) (now_us - s_last_command_us) >=
                H_BALL_COMMAND_PERIOD_US) {
            zdt_stepper_request_status_t request =
                ZdtStepper_RequestPositionWithInterrupt(
                    ZDT_STEPPER_AXIS_GEN2,
                    g_ball_balance_diag.snapshot.motor_center_millidegrees,
                    H_BALL_MOTOR_SPEED_RPM,
                    H_BALL_MOTOR_ACCELERATION_RPM_S,
                    ZDT_POSITION_ABSOLUTE, true);

            s_last_command_us = now_us;
            if (request == ZDT_STEPPER_REQUEST_ACCEPTED ||
                request == ZDT_STEPPER_REQUEST_DUPLICATE) {
                g_ball_balance_diag.snapshot.accepted_command_count++;
            } else {
                g_ball_balance_diag.snapshot.rejected_command_count++;
            }
        }
        if (motor->online == 0U ||
            BallBalance_AbsI32(motor->position_millidegrees -
                g_ball_balance_diag.snapshot.motor_center_millidegrees) <=
                BALL_BALANCE_LEVEL_TOLERANCE_MILLIDEGREES ||
            (uint32_t) (now_us - s_state_start_us) >=
                BALL_BALANCE_LEVELING_TIMEOUT_US) {
            BallBalance_DeselectMotor();
            BallBalance_SetState(BALL_BALANCE_STATE_FAULT, now_us);
        }
    } else if (state == (uint8_t) BALL_BALANCE_STATE_STOPPING) {
        const volatile zdt_stepper_axis_diagnostics_t *stopping_motor =
            &g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN2];
        bool level_reached = s_motor_center_valid != 0U &&
            stopping_motor->online != 0U &&
            BallBalance_AbsI32(stopping_motor->position_millidegrees -
                g_ball_balance_diag.snapshot.motor_center_millidegrees) <=
                BALL_BALANCE_LEVEL_TOLERANCE_MILLIDEGREES;
        bool level_timed_out =
            (uint32_t) (now_us - s_state_start_us) >=
                BALL_BALANCE_LEVELING_TIMEOUT_US;

        if (!level_reached && !level_timed_out &&
            s_motor_center_valid != 0U &&
            stopping_motor->online != 0U &&
            (uint32_t) (now_us - s_last_command_us) >=
                H_BALL_COMMAND_PERIOD_US) {
            zdt_stepper_request_status_t request =
                ZdtStepper_RequestPositionWithInterrupt(
                    ZDT_STEPPER_AXIS_GEN2,
                    g_ball_balance_diag.snapshot.motor_center_millidegrees,
                    H_BALL_MOTOR_SPEED_RPM,
                    H_BALL_MOTOR_ACCELERATION_RPM_S,
                    ZDT_POSITION_ABSOLUTE, true);

            s_last_command_us = now_us;
            if (request == ZDT_STEPPER_REQUEST_ACCEPTED ||
                request == ZDT_STEPPER_REQUEST_DUPLICATE) {
                if (request == ZDT_STEPPER_REQUEST_ACCEPTED) {
                    g_ball_balance_diag.snapshot.accepted_command_count++;
                }
            } else {
                g_ball_balance_diag.snapshot.rejected_command_count++;
            }
        }
        if ((level_reached || level_timed_out ||
                s_motor_center_valid == 0U ||
                stopping_motor->online == 0U) &&
            g_zdt_stepper_diag.backend_selected != 0U) {
            BallBalance_DeselectMotor();
        }
        if (g_zdt_stepper_diag.backend_selected == 0U &&
            g_zdt_stepper_diag.shutdown_pending == 0U) {
            s_motor_center_valid = 0U;
            BallBalance_SetState(BALL_BALANCE_STATE_IDLE, now_us);
        }
    }
    g_ball_balance_diag.snapshot.update_sequence++;
}

bool BallBalanceService_CanStartH3(uint32_t now_us)
{
    ball_vision_snapshot_t vision;
    uint8_t state = g_ball_balance_diag.snapshot.state;

    return g_ball_balance_diag.initialized != 0U &&
        (state == (uint8_t) BALL_BALANCE_STATE_IDLE ||
         state == (uint8_t) BALL_BALANCE_STATE_FAULT) &&
        g_zdt_stepper_diag.shutdown_pending == 0U &&
        BallBalance_VisionStartReady(now_us, &vision);
}

bool BallBalanceService_RequestStartH3(void)
{
    if (g_ball_balance_diag.initialized == 0U ||
        g_ball_balance_diag.start_requested != 0U) {
        return false;
    }
    s_requested_mode = (uint8_t) BALL_BALANCE_MODE_H3_STEP;
    s_requested_drive_hold = 0U;
    s_requested_target_decimm = H_BALL_H3_POSITIVE_TARGET_DECIMM;
    g_ball_balance_diag.start_requested = 1U;
    return true;
}

bool BallBalanceService_CanStartPositionHold(uint32_t now_us)
{
    return BallBalanceService_CanStartH3(now_us);
}

bool BallBalanceService_RequestStartPositionHold(int16_t target_decimm)
{
    if (g_ball_balance_diag.initialized == 0U ||
        g_ball_balance_diag.start_requested != 0U ||
        BallBalance_AbsI32(target_decimm) >
            H_BALL_HOLD_TARGET_LIMIT_DECIMM) {
        return false;
    }
    s_requested_mode = (uint8_t) BALL_BALANCE_MODE_POSITION_HOLD;
    s_requested_drive_hold = 0U;
    s_requested_target_decimm = target_decimm;
    g_ball_balance_diag.start_requested = 1U;
    return true;
}

bool BallBalanceService_RequestStartDrivePositionHold(
    int16_t target_decimm)
{
    if (g_ball_balance_diag.initialized == 0U ||
        g_ball_balance_diag.start_requested != 0U ||
        BallBalance_AbsI32(target_decimm) >
            H_BALL_HOLD_TARGET_LIMIT_DECIMM) {
        return false;
    }
    s_requested_mode = (uint8_t) BALL_BALANCE_MODE_POSITION_HOLD;
    s_requested_drive_hold = 1U;
    s_requested_target_decimm = target_decimm;
    g_ball_balance_diag.start_requested = 1U;
    return true;
}

bool BallBalanceService_RequestStartDrivePositionHoldCurrent(
    uint32_t now_us, int16_t *latched_target_decimm)
{
    ball_vision_snapshot_t vision;

    if (g_ball_balance_diag.initialized == 0U ||
        g_ball_balance_diag.start_requested != 0U ||
        g_zdt_stepper_diag.shutdown_pending != 0U ||
        !BallBalance_VisionStartReady(now_us, &vision) ||
        BallBalance_AbsI32(vision.position_decimm) >
            H_BALL_HOLD_TARGET_LIMIT_DECIMM) {
        return false;
    }
    s_requested_mode = (uint8_t) BALL_BALANCE_MODE_POSITION_HOLD;
    s_requested_drive_hold = 1U;
    s_requested_target_decimm = vision.position_decimm;
    g_ball_balance_diag.start_requested = 1U;
    if (latched_target_decimm != NULL) {
        *latched_target_decimm = vision.position_decimm;
    }
    return true;
}

bool BallBalanceService_IsTargetSettled(void)
{
    bool settled;

    taskENTER_CRITICAL();
    settled = g_ball_balance_diag.snapshot.state ==
            (uint8_t) BALL_BALANCE_STATE_HOLD_TARGET &&
        g_ball_balance_diag.snapshot.settle_ms >=
            (uint16_t) (H_BALL_HOLD_SETTLE_US / 1000U);
    taskEXIT_CRITICAL();
    return settled;
}

bool BallBalanceService_IsDriveHoldReady(void)
{
    bool ready;

    taskENTER_CRITICAL();
    ready = s_active_drive_hold != 0U &&
        g_ball_balance_diag.snapshot.mission_status ==
            (uint8_t) BALL_BALANCE_MISSION_RUNNING &&
        g_ball_balance_diag.snapshot.state ==
            (uint8_t) BALL_BALANCE_STATE_HOLD_TARGET;
    taskEXIT_CRITICAL();
    return ready;
}

bool BallBalanceService_SetChassisFeedforward(
    int16_t feedforward_millidegrees, int16_t planned_accel_mm_s2,
    int16_t imu_accel_mm_s2, int16_t encoder_accel_mm_s2)
{
    bool accepted;

    taskENTER_CRITICAL();
    accepted = s_active_drive_hold != 0U &&
        g_ball_balance_diag.snapshot.mission_status ==
            (uint8_t) BALL_BALANCE_MISSION_RUNNING;
    if (accepted) {
        BallPositionController_SetFeedforward(&s_drive_controller,
            (float) feedforward_millidegrees);
        g_ball_balance_diag.snapshot.feedforward_millidegrees =
            feedforward_millidegrees;
        g_ball_balance_diag.snapshot.planned_accel_mm_s2 =
            planned_accel_mm_s2;
        g_ball_balance_diag.snapshot.imu_accel_mm_s2 =
            imu_accel_mm_s2;
        g_ball_balance_diag.snapshot.encoder_accel_mm_s2 =
            encoder_accel_mm_s2;
    }
    taskEXIT_CRITICAL();
    return accepted;
}

bool BallBalanceService_SetOpposingFeedbackScale(uint16_t scale_permille)
{
    bool accepted;

    if (scale_permille > 1000U) {
        return false;
    }
    taskENTER_CRITICAL();
    accepted = s_active_drive_hold != 0U &&
        g_ball_balance_diag.snapshot.mission_status ==
            (uint8_t) BALL_BALANCE_MISSION_RUNNING;
    if (accepted) {
        accepted = BallPositionController_SetOpposingFeedbackScale(
            &s_drive_controller, (float) scale_permille * 0.001f);
    }
    taskEXIT_CRITICAL();
    return accepted;
}

void BallBalanceService_ClearChassisFeedforward(void)
{
    taskENTER_CRITICAL();
    BallBalance_ClearChassisFeedforwardInternal();
    taskEXIT_CRITICAL();
}

void BallBalanceService_RequestAbort(void)
{
    if (g_ball_balance_diag.initialized != 0U) {
        g_ball_balance_diag.abort_requested = 1U;
    }
}

ball_balance_mission_status_t BallBalanceService_GetMissionStatus(void)
{
    if (g_ball_balance_diag.start_requested != 0U) {
        return BALL_BALANCE_MISSION_RUNNING;
    }
    return (ball_balance_mission_status_t)
        g_ball_balance_diag.snapshot.mission_status;
}

bool BallBalanceService_GetSnapshot(ball_balance_snapshot_t *snapshot)
{
    if (snapshot == NULL || g_ball_balance_diag.initialized == 0U) {
        return false;
    }
    taskENTER_CRITICAL();
    *snapshot = g_ball_balance_diag.snapshot;
    taskEXIT_CRITICAL();
    return true;
}
