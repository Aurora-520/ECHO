#include "motor_profile.h"

#include <stddef.h>
#include <string.h>

#include "motor_profile_config.h"

#define MOTOR_PROFILE_MG370_VERSION 13U
#define MOTOR_PROFILE_513X_VERSION  1U

#if ECHO_MOTOR_PROFILE_SELECTION == ECHO_MOTOR_PROFILE_MG370

static const motor_profile_t s_active_profile = {
    MOTOR_PROFILE_SCHEMA_VERSION,
    MOTOR_PROFILE_ID_MG370,
    MOTOR_PROFILE_MG370_VERSION,
    0U,
    "MG370",
    MOTOR_PROFILE_VALID_RATED_VOLTAGE |
        MOTOR_PROFILE_VALID_RATED_CURRENT |
        MOTOR_PROFILE_VALID_STALL_CURRENT |
        MOTOR_PROFILE_VALID_GEAR_RATIO |
        MOTOR_PROFILE_VALID_ENCODER_INTERFACE |
        MOTOR_PROFILE_VALID_ENCODER_LEVEL |
        MOTOR_PROFILE_VALID_ENCODER_PPR |
        MOTOR_PROFILE_VALID_MAXIMUM_RPM |
        MOTOR_PROFILE_VALID_LEFT_CPR |
        MOTOR_PROFILE_VALID_RIGHT_CPR |
        MOTOR_PROFILE_VALID_START_PWM |
        MOTOR_PROFILE_VALID_MAXIMUM_PWM |
        MOTOR_PROFILE_VALID_SPEED_LIMIT |
        MOTOR_PROFILE_VALID_ACCELERATION_LIMIT |
        MOTOR_PROFILE_VALID_SPEED_PID |
        MOTOR_PROFILE_VALID_MOTOR_OUTPUT_SIGNS,
    MOTOR_PROFILE_FLAG_CPR_PROVISIONAL |
        MOTOR_PROFILE_FLAG_ACTUATOR_TEST_READY |
        MOTOR_PROFILE_FLAG_CLOSED_LOOP_READY,
    12000U,
    1100U,
    6200U,
    34.014f,
    500U,
    3300U,
    300U,
    500U,
    900U,
    120.0f,
    200.0f,
    0U,
    0U,
    MOTOR_ENCODER_INTERFACE_GMR_AB,
    { 8.0f, 18.0f, 0.0f, 160.0f, 10.0f, 1.5f, 6000.0f, 900.0f,
      315.0f, 320.0f, 3.8f, 3.6f, 0.35f,
      350.0f, 3000.0f, 1500.0f, 600U, 40U, 200U, 4.0f },
    {
        { 1, 1, 4U, 0U, 68028U },
        { -1, -1, 1U, 0U, 17007U }
    }
};

#elif ECHO_MOTOR_PROFILE_SELECTION == ECHO_MOTOR_PROFILE_513X

#error "ECHO 513X profile is locked: confirm rated voltage, stall current, gear ratio, encoder interface, signal level, and PPR before selecting ECHO_MOTOR_PROFILE_513X"

static const motor_profile_t s_active_profile = {
    MOTOR_PROFILE_SCHEMA_VERSION,
    MOTOR_PROFILE_ID_513X,
    MOTOR_PROFILE_513X_VERSION,
    0U,
    "513X",
    0U,
    MOTOR_PROFILE_FLAG_PLACEHOLDER,
    0U, 0U, 0U, 0.0f, 0U, 0U, 0U,
    0U, 0U, 0.0f, 0.0f, 0U, 0U,
    MOTOR_ENCODER_INTERFACE_UNKNOWN,
    { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 0U, 0U, 0U, 0.0f },
    {
        { 0, 0, 0U, 0U, 0U },
        { 0, 0, 0U, 0U, 0U }
    }
};

#else

#error "Unsupported ECHO_MOTOR_PROFILE_SELECTION. Use ECHO_MOTOR_PROFILE_MG370 or ECHO_MOTOR_PROFILE_513X."

#endif

volatile motor_profile_diagnostics_t g_motor_profile_diag;

static bool MotorProfile_WheelIsValid(const motor_wheel_profile_t *wheel)
{
    return wheel != NULL &&
        (wheel->encoder_count_sign == 1 ||
         wheel->encoder_count_sign == -1) &&
        (wheel->encoder_decode_multiplier == 1U ||
         wheel->encoder_decode_multiplier == 2U ||
         wheel->encoder_decode_multiplier == 4U) &&
        wheel->counts_per_output_revolution != 0U;
}

