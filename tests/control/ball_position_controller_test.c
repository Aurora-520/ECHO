#include <assert.h>

#include "ball_position_controller.h"

int main(void)
{
    const ball_position_controller_config_t config = {
        .position_velocity_gain_per_s = 2.8f,
        .negative_position_velocity_gain_per_s = 2.2f,
        .maximum_velocity_mm_s = 80.0f,
        .search_integral_gain_millidegrees_per_mm_s = 100.0f,
        .maximum_search_rate_millidegrees_per_s = 6000.0f,
        .velocity_noise_deadband_mm_s = 12.0f,
        .motion_detection_velocity_mm_s = 15.0f,
        .motion_detection_displacement_mm = 2.0f,
        .search_velocity_damping_millidegrees_per_mm_s = 0.0f,
        .velocity_feedback_millidegrees_per_mm_s = 100.0f,
        .reversal_braking_millidegrees_per_mm_s = 200.0f,
        .moving_bias_decay_millidegrees_per_s = 1000.0f,
        .hold_bias_decay_millidegrees_per_s = 500.0f,
        .stall_reacquire_position_error_mm = 3.0f,
        .integral_limit_millidegrees = 25000.0f,
        .maximum_output_millidegrees = 25000.0f,
        .maximum_slew_millidegrees_per_s = 50000.0f,
        .stall_reacquire_confirm_samples = 3U
    };
    ball_position_controller_t controller;
    int32_t output;

    assert(BallPositionController_Init(&controller, &config));

    /* H3/legacy updates must ignore all drive-only extensions. */
    assert(BallPositionController_SetLimits(&controller,
        40000.0f, 40000.0f));
    BallPositionController_SetFeedforward(&controller, 5000.0f);
    assert(BallPositionController_SetOpposingFeedbackScale(
        &controller, 0.2f));
    output = BallPositionController_UpdateHold(
        &controller, 0.0f, 0.0f, 0.0f, 0.02f);
    assert(output == 0);
    assert(controller.saturated == 0U);
    BallPositionController_Reset(&controller);

    /* H5 feedforward is summed before its task-specific output limit. */
    assert(BallPositionController_SetLimits(&controller,
        40000.0f, 40000.0f));
    BallPositionController_SetFeedforward(&controller, 5000.0f);
    output = BallPositionController_UpdateDriveHold(
        &controller, 0.0f, 0.0f, 0.0f, 0.02f);
    assert(output == 1000);
    assert(controller.feedforward_millidegrees == 5000.0f);
    assert(controller.output_limit_millidegrees == 40000.0f);
    controller.output_millidegrees = 39500.0f;
    BallPositionController_SetFeedforward(&controller, 50000.0f);
    output = BallPositionController_UpdateDriveHold(
        &controller, 0.0f, 0.0f, 0.0f, 0.02f);
    assert(output == 40000);
    assert(controller.saturated != 0U);
    BallPositionController_Reset(&controller);
    assert(controller.feedforward_millidegrees == 0.0f);
    assert(controller.opposing_feedback_scale == 1.0f);
    assert(controller.output_limit_millidegrees == 25000.0f);
    assert(controller.integral_limit_millidegrees == 25000.0f);

    /* Launch blending only attenuates feedback opposing feedforward. */
    controller.config.maximum_slew_millidegrees_per_s = 1000000.0f;
    assert(BallPositionController_SetOpposingFeedbackScale(
        &controller, 0.2f));
    BallPositionController_SetFeedforward(&controller, 5000.0f);
    output = BallPositionController_UpdateDriveHold(
        &controller, 0.0f, 20.0f, 0.0f, 0.02f);
    assert(output == 4120);
    BallPositionController_Reset(&controller);
    controller.config.maximum_slew_millidegrees_per_s = 1000000.0f;
    assert(BallPositionController_SetOpposingFeedbackScale(
        &controller, 0.2f));
    BallPositionController_SetFeedforward(&controller, 5000.0f);
    output = BallPositionController_UpdateDriveHold(
        &controller, 0.0f, -20.0f, 0.0f, 0.02f);
    assert(output == 10600);
    assert(!BallPositionController_SetOpposingFeedbackScale(
        &controller, 1.1f));
    BallPositionController_Reset(&controller);
    controller.config.maximum_slew_millidegrees_per_s =
        config.maximum_slew_millidegrees_per_s;
    assert(controller.opposing_feedback_scale == 1.0f);

    /* Static camera jitter must not enter the velocity feedback path. */
    output = BallPositionController_Update(
        &controller, 50.0f, 0.0f, 10.0f, 0.02f);
    assert(output == 100);
    assert(controller.filtered_velocity_mm_s == 0.0f);
    assert(controller.target_velocity_mm_s == 80.0f);
    assert(controller.motion_detected == 0U);

    /* Positive and negative travel use independent continuous envelopes. */
    BallPositionController_Reset(&controller);
    (void) BallPositionController_Update(
        &controller, 50.0f, 25.0f, 0.0f, 0.02f);
    assert(controller.target_velocity_mm_s == 70.0f);
    (void) BallPositionController_Update(
        &controller, 50.0f, 45.0f, 0.0f, 0.02f);
    assert(controller.target_velocity_mm_s == 14.0f);
    BallPositionController_Reset(&controller);
    (void) BallPositionController_Update(
        &controller, -50.0f, -25.0f, 0.0f, 0.02f);
    assert(controller.target_velocity_mm_s == -55.0f);

    /* Two directional samples plus displacement reject single-frame noise. */
    BallPositionController_Reset(&controller);
    (void) BallPositionController_Update(
        &controller, 50.0f, 0.0f, 0.0f, 0.02f);
    (void) BallPositionController_Update(
        &controller, 50.0f, 2.0f, 20.0f, 0.02f);
    (void) BallPositionController_Update(
        &controller, 50.0f, 3.0f, 20.0f, 0.02f);
    assert(controller.motion_detected != 0U);
    assert(controller.integral_millidegrees == 0.0f);

    /* The terminal profile keeps the 50 mm target while braking early. */
    BallPositionController_Reset(&controller);
    controller.motion_detected = 1U;
    assert(BallPositionController_UpdateProfiled(&controller,
        50.0f, 20.0f, 300.0f, 40.0f, 67.0f, 0.02f) == -1000);
    assert(controller.position_error_mm == 10.0f);
    assert(controller.target_velocity_mm_s == 20.0f);
    assert(controller.proportional_millidegrees == -14100.0f);

    /* Slow real motion below the control deadband must end angle search. */
    {
        ball_position_controller_config_t slow_motion_config = config;
        ball_position_controller_t slow_motion_controller;

        slow_motion_config.motion_detection_velocity_mm_s = 8.0f;
        slow_motion_config.motion_detection_displacement_mm = 1.0f;
        assert(BallPositionController_Init(&slow_motion_controller,
            &slow_motion_config));
        (void) BallPositionController_Update(&slow_motion_controller,
            -50.0f, 0.0f, 0.0f, 0.02f);
        (void) BallPositionController_Update(&slow_motion_controller,
            -50.0f, -1.1f, -9.0f, 0.02f);
        (void) BallPositionController_Update(&slow_motion_controller,
            -50.0f, -1.5f, -9.0f, 0.02f);
        assert(slow_motion_controller.filtered_velocity_mm_s == 0.0f);
        assert(slow_motion_controller.motion_detected != 0U);
        assert(slow_motion_controller.integral_millidegrees == 0.0f);
    }

    /* A qualified final-band error must not re-arm high-rate search. */
    {
        ball_position_controller_config_t final_band_config = config;
        ball_position_controller_t final_band_controller;
        uint8_t index;

        final_band_config.stall_reacquire_position_error_mm = 7.0f;
        final_band_config.stall_reacquire_confirm_samples = 12U;
        assert(BallPositionController_Init(&final_band_controller,
            &final_band_config));
        final_band_controller.motion_detected = 1U;
        for (index = 0U; index < 40U; index++) {
            (void) BallPositionController_Update(&final_band_controller,
                0.0f, -6.0f, 0.0f, 0.02f);
        }
        assert(final_band_controller.motion_detected != 0U);
    }

    /* A stationary residual error must re-arm learned-angle search. */
    controller.output_millidegrees = 1000.0f;
    (void) BallPositionController_Update(
        &controller, 0.0f, -8.0f, 0.0f, 0.02f);
    (void) BallPositionController_Update(
        &controller, 0.0f, -8.0f, 0.0f, 0.02f);
    (void) BallPositionController_Update(
        &controller, 0.0f, -8.0f, 0.0f, 0.02f);
    assert(controller.motion_detected == 0U);
    assert(controller.search_start_valid != 0U);

    /* Reversal immediately requests braking while preserving output slew. */
    controller.output_millidegrees = 10000.0f;
    BallPositionController_BeginReversal(&controller);
    output = BallPositionController_Update(
        &controller, -50.0f, 50.0f, 60.0f, 0.02f);
    assert(output < 10000);
    assert(controller.proportional_millidegrees < 0.0f);
    assert(controller.integral_millidegrees == 0.0f);
    assert(controller.reversal_braking != 0U);

    /* Once wrong-way motion is stopped, resume learned-angle search. */
    (void) BallPositionController_Update(
        &controller, -50.0f, 50.0f, 8.0f, 0.02f);
    assert(controller.filtered_velocity_mm_s == 0.0f);
    assert(controller.reversal_braking == 0U);
    assert(controller.integral_millidegrees != 0.0f);

    /* Final hold also ignores the measured static velocity jitter. */
    BallPositionController_Reset(&controller);
    controller.output_millidegrees = -1000.0f;
    controller.integral_millidegrees = -1000.0f;
    output = BallPositionController_UpdateHold(
        &controller, -50.0f, -50.0f, -10.0f, 0.02f);
    assert(output == -990);
    assert(controller.filtered_velocity_mm_s == 0.0f);
    assert(controller.integral_millidegrees == -990.0f);
    assert(controller.saturated == 0U);
    return 0;
}
