#include "ball_position_controller.h"

#include <stddef.h>

static float BallPositionController_Abs(float value)
{
    return value < 0.0f ? -value : value;
}

static float BallPositionController_Clamp(float value, float limit)
{
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

static float BallPositionController_ApplyDeadband(float value,
    float deadband)
{
    return BallPositionController_Abs(value) <= deadband ? 0.0f : value;
}

static int32_t BallPositionController_Round(float value)
{
    return value >= 0.0f ? (int32_t) (value + 0.5f) :
        (int32_t) (value - 0.5f);
}

static float BallPositionController_ApproachZero(float value, float delta)
{
    if (value > delta) {
        return value - delta;
    }
    if (value < -delta) {
        return value + delta;
    }
    return 0.0f;
}

bool BallPositionController_Init(ball_position_controller_t *controller,
    const ball_position_controller_config_t *config)
{
    if (controller == NULL || config == NULL ||
        config->position_velocity_gain_per_s <= 0.0f ||
        config->negative_position_velocity_gain_per_s <= 0.0f ||
        config->maximum_velocity_mm_s <= 0.0f ||
        config->search_integral_gain_millidegrees_per_mm_s <= 0.0f ||
        config->maximum_search_rate_millidegrees_per_s <= 0.0f ||
        config->velocity_noise_deadband_mm_s < 0.0f ||
        config->motion_detection_velocity_mm_s <= 0.0f ||
        config->motion_detection_displacement_mm <= 0.0f ||
        config->search_velocity_damping_millidegrees_per_mm_s < 0.0f ||
        config->velocity_feedback_millidegrees_per_mm_s < 0.0f ||
        config->reversal_braking_millidegrees_per_mm_s < 0.0f ||
        config->moving_bias_decay_millidegrees_per_s < 0.0f ||
        config->hold_bias_decay_millidegrees_per_s < 0.0f ||
        config->stall_reacquire_position_error_mm <= 0.0f ||
        config->stall_reacquire_confirm_samples == 0U ||
        config->integral_limit_millidegrees < 0.0f ||
        config->maximum_output_millidegrees <= 0.0f ||
        config->maximum_slew_millidegrees_per_s <= 0.0f) {
        return false;
    }
    controller->config = *config;
    controller->initialized = 1U;
    controller->motion_detection_confirm_samples = 2U;
    BallPositionController_Reset(controller);
    return true;
}

void BallPositionController_Reset(ball_position_controller_t *controller)
{
    if (controller == NULL) {
        return;
    }
    controller->output_millidegrees = 0.0f;
    controller->proportional_millidegrees = 0.0f;
    controller->integral_millidegrees = 0.0f;
    controller->position_error_mm = 0.0f;
    controller->target_velocity_mm_s = 0.0f;
    controller->filtered_velocity_mm_s = 0.0f;
    controller->velocity_error_mm_s = 0.0f;
    controller->search_start_position_mm = 0.0f;
    controller->update_count = 0U;
    controller->motion_detection_count = 0U;
    controller->search_start_valid = 0U;
    controller->saturated = 0U;
    controller->motion_detected = 0U;
    controller->reversal_braking = 0U;
    controller->stall_reacquire_count = 0U;
}

void BallPositionController_BeginReversal(
    ball_position_controller_t *controller)
{
    if (controller == NULL) {
        return;
    }
    controller->integral_millidegrees = 0.0f;
    controller->proportional_millidegrees = 0.0f;
    controller->motion_detected = 0U;
    controller->motion_detection_count = 0U;
    controller->search_start_valid = 0U;
    controller->reversal_braking = 1U;
    controller->stall_reacquire_count = 0U;
}

static int32_t BallPositionController_UpdateInternal(
    ball_position_controller_t *controller, float target_mm,
    float position_mm, float velocity_mm_s, float dt_s, bool hold,
    bool velocity_profiled, float profiled_target_velocity_mm_s,
    float profiled_velocity_feedback_gain)
{
    float requested;
    float limited;
    float maximum_delta;
    float delta;
    float direction;
    float velocity_feedback_gain;

    if (controller == NULL || controller->initialized == 0U ||
        dt_s <= 0.0f || dt_s > 0.25f) {
        return 0;
    }
    controller->position_error_mm = target_mm - position_mm;
    controller->filtered_velocity_mm_s =
        BallPositionController_ApplyDeadband(velocity_mm_s,
            controller->config.velocity_noise_deadband_mm_s);
    controller->target_velocity_mm_s = BallPositionController_Clamp(
        (controller->position_error_mm >= 0.0f ?
            controller->config.position_velocity_gain_per_s :
            controller->config.negative_position_velocity_gain_per_s) *
                controller->position_error_mm,
        controller->config.maximum_velocity_mm_s);
    if (velocity_profiled) {
        controller->target_velocity_mm_s = BallPositionController_Clamp(
            profiled_target_velocity_mm_s,
            controller->config.maximum_velocity_mm_s);
    }
    controller->velocity_error_mm_s =
        controller->target_velocity_mm_s -
            controller->filtered_velocity_mm_s;
    velocity_feedback_gain = velocity_profiled ?
        profiled_velocity_feedback_gain :
        controller->config.velocity_feedback_millidegrees_per_mm_s;

    direction = controller->position_error_mm >= 0.0f ? 1.0f : -1.0f;
    if (hold) {
        controller->motion_detected = 1U;
        controller->reversal_braking = 0U;
        controller->stall_reacquire_count = 0U;
        controller->integral_millidegrees =
            BallPositionController_ApproachZero(
                controller->integral_millidegrees,
                controller->config.hold_bias_decay_millidegrees_per_s *
                    dt_s);
    } else if (controller->motion_detected == 0U) {
        float directional_displacement;

        if (controller->search_start_valid == 0U) {
            controller->search_start_position_mm = position_mm;
            controller->search_start_valid = 1U;
        }
        directional_displacement = direction *
            (position_mm - controller->search_start_position_mm);
        /* Displacement qualification makes raw low-speed motion safe to use. */
        if (direction * velocity_mm_s >=
                controller->config.motion_detection_velocity_mm_s &&
            directional_displacement >=
                controller->config.motion_detection_displacement_mm) {
            if (controller->motion_detection_count < UINT8_MAX) {
                controller->motion_detection_count++;
            }
            if (controller->motion_detection_count >=
                    controller->motion_detection_confirm_samples) {
                controller->motion_detected = 1U;
                controller->reversal_braking = 0U;
                controller->stall_reacquire_count = 0U;
                controller->proportional_millidegrees =
                    velocity_feedback_gain *
                        controller->velocity_error_mm_s;
                /*
                 * Search tilt is only used to start motion. Carrying it into
                 * velocity control stores too much energy when breakaway is
                 * detected late, so release it as soon as motion is real.
                 */
                controller->integral_millidegrees = 0.0f;
            }
        } else {
            controller->motion_detection_count = 0U;
        }
        if (controller->motion_detected == 0U) {
            if (controller->reversal_braking != 0U &&
                direction * controller->filtered_velocity_mm_s < 0.0f) {
                controller->integral_millidegrees = 0.0f;
                controller->proportional_millidegrees =
                    -controller->config.
                        reversal_braking_millidegrees_per_mm_s *
                        controller->filtered_velocity_mm_s;
            } else {
                float search_rate;

                if (controller->reversal_braking != 0U) {
                    controller->reversal_braking = 0U;
                    controller->search_start_position_mm = position_mm;
                    controller->motion_detection_count = 0U;
                    controller->integral_millidegrees =
                        BallPositionController_Clamp(
                            controller->output_millidegrees,
                            controller->config.
                                integral_limit_millidegrees);
                }
                search_rate = BallPositionController_Clamp(
                    controller->config.
                        search_integral_gain_millidegrees_per_mm_s *
                        controller->position_error_mm,
                    controller->config.
                        maximum_search_rate_millidegrees_per_s);
                controller->integral_millidegrees =
                    BallPositionController_Clamp(
                        controller->integral_millidegrees +
                            search_rate * dt_s,
                        controller->config.integral_limit_millidegrees);
                controller->proportional_millidegrees =
                    -controller->config.
                        search_velocity_damping_millidegrees_per_mm_s *
                        controller->filtered_velocity_mm_s;
            }
        }
    } else {
        if (controller->filtered_velocity_mm_s == 0.0f &&
            BallPositionController_Abs(controller->position_error_mm) >
                controller->config.stall_reacquire_position_error_mm) {
            if (controller->stall_reacquire_count < UINT8_MAX) {
                controller->stall_reacquire_count++;
            }
            if (controller->stall_reacquire_count >=
                    controller->config.stall_reacquire_confirm_samples) {
                controller->motion_detected = 0U;
                controller->motion_detection_count = 0U;
                controller->search_start_position_mm = position_mm;
                controller->search_start_valid = 1U;
                controller->reversal_braking = 0U;
                controller->stall_reacquire_count = 0U;
                controller->integral_millidegrees = 0.0f;
                controller->proportional_millidegrees = 0.0f;
            }
        } else {
            controller->stall_reacquire_count = 0U;
        }
        if (controller->motion_detected != 0U) {
            controller->integral_millidegrees =
                BallPositionController_ApproachZero(
                    controller->integral_millidegrees,
                    controller->config.moving_bias_decay_millidegrees_per_s *
                        dt_s);
        }
    }

    if (controller->motion_detected != 0U) {
        controller->proportional_millidegrees =
            velocity_feedback_gain *
            controller->velocity_error_mm_s;
    }
    requested = controller->proportional_millidegrees +
        controller->integral_millidegrees;
    limited = BallPositionController_Clamp(requested,
        controller->config.maximum_output_millidegrees);
    controller->saturated =
        BallPositionController_Abs(requested - limited) > 0.01f ? 1U : 0U;

    maximum_delta =
        controller->config.maximum_slew_millidegrees_per_s * dt_s;
    delta = limited - controller->output_millidegrees;
    delta = BallPositionController_Clamp(delta, maximum_delta);
    controller->output_millidegrees += delta;
    controller->update_count++;
    return BallPositionController_Round(controller->output_millidegrees);
}

int32_t BallPositionController_Update(ball_position_controller_t *controller,
    float target_mm, float position_mm, float velocity_mm_s, float dt_s)
{
    return BallPositionController_UpdateInternal(controller, target_mm,
        position_mm, velocity_mm_s, dt_s, false, false, 0.0f, 0.0f);
}

int32_t BallPositionController_UpdateProfiled(
    ball_position_controller_t *controller, float target_mm,
    float target_velocity_mm_s, float velocity_feedback_gain,
    float position_mm, float velocity_mm_s, float dt_s)
{
    if (velocity_feedback_gain <= 0.0f) {
        return 0;
    }
    return BallPositionController_UpdateInternal(controller, target_mm,
        position_mm, velocity_mm_s, dt_s, false, true,
        target_velocity_mm_s, velocity_feedback_gain);
}

int32_t BallPositionController_UpdateHold(
    ball_position_controller_t *controller, float target_mm,
    float position_mm, float velocity_mm_s, float dt_s)
{
    return BallPositionController_UpdateInternal(controller, target_mm,
        position_mm, velocity_mm_s, dt_s, true, false, 0.0f, 0.0f);
}
