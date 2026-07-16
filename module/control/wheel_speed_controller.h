#ifndef ECHO_WHEEL_SPEED_CONTROLLER_H
#define ECHO_WHEEL_SPEED_CONTROLLER_H

#include <stdint.h>

typedef struct {
    float kp;
    float ki;
    float kd;
    float integrator_limit_permille;
    float load_release_unwind_gain;
    float load_release_threshold_rpm;
    float load_release_output_slew_down_permille_per_s;
    float output_limit_permille;
    float feedforward_offset_permille;
    float feedforward_gain_permille_per_rpm;
    float measurement_filter_alpha;
    float target_slew_rpm_per_s;
    float output_slew_up_permille_per_s;
    float output_slew_down_permille_per_s;
} wheel_speed_controller_config_t;

typedef struct {
    wheel_speed_controller_config_t config;
    float filtered_rpm;
    float previous_filtered_rpm;
    float requested_target_rpm;
    float ramped_target_rpm;
    float integrator_permille;
    float proportional_permille;
    float derivative_permille;
    float feedforward_permille;
    float output_permille;
    float error_rpm;
    uint32_t observation_count;
    uint32_t update_count;
    uint32_t saturation_count;
    uint32_t anti_windup_count;
    uint32_t integrator_hold_count;
    uint32_t load_release_arm_count;
    uint32_t load_release_unwind_count;
    uint32_t output_slew_count;
    uint8_t initialized;
    uint8_t load_release_armed;
    uint8_t load_release_decay_active;
    uint8_t reserved;
} wheel_speed_controller_t;

void WheelSpeedController_Init(wheel_speed_controller_t *controller,
    const wheel_speed_controller_config_t *config);
void WheelSpeedController_Reset(wheel_speed_controller_t *controller);
void WheelSpeedController_Observe(wheel_speed_controller_t *controller,
    float measured_rpm);
void WheelSpeedController_PrimeOutput(wheel_speed_controller_t *controller,
    float output_permille);
int16_t WheelSpeedController_Update(wheel_speed_controller_t *controller,
    float target_rpm, float measured_rpm, float period_s);

#endif
