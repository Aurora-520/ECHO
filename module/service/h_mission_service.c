#include "h_mission_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "chassis_actuator.h"
#include "attitude_estimator.h"
#include "ball_balance_service.h"
#include "bsp_time.h"
#include "competition_service.h"
#include "competition_storage.h"
#include "vehicle_bringup_config.h"

typedef struct {
    uint8_t slot;
    uint8_t running;
} h_mission_context_t;

typedef enum {
    H_LINE_TERMINAL_NONE = 0U,
    H_LINE_TERMINAL_BRAKING,
    H_LINE_TERMINAL_COMPLETE,
    H_LINE_TERMINAL_FAULT
} h_line_terminal_t;

typedef enum {
    H_LINE_SPEED_LAUNCH = 0U,
    H_LINE_SPEED_CURVE,
    H_LINE_SPEED_CRUISE,
    H_LINE_SPEED_FINISH
} h_line_speed_phase_t;

typedef struct {
    uint32_t white_sum[H_MISSION_LINE_SENSOR_COUNT];
    uint32_t black_sum[H_MISSION_LINE_SENSOR_COUNT];
    competition_reflectance_calibration_t candidate;
    uint32_t capture_start_ms;
    uint16_t sample_count;
    uint8_t white_captured;
    uint8_t black_captured;
} h_line_runtime_calibration_t;

#define H_LINE_CALIBRATION_MINIMUM_SPAN       180U
#define H_LINE_ACTIVE_THRESHOLD_PERMILLE      350U
#define H_LINE_CLUSTER_SUPPORT_PERMILLE       100U
#define H_LINE_MINIMUM_TOTAL_STRENGTH         450U
#if ECHO_REFLECTANCE_BACKEND == ECHO_REFLECTANCE_BACKEND_I2C6
#define H_LINE_LAUNCH_TARGET_DECI_RPM         600
#define H_LINE_CURVE_TARGET_DECI_RPM          600
#define H_LINE_CRUISE_TARGET_DECI_RPM         600
#define H_LINE_FINISH_TARGET_DECI_RPM         400
#define H_LINE_MAXIMUM_CORRECTION_DECI_RPM    460
#else
#define H_LINE_LAUNCH_TARGET_DECI_RPM        1000
#define H_LINE_CURVE_TARGET_DECI_RPM         1200
#define H_LINE_CRUISE_TARGET_DECI_RPM        1400
#define H_LINE_FINISH_TARGET_DECI_RPM         750
#define H_LINE_MAXIMUM_CORRECTION_DECI_RPM    750
#endif
#define H_LINE_MINIMUM_TARGET_DECI_RPM         60
#define H_LINE_MAXIMUM_TARGET_DECI_RPM       2200
#define H_LINE_COMMAND_PERIOD_MS              16U
#if ECHO_REFLECTANCE_BACKEND == ECHO_REFLECTANCE_BACKEND_I2C6
#define H_LINE_CENTER_DEADBAND_MILLI          150
#else
#define H_LINE_CENTER_DEADBAND_MILLI          100
#endif
#define H_LINE_LAUNCH_DERIVATIVE_LEAD_SCANS     6
#if ECHO_REFLECTANCE_BACKEND == ECHO_REFLECTANCE_BACKEND_I2C6
#define H_LINE_DERIVATIVE_LEAD_SCANS           15
#define H_LINE_DERIVATIVE_LIMIT_MILLI          750
#define H_LINE_CURVE_STEERING_FULL_SCALE_MILLI 3000
#else
#define H_LINE_DERIVATIVE_LEAD_SCANS           18
#define H_LINE_DERIVATIVE_LIMIT_MILLI         1000
#define H_LINE_CURVE_STEERING_FULL_SCALE_MILLI 3500
#endif
#if ECHO_REFLECTANCE_BACKEND == ECHO_REFLECTANCE_BACKEND_I2C6
#define H_LINE_LAUNCH_MAX_CORRECTION_DECI_RPM 360
#else
#define H_LINE_LAUNCH_MAX_CORRECTION_DECI_RPM 450
#endif
#if ECHO_REFLECTANCE_BACKEND == ECHO_REFLECTANCE_BACKEND_I2C6
#define H_LINE_CORRECTION_ATTACK_SLEW_DECI_RPM  80
#define H_LINE_CORRECTION_RELEASE_SLEW_DECI_RPM 40
#define H_LINE_STRAIGHT_ATTACK_SLEW_DECI_RPM    40
#else
#define H_LINE_CORRECTION_ATTACK_SLEW_DECI_RPM  80
#define H_LINE_CORRECTION_RELEASE_SLEW_DECI_RPM 80
#define H_LINE_STRAIGHT_ATTACK_SLEW_DECI_RPM    80
#endif
#define H_LINE_LOST_STOP_SCANS                5U
#define H_LINE_FINISH_WINDOW_SCANS             5U
#define H_LINE_FINISH_STRONG_RUN               4U
#define H_LINE_FINISH_STRONG_CONFIRM           2U
#define H_LINE_START_CLEAR_MAXIMUM_ACTIVE     3U
#define H_LINE_START_CLEAR_CONFIRM_SCANS      5U
#define H_LINE_FINISH_ARM_DISTANCE_MM       4500.0f
#define H_LINE_FINISH_FALLBACK_DISTANCE_MM  5800.0f
#define H_LINE_FINISH_ARM_CLOCKWISE_DEG       270.0f
#define H_LINE_FINISH_SLOW_DISTANCE_MM       5500.0f
#define H_LINE_FINISH_SLOW_FALLBACK_MM       5650.0f
#define H_LINE_FINISH_SLOW_CLOCKWISE_DEG      300.0f
#define H_LINE_LAUNCH_DISTANCE_MM             250.0f
#define H_LINE_CRUISE_ENTER_ERROR_MILLI       250
#define H_LINE_CRUISE_EXIT_ERROR_MILLI        450
#define H_LINE_CRUISE_ENTER_SCANS               8U
#define H_LINE_CRUISE_ENTER_YAW_RATE_DPS       12.0f
#define H_LINE_CRUISE_EXIT_YAW_RATE_DPS        18.0f
#define H_LINE_STRAIGHT_POSITION_MILLI           450
#if ECHO_REFLECTANCE_BACKEND == ECHO_REFLECTANCE_BACKEND_I2C6
#define H_LINE_STRAIGHT_DERIVATIVE_LEAD_SCANS      3
#define H_LINE_STRAIGHT_MAX_CORRECTION_DECI_RPM  160
#else
#define H_LINE_STRAIGHT_DERIVATIVE_LEAD_SCANS      6
#define H_LINE_STRAIGHT_MAX_CORRECTION_DECI_RPM  350
#endif
#define H_LINE_CONTROLLED_STOP_MS              80U
#define H_LINE_ATTITUDE_MAX_AGE_US           50000U
#define H_LINE_SCAN_STALE_MS                  60U
#define H_LINE_CALIBRATION_SAVE_DELAY_MS       750U
#define H_LINE_RUNTIME_WHITE_CAPTURE_MS         600U
#define H_LINE_RUNTIME_BLACK_CAPTURE_MS         600U
#define H_LINE_RUNTIME_WHITE_MINIMUM_SAMPLES     30U
#define H_LINE_RUNTIME_BLACK_MINIMUM_SAMPLES     30U
#define H_LINE_WHEEL_CIRCUMFERENCE_MM       204.2035f
#define H_AB_START_TARGET_DECI_RPM              200
#define H_AB_CRUISE_TARGET_DECI_RPM            1100
#define H_AB_RAMP_MS                            1800U
#define H_AB_TARGET_DISTANCE_MM               1500.0f
#define H_AB_TIMEOUT_MS                         7500U
#define H_AB_DERIVATIVE_LEAD_SCANS                 8
#define H_AB_MAX_CORRECTION_DECI_RPM             350
#define H_AB_STRAIGHT_DERIVATIVE_LEAD_SCANS         5
#define H_AB_STRAIGHT_MAX_CORRECTION_DECI_RPM     250
#define H_BALANCE_START_TARGET_DECI_RPM           200
#define H_BALANCE_CRUISE_TARGET_DECI_RPM          700
#define H_BALANCE_RAMP_MS                        2000U
#define H_BALANCE_TIMEOUT_MS                    29000U
#define H_BALANCE_DERIVATIVE_LEAD_SCANS             12
#define H_BALANCE_MAX_CORRECTION_DECI_RPM         500
#define H_BALANCE_STRAIGHT_DERIVATIVE_LEAD_SCANS     7
#define H_BALANCE_STRAIGHT_MAX_CORRECTION_DECI_RPM 300

