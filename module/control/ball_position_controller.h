#ifndef ECHO_BALL_POSITION_CONTROLLER_H
#define ECHO_BALL_POSITION_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float position_velocity_gain_per_s;
    float negative_position_velocity_gain_per_s;
    float maximum_velocity_mm_s;
    float search_integral_gain_millidegrees_per_mm_s;
    float maximum_search_rate_millidegrees_per_s;
    float velocity_noise_deadband_mm_s;
    float motion_detection_velocity_mm_s;
    float motion_detection_displacement_mm;
    float search_velocity_damping_millidegrees_per_mm_s;
    float velocity_feedback_millidegrees_per_mm_s;
    float reversal_braking_millidegrees_per_mm_s;
    float moving_bias_decay_millidegrees_per_s;
    float hold_bias_decay_millidegrees_per_s;
    float stall_reacquire_position_error_mm;
    float integral_limit_millidegrees;
    float maximum_output_millidegrees;
    float maximum_slew_millidegrees_per_s;
    uint8_t stall_reacquire_confirm_samples;
} ball_position_controller_config_t;

typedef struct {
    ball_position_controller_config_t config;
    float output_millidegrees;
    float proportional_millidegrees;
    float integral_millidegrees;
    float position_error_mm;
    float target_velocity_mm_s;
    float filtered_velocity_mm_s;
    float velocity_error_mm_s;
    float search_start_position_mm;
    uint32_t update_count;
    uint8_t motion_detection_confirm_samples;
    uint8_t motion_detection_count;
    uint8_t search_start_valid;
    uint8_t saturated;
    uint8_t initialized;
    uint8_t motion_detected;
    uint8_t reversal_braking;
    uint8_t stall_reacquire_count;
} ball_position_controller_t;

bool BallPositionController_Init(ball_position_controller_t *controller,
    const ball_position_controller_config_t *config);
void BallPositionController_Reset(ball_position_controller_t *controller);
void BallPositionController_BeginReversal(
    ball_position_controller_t *controller);
int32_t BallPositionController_Update(ball_position_controller_t *controller,
    float target_mm, float position_mm, float velocity_mm_s, float dt_s);
int32_t BallPositionController_UpdateProfiled(
    ball_position_controller_t *controller, float target_mm,
    float target_velocity_mm_s, float velocity_feedback_gain,
    float position_mm, float velocity_mm_s, float dt_s);
int32_t BallPositionController_UpdateHold(
    ball_position_controller_t *controller, float target_mm,
    float position_mm, float velocity_mm_s, float dt_s);

#endif