static bool MotorProfile_MotorSignsAreValid(void)
{
    const motor_wheel_profile_t *left =
        &s_active_profile.wheel[MOTOR_WHEEL_LEFT];
    const motor_wheel_profile_t *right =
        &s_active_profile.wheel[MOTOR_WHEEL_RIGHT];

    return (left->motor_output_sign == 1 ||
            left->motor_output_sign == -1) &&
        (right->motor_output_sign == 1 ||
         right->motor_output_sign == -1);
}

static bool MotorProfile_ClosedLoopFieldsAreValid(void)
{
    const uint32_t required_fields =
        MOTOR_PROFILE_VALID_START_PWM |
        MOTOR_PROFILE_VALID_MAXIMUM_PWM |
        MOTOR_PROFILE_VALID_SPEED_LIMIT |
        MOTOR_PROFILE_VALID_ACCELERATION_LIMIT |
        MOTOR_PROFILE_VALID_SPEED_PID |
        MOTOR_PROFILE_VALID_MOTOR_OUTPUT_SIGNS;

    return (s_active_profile.valid_fields & required_fields) ==
            required_fields &&
        MotorProfile_MotorSignsAreValid() &&
        s_active_profile.start_pwm_permille != 0U &&
        s_active_profile.maximum_pwm_permille >=
            s_active_profile.start_pwm_permille &&
        s_active_profile.speed_limit_rpm > 0.0f &&
        s_active_profile.acceleration_limit_rpm_per_s > 0.0f &&
        s_active_profile.speed_pid.output_limit_permille > 0.0f &&
        s_active_profile.speed_pid.recovery_boost_permille != 0U &&
        (float) s_active_profile.speed_pid.recovery_boost_permille <=
            s_active_profile.speed_pid.output_limit_permille &&
        s_active_profile.speed_pid.boost_minimum_ms != 0U &&
        s_active_profile.speed_pid.boost_maximum_ms >=
            s_active_profile.speed_pid.boost_minimum_ms;
}

void MotorProfile_Init(void)
{
    const motor_wheel_profile_t *left =
        &s_active_profile.wheel[MOTOR_WHEEL_LEFT];
    const motor_wheel_profile_t *right =
        &s_active_profile.wheel[MOTOR_WHEEL_RIGHT];
    bool selection_valid;

    memset((void *) &g_motor_profile_diag, 0,
        sizeof(g_motor_profile_diag));
    selection_valid =
        s_active_profile.schema_version == MOTOR_PROFILE_SCHEMA_VERSION &&
        s_active_profile.profile_id != MOTOR_PROFILE_ID_NONE &&
        s_active_profile.rated_voltage_mv != 0U &&
        s_active_profile.gear_ratio > 0.0f &&
        s_active_profile.encoder_ppr != 0U &&
        MotorProfile_WheelIsValid(left) &&
        MotorProfile_WheelIsValid(right);

    g_motor_profile_diag.magic = MOTOR_PROFILE_DIAGNOSTICS_MAGIC;
    g_motor_profile_diag.model_name = s_active_profile.model_name;
    g_motor_profile_diag.valid_fields = s_active_profile.valid_fields;
    g_motor_profile_diag.status_flags = s_active_profile.status_flags;
    g_motor_profile_diag.left_counts_per_revolution =
        left->counts_per_output_revolution;
    g_motor_profile_diag.right_counts_per_revolution =
        right->counts_per_output_revolution;
    g_motor_profile_diag.schema_version = s_active_profile.schema_version;
    g_motor_profile_diag.profile_id = s_active_profile.profile_id;
    g_motor_profile_diag.profile_version = s_active_profile.profile_version;
    g_motor_profile_diag.left_motor_output_sign = left->motor_output_sign;
    g_motor_profile_diag.right_motor_output_sign = right->motor_output_sign;
    g_motor_profile_diag.left_encoder_count_sign =
        left->encoder_count_sign;
    g_motor_profile_diag.right_encoder_count_sign =
        right->encoder_count_sign;
    g_motor_profile_diag.left_decode_multiplier =
        left->encoder_decode_multiplier;
    g_motor_profile_diag.right_decode_multiplier =
        right->encoder_decode_multiplier;
    g_motor_profile_diag.selection_valid = selection_valid ? 1U : 0U;
    g_motor_profile_diag.actuator_test_ready =
        (selection_valid &&
         (s_active_profile.status_flags &
          MOTOR_PROFILE_FLAG_ACTUATOR_TEST_READY) != 0U) ? 1U : 0U;
    g_motor_profile_diag.closed_loop_ready =
        (selection_valid &&
         (s_active_profile.status_flags &
          MOTOR_PROFILE_FLAG_CLOSED_LOOP_READY) != 0U &&
         MotorProfile_ClosedLoopFieldsAreValid()) ? 1U : 0U;
    g_motor_profile_diag.output_locked =
        (g_motor_profile_diag.actuator_test_ready == 0U) ? 1U : 0U;
    g_motor_profile_diag.initialized = 1U;
}