static const int16_t s_line_sensor_position[H_MISSION_LINE_SENSOR_COUNT] = {
    -3500, -2500, -1500, -500, 500, 1500, 2500, 3500
};

volatile h_mission_diagnostics_t g_h_mission_diag;

static h_mission_context_t s_context[H_MISSION_COUNT];
static uint32_t s_line_request_sequence;
static uint32_t s_line_run_start_ms;
static uint32_t s_line_last_command_ms;
static uint32_t s_line_last_scan_sequence;
static uint32_t s_line_progress_last_ms;
static uint32_t s_line_calibration_full_ms;
static bool s_line_filter_initialized;
static bool s_line_calibration_collecting;
static bool s_line_yaw_initialized;
static int16_t s_line_previous_filtered_position;
static int16_t s_line_filtered_delta;
static int16_t s_line_last_correction_deci_rpm;
static float s_line_last_yaw_deg;
static uint8_t s_line_straight_streak;
static uint8_t s_line_speed_phase;
static uint8_t s_line_finish_masks[H_LINE_FINISH_WINDOW_SCANS];
static uint8_t s_line_finish_mask_index;
static float s_line_yaw_rate_dps;
static h_line_runtime_calibration_t s_line_runtime_calibration;

static int16_t HMission_ClampI16(int32_t value, int16_t minimum,
    int16_t maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return (int16_t) value;
}

static int16_t HMission_SlewLineCorrection(int32_t target,
    int16_t attack_step)
{
    int32_t current = s_line_last_correction_deci_rpm;
    int32_t current_magnitude = current < 0 ? -current : current;
    int32_t target_magnitude = target < 0 ? -target : target;
    bool same_direction = current == 0 || target == 0 ||
        ((current < 0) == (target < 0));
    int16_t step = same_direction && target_magnitude > current_magnitude ?
        attack_step :
        H_LINE_CORRECTION_RELEASE_SLEW_DECI_RPM;

    return HMission_ClampI16(target,
        (int16_t) (current - step), (int16_t) (current + step));
}

static float HMission_AbsFloat(float value)
{
    return (value < 0.0f) ? -value : value;
}

static bool HMission_IsStraightControlRegion(int32_t filtered_position)
{
    int32_t magnitude = filtered_position < 0 ?
        -filtered_position : filtered_position;

    return magnitude <= H_LINE_STRAIGHT_POSITION_MILLI &&
        s_line_yaw_rate_dps <= H_LINE_CRUISE_EXIT_YAW_RATE_DPS;
}

static bool HMission_IsLineFollowingMission(uint8_t slot)
{
    return slot == (uint8_t) H_MISSION_LINE_LAP ||
        slot == (uint8_t) H_MISSION_LAP_CENTER ||
        slot == (uint8_t) H_MISSION_LAP_HOLD;
}

static bool HMission_IsBalanceLapMission(uint8_t slot)
{
    return slot == (uint8_t) H_MISSION_LAP_CENTER ||
        slot == (uint8_t) H_MISSION_LAP_HOLD;
}

static h_mission_context_t *HMission_GetActiveLineMission(void)
{
    if (s_context[H_MISSION_LINE_LAP].running != 0U) {
        return &s_context[H_MISSION_LINE_LAP];
    }
    if (s_context[H_MISSION_LAP_CENTER].running != 0U) {
        return &s_context[H_MISSION_LAP_CENTER];
    }
    if (s_context[H_MISSION_LAP_HOLD].running != 0U) {
        return &s_context[H_MISSION_LAP_HOLD];
    }
    return NULL;
}

static float HMission_WrapDegrees(float angle_deg)
{
    while (angle_deg > 180.0f) {
        angle_deg -= 360.0f;
    }
    while (angle_deg < -180.0f) {
        angle_deg += 360.0f;
    }
    return angle_deg;
}

static void HMission_UpdateLineYawProgress(void)
{
    attitude_estimator_snapshot_t attitude;

    if (!AttitudeEstimator_GetSnapshot(&attitude) ||
        (attitude.flags & ATTITUDE_ESTIMATOR_FLAG_SOURCE_VALID) == 0U ||
        (uint32_t) (BSP_Time_GetUs() - attitude.timestamp_us) >
            H_LINE_ATTITUDE_MAX_AGE_US) {
        g_h_mission_diag.line_yaw_valid = 0U;
        s_line_yaw_initialized = false;
        s_line_yaw_rate_dps = H_LINE_CRUISE_EXIT_YAW_RATE_DPS;
        return;
    }
    g_h_mission_diag.line_yaw_valid = 1U;
    s_line_yaw_rate_dps = HMission_AbsFloat(attitude.axis_rate_dps[2]);
    if (!s_line_yaw_initialized) {
        s_line_last_yaw_deg = attitude.yaw_deg;
        s_line_yaw_initialized = true;
        return;
    }
    g_h_mission_diag.line_clockwise_yaw_deg -= HMission_WrapDegrees(
        attitude.yaw_deg - s_line_last_yaw_deg);
    s_line_last_yaw_deg = attitude.yaw_deg;
    if (g_h_mission_diag.line_clockwise_yaw_deg < 0.0f) {
        g_h_mission_diag.line_clockwise_yaw_deg = 0.0f;
    } else if (g_h_mission_diag.line_clockwise_yaw_deg > 720.0f) {
        g_h_mission_diag.line_clockwise_yaw_deg = 720.0f;
    }
}

static uint8_t HMission_CountBits(uint8_t value)
{
    uint8_t count = 0U;

    while (value != 0U) {
        count = (uint8_t) (count + (value & 0x01U));
        value >>= 1U;
    }
    return count;
}

static uint8_t HMission_MaximumRunBits(uint8_t value)
{
    uint8_t maximum = 0U;
    uint8_t current = 0U;

    while (value != 0U) {
        if ((value & 0x01U) != 0U) {
            current++;
            if (current > maximum) {
                maximum = current;
            }
        } else {
            current = 0U;
        }
        value >>= 1U;
    }
    return maximum;
}

