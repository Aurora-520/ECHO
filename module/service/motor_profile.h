#ifndef ECHO_MOTOR_PROFILE_H
#define ECHO_MOTOR_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#define MOTOR_PROFILE_DIAGNOSTICS_MAGIC 0x4D505246UL
#define MOTOR_PROFILE_SCHEMA_VERSION   1U

#define MOTOR_PROFILE_VALID_RATED_VOLTAGE       (1UL << 0)
#define MOTOR_PROFILE_VALID_RATED_CURRENT       (1UL << 1)
#define MOTOR_PROFILE_VALID_STALL_CURRENT       (1UL << 2)
#define MOTOR_PROFILE_VALID_GEAR_RATIO          (1UL << 3)
#define MOTOR_PROFILE_VALID_ENCODER_INTERFACE   (1UL << 4)
#define MOTOR_PROFILE_VALID_ENCODER_LEVEL       (1UL << 5)
#define MOTOR_PROFILE_VALID_ENCODER_PPR         (1UL << 6)
#define MOTOR_PROFILE_VALID_MAXIMUM_RPM         (1UL << 7)
#define MOTOR_PROFILE_VALID_LEFT_CPR            (1UL << 8)
#define MOTOR_PROFILE_VALID_RIGHT_CPR           (1UL << 9)
#define MOTOR_PROFILE_VALID_START_PWM           (1UL << 10)
#define MOTOR_PROFILE_VALID_MAXIMUM_PWM         (1UL << 11)
#define MOTOR_PROFILE_VALID_SPEED_LIMIT         (1UL << 12)
#define MOTOR_PROFILE_VALID_ACCELERATION_LIMIT  (1UL << 13)
#define MOTOR_PROFILE_VALID_STALL_THRESHOLD     (1UL << 14)
#define MOTOR_PROFILE_VALID_SPEED_PID           (1UL << 15)
#define MOTOR_PROFILE_VALID_MOTOR_OUTPUT_SIGNS  (1UL << 16)

#define MOTOR_PROFILE_FLAG_PLACEHOLDER          (1UL << 0)
#define MOTOR_PROFILE_FLAG_CPR_PROVISIONAL      (1UL << 1)
#define MOTOR_PROFILE_FLAG_ACTUATOR_TEST_READY  (1UL << 2)
#define MOTOR_PROFILE_FLAG_CLOSED_LOOP_READY    (1UL << 3)
#define MOTOR_PROFILE_FLAG_MOTOR_SIGNS_PENDING  (1UL << 4)
#define MOTOR_PROFILE_FLAG_LIMITS_PENDING       (1UL << 5)
#define MOTOR_PROFILE_FLAG_PID_PENDING          (1UL << 6)

typedef enum {
    MOTOR_PROFILE_ID_NONE = 0U,
    MOTOR_PROFILE_ID_MG370 = 1U,
    MOTOR_PROFILE_ID_513X = 2U
} motor_profile_id_t;

typedef enum {
    MOTOR_WHEEL_LEFT = 0U,
    MOTOR_WHEEL_RIGHT = 1U,
    MOTOR_WHEEL_COUNT
} motor_wheel_t;

typedef enum {
    MOTOR_ENCODER_INTERFACE_UNKNOWN = 0U,
    MOTOR_ENCODER_INTERFACE_GMR_AB = 1U
} motor_encoder_interface_t;

typedef struct {
    float kp;
    float ki;
    float kd;
    float integrator_limit;
    float load_release_unwind_gain;
    float load_release_threshold_rpm;
    float load_release_output_slew_down_permille_per_s;
    float output_limit_permille;
    float left_feedforward_offset_permille;
    float right_feedforward_offset_permille;
    float left_feedforward_gain_permille_per_rpm;
    float right_feedforward_gain_permille_per_rpm;
    float measurement_filter_alpha;
    float target_slew_rpm_per_s;
    float output_slew_up_permille_per_s;
    float output_slew_down_permille_per_s;
    uint16_t recovery_boost_permille;
    uint16_t boost_minimum_ms;
    uint16_t boost_maximum_ms;
    float boost_release_rpm;
} motor_speed_pid_config_t;

typedef struct {
    int8_t motor_output_sign;
    int8_t encoder_count_sign;
    uint8_t encoder_decode_multiplier;
    uint8_t reserved;
    uint32_t counts_per_output_revolution;
} motor_wheel_profile_t;

typedef struct {
    uint16_t schema_version;
    uint16_t profile_id;
    uint16_t profile_version;
    uint16_t reserved;
    const char *model_name;
    uint32_t valid_fields;
    uint32_t status_flags;
    uint32_t rated_voltage_mv;
    uint32_t rated_current_ma;
    uint32_t stall_current_ma;
    float gear_ratio;
    uint32_t encoder_ppr;
    uint32_t encoder_signal_mv;
    uint32_t maximum_output_rpm;
    uint16_t start_pwm_permille;
    uint16_t maximum_pwm_permille;
    float speed_limit_rpm;
    float acceleration_limit_rpm_per_s;
    uint32_t stall_current_threshold_ma;
    uint32_t stall_timeout_ms;
    motor_encoder_interface_t encoder_interface;
    motor_speed_pid_config_t speed_pid;
    motor_wheel_profile_t wheel[MOTOR_WHEEL_COUNT];
} motor_profile_t;

typedef struct {
    uint32_t magic;
    const char *model_name;
    uint32_t valid_fields;
    uint32_t status_flags;
    uint32_t speed_sample_count;
    uint32_t invalid_speed_sample_count;
    float left_output_rpm;
    float right_output_rpm;
    uint32_t left_counts_per_revolution;
    uint32_t right_counts_per_revolution;
    uint16_t schema_version;
    uint16_t profile_id;
    uint16_t profile_version;
    int8_t left_motor_output_sign;
    int8_t right_motor_output_sign;
    int8_t left_encoder_count_sign;
    int8_t right_encoder_count_sign;
    uint8_t left_decode_multiplier;
    uint8_t right_decode_multiplier;
    uint8_t selection_valid;
    uint8_t actuator_test_ready;
    uint8_t closed_loop_ready;
    uint8_t output_locked;
    uint8_t initialized;
    uint8_t reserved[3];
} motor_profile_diagnostics_t;

extern volatile motor_profile_diagnostics_t g_motor_profile_diag;

void MotorProfile_Init(void);
const motor_profile_t *MotorProfile_GetActive(void);
const motor_wheel_profile_t *MotorProfile_GetWheel(motor_wheel_t wheel);
const motor_speed_pid_config_t *MotorProfile_GetSpeedPidConfig(void);
const char *MotorProfile_GetModelName(void);
bool MotorProfile_ActuatorTestReady(void);
bool MotorProfile_ClosedLoopReady(void);
bool MotorProfile_NormalizeMotorPermille(motor_wheel_t wheel,
    int16_t normalized_permille, int16_t *electrical_permille);
void MotorProfile_UpdateEncoderSpeeds(int32_t left_delta_counts,
    int32_t right_delta_counts, uint32_t period_us);

#endif