const motor_profile_t *MotorProfile_GetActive(void)
{
    return &s_active_profile;
}

const motor_wheel_profile_t *MotorProfile_GetWheel(motor_wheel_t wheel)
{
    if ((uint32_t) wheel >= (uint32_t) MOTOR_WHEEL_COUNT) {
        return NULL;
    }
    return &s_active_profile.wheel[(uint32_t) wheel];
}

const motor_speed_pid_config_t *MotorProfile_GetSpeedPidConfig(void)
{
    return &s_active_profile.speed_pid;
}

const char *MotorProfile_GetModelName(void)
{
    return s_active_profile.model_name;
}

bool MotorProfile_ActuatorTestReady(void)
{
    return g_motor_profile_diag.initialized != 0U &&
        g_motor_profile_diag.actuator_test_ready != 0U &&
        g_motor_profile_diag.output_locked == 0U;
}

bool MotorProfile_ClosedLoopReady(void)
{
    return g_motor_profile_diag.initialized != 0U &&
        g_motor_profile_diag.closed_loop_ready != 0U &&
        g_motor_profile_diag.output_locked == 0U;
}

bool MotorProfile_NormalizeMotorPermille(motor_wheel_t wheel,
    int16_t normalized_permille, int16_t *electrical_permille)
{
    const motor_wheel_profile_t *wheel_profile =
        MotorProfile_GetWheel(wheel);
    uint16_t magnitude;

    if (electrical_permille == NULL || wheel_profile == NULL ||
        !MotorProfile_ClosedLoopReady() ||
        normalized_permille < -1000 || normalized_permille > 1000 ||
        (wheel_profile->motor_output_sign != 1 &&
         wheel_profile->motor_output_sign != -1)) {
        return false;
    }
    magnitude = (normalized_permille < 0) ?
        (uint16_t) (-normalized_permille) :
        (uint16_t) normalized_permille;
    if (magnitude > s_active_profile.maximum_pwm_permille) {
        return false;
    }
    *electrical_permille = (int16_t) (
        normalized_permille * wheel_profile->motor_output_sign);
    return true;
}

void MotorProfile_UpdateEncoderSpeeds(int32_t left_delta_counts,
    int32_t right_delta_counts, uint32_t period_us)
{
    const motor_wheel_profile_t *left =
        &s_active_profile.wheel[MOTOR_WHEEL_LEFT];
    const motor_wheel_profile_t *right =
        &s_active_profile.wheel[MOTOR_WHEEL_RIGHT];
    const float microseconds_per_minute = 60000000.0f;

    if (period_us == 0U || g_motor_profile_diag.selection_valid == 0U ||
        left->counts_per_output_revolution == 0U ||
        right->counts_per_output_revolution == 0U) {
        g_motor_profile_diag.left_output_rpm = 0.0f;
        g_motor_profile_diag.right_output_rpm = 0.0f;
        g_motor_profile_diag.invalid_speed_sample_count++;
        return;
    }

    g_motor_profile_diag.left_output_rpm =
        ((float) left_delta_counts * (float) left->encoder_count_sign *
         microseconds_per_minute) /
        ((float) left->counts_per_output_revolution * (float) period_us);
    g_motor_profile_diag.right_output_rpm =
        ((float) right_delta_counts * (float) right->encoder_count_sign *
         microseconds_per_minute) /
        ((float) right->counts_per_output_revolution * (float) period_us);
    g_motor_profile_diag.speed_sample_count++;
}