static int16_t HMission_SelectBaseTarget(int32_t steering_error,
    uint32_t now_ms, uint8_t mission_slot)
{
    int32_t magnitude = steering_error < 0 ?
        -steering_error : steering_error;
    bool finish_slow =
        (g_h_mission_diag.line_progress_mm >=
                H_LINE_FINISH_SLOW_DISTANCE_MM &&
            g_h_mission_diag.line_yaw_valid != 0U &&
            g_h_mission_diag.line_clockwise_yaw_deg >=
                H_LINE_FINISH_SLOW_CLOCKWISE_DEG) ||
        g_h_mission_diag.line_progress_mm >=
            H_LINE_FINISH_SLOW_FALLBACK_MM;

    if (mission_slot == (uint8_t) H_MISSION_AB_CENTER &&
        g_h_mission_diag.ab_passed == 0U) {
        uint32_t elapsed_ms = now_ms - s_line_run_start_ms;

        if (elapsed_ms < H_AB_RAMP_MS) {
            int32_t target = H_AB_START_TARGET_DECI_RPM +
                (int32_t) ((uint32_t) (
                    H_AB_CRUISE_TARGET_DECI_RPM -
                    H_AB_START_TARGET_DECI_RPM) * elapsed_ms /
                    H_AB_RAMP_MS);

            s_line_speed_phase = (uint8_t) H_LINE_SPEED_LAUNCH;
            return (int16_t) target;
        }
        s_line_speed_phase = (uint8_t) H_LINE_SPEED_CRUISE;
        return H_AB_CRUISE_TARGET_DECI_RPM;
    }
    if (HMission_IsBalanceLapMission(mission_slot)) {
        uint32_t elapsed_ms = now_ms - s_line_run_start_ms;

        if (elapsed_ms < H_BALANCE_RAMP_MS) {
            int32_t target = H_BALANCE_START_TARGET_DECI_RPM +
                (int32_t) ((uint32_t) (
                    H_BALANCE_CRUISE_TARGET_DECI_RPM -
                    H_BALANCE_START_TARGET_DECI_RPM) * elapsed_ms /
                    H_BALANCE_RAMP_MS);

            s_line_speed_phase = (uint8_t) H_LINE_SPEED_LAUNCH;
            return (int16_t) target;
        }
        s_line_speed_phase = (uint8_t) H_LINE_SPEED_CRUISE;
        return H_BALANCE_CRUISE_TARGET_DECI_RPM;
    }
    if (finish_slow) {
        s_line_speed_phase = (uint8_t) H_LINE_SPEED_FINISH;
        s_line_straight_streak = 0U;
        return H_LINE_FINISH_TARGET_DECI_RPM;
    }
    if (g_h_mission_diag.line_progress_mm < H_LINE_LAUNCH_DISTANCE_MM) {
        s_line_speed_phase = (uint8_t) H_LINE_SPEED_LAUNCH;
        s_line_straight_streak = 0U;
        return H_LINE_LAUNCH_TARGET_DECI_RPM;
    }
    if (s_line_speed_phase == (uint8_t) H_LINE_SPEED_CRUISE) {
        if (magnitude > H_LINE_CRUISE_EXIT_ERROR_MILLI ||
            s_line_yaw_rate_dps > H_LINE_CRUISE_EXIT_YAW_RATE_DPS) {
            s_line_speed_phase = (uint8_t) H_LINE_SPEED_CURVE;
            s_line_straight_streak = 0U;
        }
    } else if (magnitude <= H_LINE_CRUISE_ENTER_ERROR_MILLI &&
        s_line_yaw_rate_dps <= H_LINE_CRUISE_ENTER_YAW_RATE_DPS) {
        if (s_line_straight_streak < UINT8_MAX) {
            s_line_straight_streak++;
        }
        if (s_line_straight_streak >= H_LINE_CRUISE_ENTER_SCANS) {
            s_line_speed_phase = (uint8_t) H_LINE_SPEED_CRUISE;
        }
    } else {
        s_line_straight_streak = 0U;
        s_line_speed_phase = (uint8_t) H_LINE_SPEED_CURVE;
    }
    return s_line_speed_phase == (uint8_t) H_LINE_SPEED_CRUISE ?
        H_LINE_CRUISE_TARGET_DECI_RPM : H_LINE_CURVE_TARGET_DECI_RPM;
}

static chassis_actuator_command_status_t HMission_StageLineSpeed(
    int16_t left_deci_rpm, int16_t right_deci_rpm)
{
    chassis_actuator_debug_request_t request;
    chassis_actuator_command_status_t status;

    memset(&request, 0, sizeof(request));
    s_line_request_sequence++;
    if (s_line_request_sequence == 0U) {
        s_line_request_sequence = 0x48000001UL;
    }
    request.magic = CHASSIS_ACTUATOR_DEBUG_MAGIC;
    request.magic_inverse = CHASSIS_ACTUATOR_DEBUG_MAGIC_INVERSE;
    request.sequence = s_line_request_sequence;
    request.left_electrical_permille = left_deci_rpm;
    request.right_electrical_permille = right_deci_rpm;
    request.duration_ms = 0U;
    request.reserved = (uint16_t) CHASSIS_ACTUATOR_MODE_SPEED;
    status = ChassisActuator_StageDebugRequest(&request);
    if (status == CHASSIS_ACTUATOR_COMMAND_STAGED) {
        g_h_mission_diag.line_command_accepted_count++;
    } else if (status == CHASSIS_ACTUATOR_COMMAND_BUSY) {
        g_h_mission_diag.line_command_busy_count++;
    } else {
        g_h_mission_diag.line_command_rejected_count++;
    }
    return status;
}

static void HMission_UpdateCalibration(
    const uint16_t raw[H_MISSION_LINE_SENSOR_COUNT])
{
    uint8_t channel;
    uint8_t mask = 0U;

    for (channel = 0U; channel < H_MISSION_LINE_SENSOR_COUNT; channel++) {
        uint16_t black = g_h_mission_diag.line_calibration_black[channel];
        uint16_t white = g_h_mission_diag.line_calibration_white[channel];

        if (raw[channel] < black) {
            black = raw[channel];
            g_h_mission_diag.line_calibration_black[channel] = black;
        }
        if (raw[channel] > white) {
            white = raw[channel];
            g_h_mission_diag.line_calibration_white[channel] = white;
        }
        if (white >= black && (uint16_t) (white - black) >=
            H_LINE_CALIBRATION_MINIMUM_SPAN) {
            mask |= (uint8_t) (1U << channel);
        }
    }
    g_h_mission_diag.line_calibration_mask = mask;
}

static bool HMission_CalibrationValid(
    const competition_reflectance_calibration_t *calibration)
{
    uint8_t channel;

    if (calibration == NULL || calibration->valid_mask != 0xFFU) {
        return false;
    }
    for (channel = 0U; channel < H_MISSION_LINE_SENSOR_COUNT; channel++) {
        if (calibration->white[channel] < calibration->black[channel] ||
            (uint16_t) (calibration->white[channel] -
                calibration->black[channel]) <
                H_LINE_CALIBRATION_MINIMUM_SPAN) {
            return false;
        }
    }
    return true;
}

static bool HMission_SaveCalibration(void)
{
    competition_reflectance_calibration_t calibration;
    uint8_t channel;

    memset(&calibration, 0, sizeof(calibration));
    calibration.valid_mask = g_h_mission_diag.line_calibration_mask;
    for (channel = 0U; channel < H_MISSION_LINE_SENSOR_COUNT; channel++) {
        calibration.black[channel] =
            g_h_mission_diag.line_calibration_black[channel];
        calibration.white[channel] =
            g_h_mission_diag.line_calibration_white[channel];
    }
    return HMission_CalibrationValid(&calibration) &&
        CompetitionStorage_SaveReflectanceCalibration(&calibration);
}

static void HMission_ApplyRuntimeCalibration(void)
{
    competition_reflectance_calibration_t *candidate =
        &s_line_runtime_calibration.candidate;
    uint8_t channel;
    uint8_t mask = 0U;

    for (channel = 0U; channel < H_MISSION_LINE_SENSOR_COUNT; channel++) {
        if (candidate->white[channel] >= candidate->black[channel] &&
            (uint16_t) (candidate->white[channel] -
                candidate->black[channel]) >=
                H_LINE_CALIBRATION_MINIMUM_SPAN) {
            mask |= (uint8_t) (1U << channel);
        }
    }
    candidate->valid_mask = mask;
    g_h_mission_diag.line_runtime_calibration_mask = mask;
    if (!HMission_CalibrationValid(candidate)) {
        g_h_mission_diag.line_runtime_calibration_failure_count++;
        g_h_mission_diag.line_runtime_calibration_state =
            (uint8_t) H_LINE_RUNTIME_CAL_FAILED;
        return;
    }
    for (channel = 0U; channel < H_MISSION_LINE_SENSOR_COUNT; channel++) {
        g_h_mission_diag.line_calibration_black[channel] =
            candidate->black[channel];
        g_h_mission_diag.line_calibration_white[channel] =
            candidate->white[channel];
    }
    g_h_mission_diag.line_calibration_mask = candidate->valid_mask;
    g_h_mission_diag.line_runtime_calibration_count++;
    g_h_mission_diag.line_runtime_calibration_applied = 1U;
    g_h_mission_diag.line_runtime_calibration_state =
        (uint8_t) H_LINE_RUNTIME_CAL_COMPLETE;
    s_line_calibration_collecting = false;
    s_line_filter_initialized = false;
}

static void HMission_UpdateRuntimeCalibrationCapture(
    const uint16_t raw[H_MISSION_LINE_SENSOR_COUNT], uint32_t now_ms)
{
    uint8_t state = g_h_mission_diag.line_runtime_calibration_state;
    uint8_t channel;

    if (state == (uint8_t) H_LINE_RUNTIME_CAL_CAPTURE_WHITE) {
        for (channel = 0U; channel < H_MISSION_LINE_SENSOR_COUNT;
            channel++) {
            s_line_runtime_calibration.white_sum[channel] += raw[channel];
        }
        if (s_line_runtime_calibration.sample_count < UINT16_MAX) {
            s_line_runtime_calibration.sample_count++;
        }
        g_h_mission_diag.line_runtime_calibration_samples =
            s_line_runtime_calibration.sample_count;
        if ((uint32_t) (now_ms -
                s_line_runtime_calibration.capture_start_ms) >=
                H_LINE_RUNTIME_WHITE_CAPTURE_MS) {
            if (s_line_runtime_calibration.sample_count <
                    H_LINE_RUNTIME_WHITE_MINIMUM_SAMPLES) {
                g_h_mission_diag.line_runtime_calibration_failure_count++;
                g_h_mission_diag.line_runtime_calibration_state =
                    (uint8_t) H_LINE_RUNTIME_CAL_FAILED;
                return;
            }
            for (channel = 0U; channel < H_MISSION_LINE_SENSOR_COUNT;
                channel++) {
                s_line_runtime_calibration.candidate.white[channel] =
                    (uint16_t) ((
                        s_line_runtime_calibration.white_sum[channel] +
                        s_line_runtime_calibration.sample_count / 2U) /
                        s_line_runtime_calibration.sample_count);
            }
            s_line_runtime_calibration.white_captured = 1U;
            g_h_mission_diag.line_runtime_white_captured = 1U;
            g_h_mission_diag.line_runtime_calibration_state =
                (uint8_t) H_LINE_RUNTIME_CAL_READY;
            g_h_mission_diag.line_runtime_calibration_samples = 0U;
            if (s_line_runtime_calibration.black_captured != 0U) {
                HMission_ApplyRuntimeCalibration();
            }
        }
    } else if (state == (uint8_t) H_LINE_RUNTIME_CAL_CAPTURE_BLACK) {
        for (channel = 0U; channel < H_MISSION_LINE_SENSOR_COUNT;
            channel++) {
            s_line_runtime_calibration.black_sum[channel] += raw[channel];
        }
        if (s_line_runtime_calibration.sample_count < UINT16_MAX) {
            s_line_runtime_calibration.sample_count++;
        }
        g_h_mission_diag.line_runtime_calibration_samples =
            s_line_runtime_calibration.sample_count;
        if ((uint32_t) (now_ms -
                s_line_runtime_calibration.capture_start_ms) >=
                H_LINE_RUNTIME_BLACK_CAPTURE_MS) {
            if (s_line_runtime_calibration.sample_count <
                    H_LINE_RUNTIME_BLACK_MINIMUM_SAMPLES) {
                g_h_mission_diag.line_runtime_calibration_failure_count++;
                g_h_mission_diag.line_runtime_calibration_state =
                    (uint8_t) H_LINE_RUNTIME_CAL_FAILED;
                return;
            }
            for (channel = 0U; channel < H_MISSION_LINE_SENSOR_COUNT;
                channel++) {
                s_line_runtime_calibration.candidate.black[channel] =
                    (uint16_t) ((
                        s_line_runtime_calibration.black_sum[channel] +
                        s_line_runtime_calibration.sample_count / 2U) /
                        s_line_runtime_calibration.sample_count);
            }
            s_line_runtime_calibration.black_captured = 1U;
            g_h_mission_diag.line_runtime_black_captured = 1U;
            g_h_mission_diag.line_runtime_calibration_state =
                (uint8_t) H_LINE_RUNTIME_CAL_READY;
            g_h_mission_diag.line_runtime_calibration_samples = 0U;
            if (s_line_runtime_calibration.white_captured != 0U) {
                HMission_ApplyRuntimeCalibration();
            }
        }
    }
}

static void HMission_UpdateLineEstimate(
    const uint16_t raw[H_MISSION_LINE_SENSOR_COUNT])
{
    uint32_t strength_sum = 0U;
    uint32_t selected_strength_sum = 0U;
    int32_t selected_weighted_sum = 0;
    int32_t selected_distance = INT32_MAX;
    uint8_t active_mask = 0U;
    uint8_t support_mask = 0U;
    uint8_t selected_mask = 0U;
    uint8_t cluster_count = 0U;
    uint8_t channel;

    for (channel = 0U; channel < H_MISSION_LINE_SENSOR_COUNT; channel++) {
        uint16_t black = g_h_mission_diag.line_calibration_black[channel];
        uint16_t white = g_h_mission_diag.line_calibration_white[channel];
        uint16_t span = white >= black ?
            (uint16_t) (white - black) : 0U;
        uint16_t strength = 0U;

        if (span >= H_LINE_CALIBRATION_MINIMUM_SPAN &&
            raw[channel] < white) {
            uint32_t delta = (uint32_t) white - raw[channel];

            strength = (uint16_t) ((delta * 1000U + span / 2U) / span);
            if (strength > 1000U) {
                strength = 1000U;
            }
        }
        g_h_mission_diag.line_strength_permille[channel] = strength;
        strength_sum += strength;
        if (strength >= H_LINE_ACTIVE_THRESHOLD_PERMILLE) {
            active_mask |= (uint8_t) (1U << channel);
        }
        if (strength >= H_LINE_CLUSTER_SUPPORT_PERMILLE) {
            support_mask |= (uint8_t) (1U << channel);
        }
    }

    channel = 0U;
    while (channel < H_MISSION_LINE_SENSOR_COUNT) {
        uint32_t cluster_strength_sum = 0U;
        int32_t cluster_weighted_sum = 0;
        int32_t cluster_position;
        int32_t cluster_distance;
        uint8_t cluster_mask = 0U;

        if ((support_mask & (uint8_t) (1U << channel)) == 0U) {
            channel++;
            continue;
        }
        while (channel < H_MISSION_LINE_SENSOR_COUNT &&
            (support_mask & (uint8_t) (1U << channel)) != 0U) {
            cluster_mask |= (uint8_t) (1U << channel);
            cluster_strength_sum +=
                g_h_mission_diag.line_strength_permille[channel];
            cluster_weighted_sum += (int32_t)
                g_h_mission_diag.line_strength_permille[channel] *
                s_line_sensor_position[channel];
            channel++;
        }
        if ((cluster_mask & active_mask) == 0U ||
            cluster_strength_sum == 0U) {
            continue;
        }
        cluster_count++;
        cluster_position = cluster_weighted_sum /
            (int32_t) cluster_strength_sum;
        if (s_line_filter_initialized) {
            cluster_distance = cluster_position -
                g_h_mission_diag.line_filtered_position_milli;
            if (cluster_distance < 0) {
                cluster_distance = -cluster_distance;
            }
        } else {
            cluster_distance = 0;
        }
        if (selected_mask == 0U ||
            (s_line_filter_initialized &&
             cluster_distance < selected_distance) ||
            ((!s_line_filter_initialized ||
              cluster_distance == selected_distance) &&
             cluster_strength_sum > selected_strength_sum)) {
            selected_mask = cluster_mask;
            selected_strength_sum = cluster_strength_sum;
            selected_weighted_sum = cluster_weighted_sum;
            selected_distance = cluster_distance;
        }
    }

    g_h_mission_diag.line_active_mask = active_mask;
    g_h_mission_diag.line_selected_mask = selected_mask;
    g_h_mission_diag.line_cluster_count = cluster_count;
    if (cluster_count > 1U) {
        g_h_mission_diag.line_ambiguous_scan_count++;
    }
    g_h_mission_diag.line_active_count = HMission_CountBits(active_mask);
    g_h_mission_diag.line_valid =
        g_h_mission_diag.line_calibration_mask == 0xFFU &&
        strength_sum >= H_LINE_MINIMUM_TOTAL_STRENGTH &&
        selected_mask != 0U;
    if (g_h_mission_diag.line_valid != 0U) {
        g_h_mission_diag.line_position_milli = (int16_t)
            (selected_weighted_sum / (int32_t) selected_strength_sum);
    }
}

static chassis_actuator_command_status_t HMission_UpdateLineTargets(
    uint32_t now_ms, uint8_t mission_slot)
{
    int32_t filtered;
    int32_t filtered_delta;
    int32_t derivative_lead_scans;
    int32_t derivative_term;
    int32_t steering_error;
    int32_t correction;
    int32_t correction_limit;
    int32_t correction_full_scale = 3500;
    int16_t correction_attack_step =
        H_LINE_CORRECTION_ATTACK_SLEW_DECI_RPM;
    int16_t base_target;
    int16_t left_target;
    int16_t right_target;
    bool straight_control;
    bool ab_segment;
    chassis_actuator_command_status_t status;

    if (!s_line_filter_initialized) {
        filtered = g_h_mission_diag.line_position_milli;
        s_line_previous_filtered_position = (int16_t) filtered;
        s_line_filtered_delta = 0;
        s_line_filter_initialized = true;
    } else {
        filtered = ((int32_t)
            g_h_mission_diag.line_filtered_position_milli * 3 +
            g_h_mission_diag.line_position_milli) / 4;
        filtered_delta = filtered - s_line_previous_filtered_position;
        s_line_filtered_delta = (int16_t) (((int32_t)
            s_line_filtered_delta * 3 + filtered_delta) / 4);
        s_line_previous_filtered_position = (int16_t) filtered;
    }
    g_h_mission_diag.line_filtered_position_milli = (int16_t) filtered;
    straight_control = HMission_IsStraightControlRegion(filtered);
    ab_segment = mission_slot == (uint8_t) H_MISSION_AB_CENTER &&
        g_h_mission_diag.ab_passed == 0U;
    if (ab_segment) {
        derivative_lead_scans = straight_control ?
            H_AB_STRAIGHT_DERIVATIVE_LEAD_SCANS :
            H_AB_DERIVATIVE_LEAD_SCANS;
    } else if (HMission_IsBalanceLapMission(mission_slot)) {
        derivative_lead_scans = straight_control ?
            H_BALANCE_STRAIGHT_DERIVATIVE_LEAD_SCANS :
            H_BALANCE_DERIVATIVE_LEAD_SCANS;
    } else {
        derivative_lead_scans =
            g_h_mission_diag.line_progress_mm < H_LINE_LAUNCH_DISTANCE_MM ?
            H_LINE_LAUNCH_DERIVATIVE_LEAD_SCANS :
            (straight_control ?
                H_LINE_STRAIGHT_DERIVATIVE_LEAD_SCANS :
                H_LINE_DERIVATIVE_LEAD_SCANS);
    }
    derivative_term = (int32_t) s_line_filtered_delta *
        derivative_lead_scans;
    derivative_term = HMission_ClampI16(derivative_term,
        -H_LINE_DERIVATIVE_LIMIT_MILLI,
        H_LINE_DERIVATIVE_LIMIT_MILLI);
    steering_error = HMission_ClampI16(filtered + derivative_term,
        -3500, 3500);
    if ((filtered > H_LINE_CENTER_DEADBAND_MILLI &&
            steering_error < 0) ||
        (filtered < -H_LINE_CENTER_DEADBAND_MILLI &&
            steering_error > 0)) {
#if ECHO_REFLECTANCE_BACKEND == ECHO_REFLECTANCE_BACKEND_I2C6
        steering_error = filtered / 2;
#else
        steering_error = 0;
#endif
    }
    if (steering_error > H_LINE_CENTER_DEADBAND_MILLI) {
        steering_error -= H_LINE_CENTER_DEADBAND_MILLI;
    } else if (steering_error < -H_LINE_CENTER_DEADBAND_MILLI) {
        steering_error += H_LINE_CENTER_DEADBAND_MILLI;
    } else {
        steering_error = 0;
    }
    g_h_mission_diag.line_derivative_milli =
        (int16_t) derivative_term;
    g_h_mission_diag.line_steering_error_milli =
        (int16_t) steering_error;
    base_target = HMission_SelectBaseTarget(steering_error, now_ms,
        mission_slot);
    g_h_mission_diag.line_base_target_deci_rpm = base_target;
    g_h_mission_diag.line_speed_phase = s_line_speed_phase;
    if (ab_segment) {
        correction_limit = straight_control ?
            H_AB_STRAIGHT_MAX_CORRECTION_DECI_RPM :
            H_AB_MAX_CORRECTION_DECI_RPM;
        if (correction_limit >
                base_target - H_LINE_MINIMUM_TARGET_DECI_RPM) {
            correction_limit =
                base_target - H_LINE_MINIMUM_TARGET_DECI_RPM;
        }
    } else if (HMission_IsBalanceLapMission(mission_slot)) {
        correction_limit = straight_control ?
            H_BALANCE_STRAIGHT_MAX_CORRECTION_DECI_RPM :
            H_BALANCE_MAX_CORRECTION_DECI_RPM;
        if (correction_limit >
                base_target - H_LINE_MINIMUM_TARGET_DECI_RPM) {
            correction_limit =
                base_target - H_LINE_MINIMUM_TARGET_DECI_RPM;
        }
    } else {
        correction_limit =
            g_h_mission_diag.line_progress_mm < H_LINE_LAUNCH_DISTANCE_MM ?
            H_LINE_LAUNCH_MAX_CORRECTION_DECI_RPM :
            (straight_control ?
                H_LINE_STRAIGHT_MAX_CORRECTION_DECI_RPM :
                H_LINE_MAXIMUM_CORRECTION_DECI_RPM);
#if ECHO_REFLECTANCE_BACKEND == ECHO_REFLECTANCE_BACKEND_I2C6
        if (mission_slot == (uint8_t) H_MISSION_LINE_LAP) {
            if (straight_control) {
                correction_attack_step =
                    H_LINE_STRAIGHT_ATTACK_SLEW_DECI_RPM;
            } else {
                correction_full_scale =
                    H_LINE_CURVE_STEERING_FULL_SCALE_MILLI;
            }
        }
#endif
    }
    correction = steering_error * correction_limit / correction_full_scale;
    correction = HMission_ClampI16(correction,
        (int16_t) -correction_limit,
        (int16_t) correction_limit);
    correction = HMission_SlewLineCorrection(correction,
        correction_attack_step);
    s_line_last_correction_deci_rpm = (int16_t) correction;
    left_target = HMission_ClampI16(
        base_target + correction,
        H_LINE_MINIMUM_TARGET_DECI_RPM,
        H_LINE_MAXIMUM_TARGET_DECI_RPM);
    right_target = HMission_ClampI16(
        base_target - correction,
        H_LINE_MINIMUM_TARGET_DECI_RPM,
        H_LINE_MAXIMUM_TARGET_DECI_RPM);
    g_h_mission_diag.line_correction_deci_rpm = (int16_t) correction;
    g_h_mission_diag.line_left_target_deci_rpm = left_target;
    g_h_mission_diag.line_right_target_deci_rpm = right_target;

    if ((uint32_t) (now_ms - s_line_last_command_ms) <
        H_LINE_COMMAND_PERIOD_MS) {
        return CHASSIS_ACTUATOR_COMMAND_BUSY;
    }
    status = HMission_StageLineSpeed(left_target, right_target);
    if (status == CHASSIS_ACTUATOR_COMMAND_STAGED) {
        s_line_last_command_ms = now_ms;
    }
    return status;
}

static bool HMission_Start(void *context)
{
    h_mission_context_t *mission = (h_mission_context_t *) context;

    if (mission == NULL || mission->slot >= H_MISSION_COUNT ||
        HMissionService_RuntimeCalibrationActive()) {
        return false;
    }
    if (mission->slot == (uint8_t) H_MISSION_AB_CENTER) {
        uint32_t now_us = BSP_Time_GetUs();

        ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_COMPLETE);
        if (!BallBalanceService_CanStartPositionHold(now_us) ||
            !BallBalanceService_RequestStartPositionHold(0)) {
            return false;
        }
    } else {
        ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_NONE);
    }
    if (mission->slot == (uint8_t) H_MISSION_BALL_STEP) {
        uint32_t now_us = BSP_Time_GetUs();

        if (!BallBalanceService_CanStartH3(now_us) ||
            !BallBalanceService_RequestStartH3()) {
            return false;
        }
    }
    if (HMission_IsLineFollowingMission(mission->slot)) {
        if (g_h_mission_diag.line_calibration_mask != 0xFFU ||
            g_h_mission_diag.line_valid == 0U) {
            return false;
        }
        g_h_mission_diag.line_lost_streak = 0U;
        g_h_mission_diag.line_finish_streak = 0U;
        g_h_mission_diag.line_start_clear_streak = 0U;
        g_h_mission_diag.line_start_cleared = 0U;
        g_h_mission_diag.line_finish_armed = 0U;
        g_h_mission_diag.line_progress_mm = 0.0f;
        g_h_mission_diag.line_clockwise_yaw_deg = 0.0f;
        g_h_mission_diag.line_yaw_valid = 0U;
        g_h_mission_diag.line_terminal_status = H_LINE_TERMINAL_NONE;
        g_h_mission_diag.line_braking = 0U;
        g_h_mission_diag.ab_elapsed_ms = 0U;
        g_h_mission_diag.ab_passed = 0U;
        g_h_mission_diag.balance_lap_elapsed_ms = 0U;
        g_h_mission_diag.line_finish_window_mask = 0U;
        g_h_mission_diag.line_finish_evidence_count = 0U;
        s_line_filter_initialized = false;
        s_line_last_correction_deci_rpm = 0;
        s_line_yaw_initialized = false;
        s_line_straight_streak = 0U;
        s_line_speed_phase = (uint8_t) H_LINE_SPEED_LAUNCH;
        s_line_yaw_rate_dps = H_LINE_CRUISE_EXIT_YAW_RATE_DPS;
        memset(s_line_finish_masks, 0, sizeof(s_line_finish_masks));
        s_line_finish_mask_index = 0U;
        s_line_run_start_ms = g_h_mission_diag.line_last_scan_ms;
        s_line_progress_last_ms = s_line_run_start_ms;
        s_line_last_command_ms = s_line_run_start_ms -
            H_LINE_COMMAND_PERIOD_MS;
        if (HMission_UpdateLineTargets(s_line_run_start_ms,
                mission->slot) !=
            CHASSIS_ACTUATOR_COMMAND_STAGED) {
            return false;
        }
    }
    mission->running = 1U;
    g_h_mission_diag.active_mission = mission->slot;
    g_h_mission_diag.start_count[mission->slot]++;
    return true;
}

static competition_mission_status_t HMission_Service(void *context,
    uint32_t now_ms)
{
    h_mission_context_t *mission = (h_mission_context_t *) context;

    if (mission == NULL || mission->slot >= H_MISSION_COUNT ||
        mission->running == 0U) {
        return COMPETITION_MISSION_FAULT;
    }
    g_h_mission_diag.service_count[mission->slot]++;
    if (mission->slot == (uint8_t) H_MISSION_BALL_STEP) {
        ball_balance_mission_status_t ball_status =
            BallBalanceService_GetMissionStatus();

        if (ball_status == BALL_BALANCE_MISSION_COMPLETE ||
            ball_status == BALL_BALANCE_MISSION_FAULT) {
            mission->running = 0U;
            g_h_mission_diag.active_mission = H_MISSION_COUNT;
            return ball_status == BALL_BALANCE_MISSION_COMPLETE ?
                COMPETITION_MISSION_COMPLETE : COMPETITION_MISSION_FAULT;
        }
        return COMPETITION_MISSION_RUNNING;
    }
    if (mission->slot == (uint8_t) H_MISSION_AB_CENTER) {
        ball_balance_mission_status_t ball_status;

        ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_COMPLETE);
        ball_status = BallBalanceService_GetMissionStatus();
        if (ball_status != BALL_BALANCE_MISSION_RUNNING) {
            mission->running = 0U;
            g_h_mission_diag.active_mission = H_MISSION_COUNT;
            return COMPETITION_MISSION_FAULT;
        }
        return COMPETITION_MISSION_RUNNING;
    }
    if (HMission_IsLineFollowingMission(mission->slot)) {
        if (mission->slot == (uint8_t) H_MISSION_AB_CENTER &&
            g_h_mission_diag.ab_passed == 0U) {
            g_h_mission_diag.ab_elapsed_ms = now_ms - s_line_run_start_ms;
            if (g_h_mission_diag.line_terminal_status ==
                    H_LINE_TERMINAL_NONE &&
                g_h_mission_diag.ab_elapsed_ms >= H_AB_TIMEOUT_MS) {
                g_h_mission_diag.ab_timeout_count++;
                g_h_mission_diag.line_terminal_status =
                    H_LINE_TERMINAL_FAULT;
                ChassisActuator_ForceSafe(
                    CHASSIS_ACTUATOR_STOP_REJECTED);
            }
        } else if (HMission_IsBalanceLapMission(mission->slot)) {
            g_h_mission_diag.balance_lap_elapsed_ms =
                now_ms - s_line_run_start_ms;
            if (g_h_mission_diag.line_terminal_status ==
                    H_LINE_TERMINAL_NONE &&
                g_h_mission_diag.balance_lap_elapsed_ms >=
                    H_BALANCE_TIMEOUT_MS) {
                g_h_mission_diag.balance_lap_timeout_count++;
                g_h_mission_diag.line_terminal_status =
                    H_LINE_TERMINAL_FAULT;
                ChassisActuator_ForceSafe(
                    CHASSIS_ACTUATOR_STOP_REJECTED);
            }
        }
        if (g_h_mission_diag.line_terminal_status ==
                H_LINE_TERMINAL_BRAKING) {
            if (g_chassis_actuator_diag.output_permitted != 0U ||
                g_chassis_actuator_diag.controlled_stop_active != 0U) {
                return COMPETITION_MISSION_RUNNING;
            }
            if (g_chassis_actuator_diag.last_stop_reason ==
                    (uint8_t) CHASSIS_ACTUATOR_STOP_COMPLETE) {
                g_h_mission_diag.line_terminal_status =
                    H_LINE_TERMINAL_COMPLETE;
                g_h_mission_diag.line_braking = 0U;
            } else {
                g_h_mission_diag.line_terminal_status =
                    H_LINE_TERMINAL_FAULT;
            }
        }
        if (g_h_mission_diag.line_terminal_status !=
            H_LINE_TERMINAL_NONE) {
            competition_mission_status_t status =
                g_h_mission_diag.line_terminal_status ==
                    H_LINE_TERMINAL_COMPLETE ?
                COMPETITION_MISSION_COMPLETE : COMPETITION_MISSION_FAULT;

            mission->running = 0U;
            g_h_mission_diag.active_mission = H_MISSION_COUNT;
            return status;
        }
        if ((uint32_t) (now_ms -
                g_h_mission_diag.line_last_scan_ms) > H_LINE_SCAN_STALE_MS) {
            g_h_mission_diag.line_terminal_status = H_LINE_TERMINAL_FAULT;
            ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_REJECTED);
            return COMPETITION_MISSION_FAULT;
        }
    }
    return COMPETITION_MISSION_RUNNING;
}

static void HMission_Stop(void *context)
{
    h_mission_context_t *mission = (h_mission_context_t *) context;
    uint8_t was_running;

    if (mission == NULL || mission->slot >= H_MISSION_COUNT) {
        return;
    }
    was_running = mission->running;
    mission->running = 0U;
    if (mission->slot == (uint8_t) H_MISSION_BALL_STEP ||
        mission->slot == (uint8_t) H_MISSION_AB_CENTER) {
        BallBalanceService_RequestAbort();
    }
    if (mission->slot == (uint8_t) H_MISSION_AB_CENTER) {
        ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_COMPLETE);
    }
    if (was_running != 0U && HMission_IsLineFollowingMission(mission->slot)) {
        g_h_mission_diag.line_terminal_status = H_LINE_TERMINAL_FAULT;
    }
    g_h_mission_diag.stop_count[mission->slot]++;
    g_h_mission_diag.active_mission = H_MISSION_COUNT;
}

bool HMissionService_BeginRuntimeCalibration(void)
{
    if (HMission_GetActiveLineMission() != NULL ||
        g_chassis_actuator_diag.output_permitted != 0U) {
        return false;
    }
    memset(&s_line_runtime_calibration, 0,
        sizeof(s_line_runtime_calibration));
    g_h_mission_diag.line_runtime_calibration_mask = 0U;
    g_h_mission_diag.line_runtime_calibration_samples = 0U;
    g_h_mission_diag.line_runtime_calibration_applied = 0U;
    g_h_mission_diag.line_runtime_white_captured = 0U;
    g_h_mission_diag.line_runtime_black_captured = 0U;
    g_h_mission_diag.line_runtime_calibration_state =
        (uint8_t) H_LINE_RUNTIME_CAL_READY;
    return true;
}

bool HMissionService_CaptureRuntimeWhite(uint32_t now_ms)
{
    uint8_t state = g_h_mission_diag.line_runtime_calibration_state;

    if (state != (uint8_t) H_LINE_RUNTIME_CAL_READY &&
        state != (uint8_t) H_LINE_RUNTIME_CAL_COMPLETE &&
        state != (uint8_t) H_LINE_RUNTIME_CAL_FAILED) {
        return false;
    }
    memset(s_line_runtime_calibration.white_sum, 0,
        sizeof(s_line_runtime_calibration.white_sum));
    s_line_runtime_calibration.white_captured = 0U;
    g_h_mission_diag.line_runtime_white_captured = 0U;
    s_line_runtime_calibration.sample_count = 0U;
    s_line_runtime_calibration.capture_start_ms = now_ms;
    g_h_mission_diag.line_runtime_calibration_samples = 0U;
    g_h_mission_diag.line_runtime_calibration_state =
        (uint8_t) H_LINE_RUNTIME_CAL_CAPTURE_WHITE;
    return true;
}

bool HMissionService_CaptureRuntimeBlack(uint32_t now_ms)
{
    uint8_t state = g_h_mission_diag.line_runtime_calibration_state;

    if (state != (uint8_t) H_LINE_RUNTIME_CAL_READY &&
        state != (uint8_t) H_LINE_RUNTIME_CAL_COMPLETE &&
        state != (uint8_t) H_LINE_RUNTIME_CAL_FAILED) {
        return false;
    }
    memset(s_line_runtime_calibration.black_sum, 0,
        sizeof(s_line_runtime_calibration.black_sum));
    s_line_runtime_calibration.black_captured = 0U;
    g_h_mission_diag.line_runtime_black_captured = 0U;
    s_line_runtime_calibration.sample_count = 0U;
    s_line_runtime_calibration.capture_start_ms = now_ms;
    g_h_mission_diag.line_runtime_calibration_samples = 0U;
    g_h_mission_diag.line_runtime_calibration_state =
        (uint8_t) H_LINE_RUNTIME_CAL_CAPTURE_BLACK;
    return true;
}

void HMissionService_AbortRuntimeCalibration(void)
{
    g_h_mission_diag.line_runtime_calibration_state =
        (uint8_t) H_LINE_RUNTIME_CAL_IDLE;
    g_h_mission_diag.line_runtime_calibration_samples = 0U;
}

bool HMissionService_RuntimeCalibrationActive(void)
{
    return g_h_mission_diag.line_runtime_calibration_state !=
        (uint8_t) H_LINE_RUNTIME_CAL_IDLE;
}

void HMissionService_Init(void)
{
    competition_reflectance_calibration_t calibration;
    uint8_t slot;

    memset((void *) &g_h_mission_diag, 0, sizeof(g_h_mission_diag));
    memset(s_context, 0, sizeof(s_context));
    memset(&s_line_runtime_calibration, 0,
        sizeof(s_line_runtime_calibration));
    for (slot = 0U; slot < H_MISSION_LINE_SENSOR_COUNT; slot++) {
        g_h_mission_diag.line_calibration_black[slot] = UINT16_MAX;
    }
    s_line_request_sequence = 0x48000000UL;
    s_line_run_start_ms = 0U;
    s_line_last_command_ms = 0U;
    s_line_last_scan_sequence = 0U;
    s_line_progress_last_ms = 0U;
    s_line_calibration_full_ms = 0U;
    s_line_filter_initialized = false;
    s_line_calibration_collecting = true;
    s_line_yaw_initialized = false;
    s_line_previous_filtered_position = 0;
    s_line_filtered_delta = 0;
    s_line_last_correction_deci_rpm = 0;
    s_line_last_yaw_deg = 0.0f;
    s_line_straight_streak = 0U;
    s_line_speed_phase = (uint8_t) H_LINE_SPEED_LAUNCH;
    s_line_yaw_rate_dps = H_LINE_CRUISE_EXIT_YAW_RATE_DPS;
    memset(s_line_finish_masks, 0, sizeof(s_line_finish_masks));
    s_line_finish_mask_index = 0U;
    g_h_mission_diag.line_runtime_calibration_state =
        (uint8_t) H_LINE_RUNTIME_CAL_IDLE;
    if (CompetitionStorage_LoadReflectanceCalibration(&calibration) &&
        HMission_CalibrationValid(&calibration)) {
        for (slot = 0U; slot < H_MISSION_LINE_SENSOR_COUNT; slot++) {
            g_h_mission_diag.line_calibration_black[slot] =
                calibration.black[slot];
            g_h_mission_diag.line_calibration_white[slot] =
                calibration.white[slot];
        }
        g_h_mission_diag.line_calibration_mask = calibration.valid_mask;
        g_h_mission_diag.line_calibration_loaded = 1U;
        g_h_mission_diag.line_calibration_saved = 1U;
        s_line_calibration_collecting = false;
    }
    g_h_mission_diag.active_mission = H_MISSION_COUNT;
    for (slot = 0U; slot < H_MISSION_COUNT; slot++) {
        competition_mission_t mission;

        s_context[slot].slot = slot;
        mission.start = HMission_Start;
        mission.service = HMission_Service;
        mission.stop = HMission_Stop;
        mission.context = &s_context[slot];
        if (!CompetitionService_RegisterMission(slot, &mission)) {
            return;
        }
    }
    g_h_mission_diag.initialized = 1U;
}

void HMissionService_ProcessReflectance(
    const uint16_t raw[H_MISSION_LINE_SENSOR_COUNT],
    uint32_t scan_sequence, uint32_t now_ms)
{
    h_mission_context_t *line_mission = HMission_GetActiveLineMission();

    if (raw == NULL || scan_sequence == s_line_last_scan_sequence) {
        return;
    }
    s_line_last_scan_sequence = scan_sequence;
    g_h_mission_diag.line_scan_count++;
    g_h_mission_diag.line_last_scan_ms = now_ms;
    HMission_UpdateRuntimeCalibrationCapture(raw, now_ms);
    if (line_mission == NULL && s_line_calibration_collecting &&
        !HMissionService_RuntimeCalibrationActive()) {
        uint8_t previous_mask =
            g_h_mission_diag.line_calibration_mask;

        HMission_UpdateCalibration(raw);
        if (previous_mask != 0xFFU &&
            g_h_mission_diag.line_calibration_mask == 0xFFU) {
            s_line_calibration_full_ms = now_ms;
        }
        if (g_h_mission_diag.line_calibration_mask == 0xFFU &&
            (uint32_t) (now_ms - s_line_calibration_full_ms) >=
                H_LINE_CALIBRATION_SAVE_DELAY_MS &&
            g_chassis_actuator_diag.output_permitted == 0U) {
            if (HMission_SaveCalibration()) {
                g_h_mission_diag.line_calibration_save_count++;
                g_h_mission_diag.line_calibration_saved = 1U;
                s_line_calibration_collecting = false;
            } else {
                g_h_mission_diag.line_calibration_save_failure_count++;
                s_line_calibration_full_ms = now_ms;
            }
        }
    }
    HMission_UpdateLineEstimate(raw);
    if (line_mission == NULL ||
        g_h_mission_diag.line_terminal_status != H_LINE_TERMINAL_NONE) {
        return;
    }

    if (g_chassis_actuator_diag.output_permitted != 0U) {
        uint32_t elapsed_ms = now_ms - s_line_progress_last_ms;

        if (elapsed_ms <= H_LINE_SCAN_STALE_MS) {
            float average_rpm = 0.5f * (
                HMission_AbsFloat(
                    g_chassis_actuator_diag.left_measured_rpm) +
                HMission_AbsFloat(
                    g_chassis_actuator_diag.right_measured_rpm));

            g_h_mission_diag.line_progress_mm += average_rpm *
                H_LINE_WHEEL_CIRCUMFERENCE_MM *
                (float) elapsed_ms / 60000.0f;
        }
    }
    s_line_progress_last_ms = now_ms;
    HMission_UpdateLineYawProgress();

    if (line_mission->slot == (uint8_t) H_MISSION_AB_CENTER &&
        g_h_mission_diag.ab_passed == 0U) {
        g_h_mission_diag.ab_elapsed_ms = now_ms - s_line_run_start_ms;
        if (g_h_mission_diag.line_progress_mm >=
                H_AB_TARGET_DISTANCE_MM) {
            g_h_mission_diag.ab_passed = 1U;
            g_h_mission_diag.ab_pass_count++;
        } else if (g_h_mission_diag.line_valid == 0U) {
            if (g_h_mission_diag.line_lost_streak < UINT8_MAX) {
                g_h_mission_diag.line_lost_streak++;
            }
            if (g_h_mission_diag.line_lost_streak >=
                    H_LINE_LOST_STOP_SCANS) {
                g_h_mission_diag.line_lost_stop_count++;
                g_h_mission_diag.line_terminal_status =
                    H_LINE_TERMINAL_FAULT;
                ChassisActuator_ForceSafe(
                    CHASSIS_ACTUATOR_STOP_REJECTED);
            }
            return;
        } else {
            g_h_mission_diag.line_lost_streak = 0U;
            (void) HMission_UpdateLineTargets(now_ms,
                line_mission->slot);
            return;
        }
    }

    if (g_h_mission_diag.line_start_cleared == 0U) {
        if (g_h_mission_diag.line_valid != 0U &&
            g_h_mission_diag.line_active_count <=
                H_LINE_START_CLEAR_MAXIMUM_ACTIVE) {
            if (g_h_mission_diag.line_start_clear_streak < UINT8_MAX) {
                g_h_mission_diag.line_start_clear_streak++;
            }
            if (g_h_mission_diag.line_start_clear_streak >=
                H_LINE_START_CLEAR_CONFIRM_SCANS) {
                g_h_mission_diag.line_start_cleared = 1U;
            }
        } else {
            g_h_mission_diag.line_start_clear_streak = 0U;
        }
    }
    if (g_h_mission_diag.line_start_cleared != 0U &&
        ((g_h_mission_diag.line_progress_mm >=
                H_LINE_FINISH_ARM_DISTANCE_MM &&
          g_h_mission_diag.line_clockwise_yaw_deg >=
                H_LINE_FINISH_ARM_CLOCKWISE_DEG) ||
         g_h_mission_diag.line_progress_mm >=
                H_LINE_FINISH_FALLBACK_DISTANCE_MM)) {
        g_h_mission_diag.line_finish_armed = 1U;
    }

    if (g_h_mission_diag.line_finish_armed != 0U) {
        uint8_t finish_union = 0U;
        uint8_t strong_count = 0U;
        uint8_t index;

        s_line_finish_masks[s_line_finish_mask_index] =
            g_h_mission_diag.line_active_mask;
        s_line_finish_mask_index = (uint8_t) (
            (s_line_finish_mask_index + 1U) % H_LINE_FINISH_WINDOW_SCANS);
        for (index = 0U; index < H_LINE_FINISH_WINDOW_SCANS; index++) {
            finish_union |= s_line_finish_masks[index];
            if (HMission_MaximumRunBits(s_line_finish_masks[index]) >=
                    H_LINE_FINISH_STRONG_RUN) {
                strong_count++;
            }
        }
        g_h_mission_diag.line_finish_window_mask = finish_union;
        g_h_mission_diag.line_finish_evidence_count = strong_count;
        if (strong_count >= H_LINE_FINISH_STRONG_CONFIRM) {
            g_h_mission_diag.line_finish_streak = 1U;
        } else {
            g_h_mission_diag.line_finish_streak = 0U;
        }
    } else {
        memset(s_line_finish_masks, 0, sizeof(s_line_finish_masks));
        g_h_mission_diag.line_finish_streak = 0U;
        g_h_mission_diag.line_finish_window_mask = 0U;
        g_h_mission_diag.line_finish_evidence_count = 0U;
    }
    if (g_h_mission_diag.line_finish_streak != 0U) {
        g_h_mission_diag.line_finish_count++;
        if (!ChassisActuator_RequestControlledStop(
                H_LINE_CONTROLLED_STOP_MS)) {
            g_h_mission_diag.line_terminal_status = H_LINE_TERMINAL_FAULT;
            ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_REJECTED);
            return;
        }
        g_h_mission_diag.line_terminal_status = H_LINE_TERMINAL_BRAKING;
        g_h_mission_diag.line_braking = 1U;
        return;
    }

    if (g_h_mission_diag.line_valid == 0U) {
        if (g_h_mission_diag.line_lost_streak < UINT8_MAX) {
            g_h_mission_diag.line_lost_streak++;
        }
        if (g_h_mission_diag.line_lost_streak >=
            H_LINE_LOST_STOP_SCANS) {
            g_h_mission_diag.line_lost_stop_count++;
            g_h_mission_diag.line_terminal_status = H_LINE_TERMINAL_FAULT;
            ChassisActuator_ForceSafe(CHASSIS_ACTUATOR_STOP_REJECTED);
        }
        return;
    }
    g_h_mission_diag.line_lost_streak = 0U;
    (void) HMission_UpdateLineTargets(now_ms, line_mission->slot);
}

const char *HMissionService_Code(uint8_t slot)
{
    static const char *const codes[H_MISSION_COUNT] = {
        "H2", "H3", "H4", "H5", "H6"
    };

    return slot < H_MISSION_COUNT ? codes[slot] : "H?";
}

const char *HMissionService_Name(uint8_t slot)
{
    static const char *const names[H_MISSION_COUNT] = {
        "LINE LAP",
        "BALL STEP",
        "BALL CENTER",
        "LAP CENTER",
        "LAP HOLD"
    };

    return slot < H_MISSION_COUNT ? names[slot] : "UNKNOWN";
}
