#include "wheel_speed_controller.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static float WheelSpeedController_Abs(float value)
{
    return (value < 0.0f) ? -value : value;
}

static float WheelSpeedController_Clamp(
    float value, float minimum, float maximum)
{
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

static float WheelSpeedController_RateLimit(
    float current, float requested, float maximum_delta)
{
    float delta = requested - current;

    if (delta > maximum_delta) {
        return current + maximum_delta;
    }
    if (delta < -maximum_delta) {
        return current - maximum_delta;
    }
    return requested;
}

void WheelSpeedController_Init(wheel_speed_controller_t *controller,
    const wheel_speed_controller_config_t *config)
{
    if (controller == NULL || config == NULL) {
        return;
    }
    memset(controller, 0, sizeof(*controller));
    controller->config = *config;
    controller->initialized = 1U;
}

void WheelSpeedController_Reset(wheel_speed_controller_t *controller)
{
    wheel_speed_controller_config_t config;
    uint8_t initialized;

    if (controller == NULL) {
        return;
    }
    config = controller->config;
    initialized = controller->initialized;
    memset(controller, 0, sizeof(*controller));
    controller->config = config;
    controller->initialized = initialized;
}

void WheelSpeedController_Observe(wheel_speed_controller_t *controller,
    float measured_rpm)
{
    float alpha;

    if (controller == NULL || controller->initialized == 0U) {
        return;
    }
    alpha = WheelSpeedController_Clamp(
        controller->config.measurement_filter_alpha, 0.0f, 1.0f);
    controller->previous_filtered_rpm = controller->filtered_rpm;
    if (controller->observation_count == 0U) {
        controller->filtered_rpm = measured_rpm;
    } else {
        controller->filtered_rpm +=
            alpha * (measured_rpm - controller->filtered_rpm);
    }
    controller->observation_count++;
}

void WheelSpeedController_PrimeOutput(wheel_speed_controller_t *controller,
    float output_permille)
{
    float limit;

    if (controller == NULL || controller->initialized == 0U) {
        return;
    }
    limit = controller->config.output_limit_permille;
    controller->output_permille = WheelSpeedController_Clamp(
        output_permille, -limit, limit);
    controller->load_release_armed = 0U;
    controller->load_release_decay_active = 0U;
}

int16_t WheelSpeedController_Update(wheel_speed_controller_t *controller,
    float target_rpm, float measured_rpm, float period_s)
{
    float target_delta;
    float output_delta;
    float target_magnitude;
    float target_sign;
    float derivative_rpm_per_s;
    float candidate_integrator;
    float integrator_delta;
    float load_release_unwind_gain;
    float load_release_threshold_rpm;
    float output_slew_down_permille_per_s;
    float candidate_output;
    float limited_output;
    float maximum_output_delta;
    float output_limit;
    bool saturated_high;
    bool saturated_low;
    bool opposes_target;
    bool target_changed;
    bool target_ramping;
    bool unwind_load_integrator;

    if (controller == NULL || controller->initialized == 0U ||
        period_s <= 0.0f) {
        return 0;
    }

    WheelSpeedController_Observe(controller, measured_rpm);
    target_changed = WheelSpeedController_Abs(
        target_rpm - controller->requested_target_rpm) > 0.01f;
    controller->requested_target_rpm = target_rpm;
    if (target_changed) {
        controller->load_release_armed = 0U;
        controller->load_release_decay_active = 0U;
    }
    target_delta = controller->config.target_slew_rpm_per_s * period_s;
    controller->ramped_target_rpm = WheelSpeedController_RateLimit(
        controller->ramped_target_rpm, target_rpm, target_delta);

    if (WheelSpeedController_Abs(controller->ramped_target_rpm) < 0.01f) {
        controller->ramped_target_rpm = 0.0f;
        controller->integrator_permille = 0.0f;
        controller->proportional_permille = 0.0f;
        controller->derivative_permille = 0.0f;
        controller->feedforward_permille = 0.0f;
        controller->error_rpm = 0.0f;
        controller->output_permille = 0.0f;
        controller->update_count++;
        return 0;
    }

    target_sign = (controller->ramped_target_rpm < 0.0f) ? -1.0f : 1.0f;
    target_magnitude = WheelSpeedController_Abs(
        controller->ramped_target_rpm);
    controller->feedforward_permille = target_sign *
        (controller->config.feedforward_offset_permille +
         controller->config.feedforward_gain_permille_per_rpm *
             target_magnitude);
    controller->error_rpm =
        controller->ramped_target_rpm - controller->filtered_rpm;
    controller->proportional_permille =
        controller->config.kp * controller->error_rpm;
    derivative_rpm_per_s =
        (controller->filtered_rpm - controller->previous_filtered_rpm) /
        period_s;
    controller->derivative_permille =
        -controller->config.kd * derivative_rpm_per_s;
    load_release_unwind_gain = WheelSpeedController_Clamp(
        controller->config.load_release_unwind_gain, 1.0f, 10.0f);
    load_release_threshold_rpm = WheelSpeedController_Clamp(
        controller->config.load_release_threshold_rpm,
        0.0f, target_magnitude);

    target_ramping = WheelSpeedController_Abs(
        controller->requested_target_rpm -
        controller->ramped_target_rpm) > 0.01f;
    candidate_integrator = controller->integrator_permille;
    if (target_ramping) {
        controller->integrator_hold_count++;
    } else {
        integrator_delta =
            controller->config.ki * controller->error_rpm * period_s;
        unwind_load_integrator =
            controller->load_release_armed != 0U &&
            target_sign * controller->integrator_permille > 0.0f &&
            target_sign * controller->error_rpm <
                -load_release_threshold_rpm &&
            load_release_unwind_gain > 1.0f;
        if (unwind_load_integrator) {
            integrator_delta *= load_release_unwind_gain;
            controller->load_release_unwind_count++;
            controller->load_release_decay_active = 1U;
        }
        candidate_integrator += integrator_delta;
        if (unwind_load_integrator &&
            target_sign * candidate_integrator < 0.0f) {
            candidate_integrator = 0.0f;
            controller->load_release_armed = 0U;
        } else if (target_sign * candidate_integrator <= 0.0f) {
            controller->load_release_armed = 0U;
        } else if (controller->load_release_armed == 0U &&
            target_sign * controller->error_rpm >
                load_release_threshold_rpm) {
            controller->load_release_armed = 1U;
            controller->load_release_arm_count++;
        }
    }
    candidate_integrator = WheelSpeedController_Clamp(candidate_integrator,
        -controller->config.integrator_limit_permille,
        controller->config.integrator_limit_permille);
    candidate_output = controller->feedforward_permille +
        controller->proportional_permille + candidate_integrator +
        controller->derivative_permille;
    opposes_target =
        (controller->ramped_target_rpm > 0.0f &&
         candidate_output < 0.0f) ||
        (controller->ramped_target_rpm < 0.0f &&
         candidate_output > 0.0f);
    output_limit = controller->config.output_limit_permille;
    limited_output = WheelSpeedController_Clamp(
        candidate_output, -output_limit, output_limit);
    saturated_high = candidate_output > output_limit;
    saturated_low = candidate_output < -output_limit;
    if (!opposes_target &&
        ((!saturated_high && !saturated_low) ||
        (saturated_high && controller->error_rpm < 0.0f) ||
        (saturated_low && controller->error_rpm > 0.0f))) {
        controller->integrator_permille = candidate_integrator;
    } else {
        controller->anti_windup_count++;
        candidate_output = controller->feedforward_permille +
            controller->proportional_permille +
            controller->integrator_permille +
            controller->derivative_permille;
        limited_output = WheelSpeedController_Clamp(
            candidate_output, -output_limit, output_limit);
    }
    if ((controller->ramped_target_rpm > 0.0f &&
         limited_output < 0.0f) ||
        (controller->ramped_target_rpm < 0.0f &&
         limited_output > 0.0f)) {
        limited_output = 0.0f;
    }
    if (limited_output != candidate_output) {
        controller->saturation_count++;
    }

    output_delta = limited_output - controller->output_permille;
    if (WheelSpeedController_Abs(limited_output) >
        WheelSpeedController_Abs(controller->output_permille)) {
        maximum_output_delta =
            controller->config.output_slew_up_permille_per_s * period_s;
    } else {
        output_slew_down_permille_per_s =
            controller->config.output_slew_down_permille_per_s;
        if (controller->load_release_decay_active != 0U &&
            target_sign * controller->error_rpm <
                -load_release_threshold_rpm) {
            output_slew_down_permille_per_s =
                WheelSpeedController_Clamp(
                    controller->config.
                        load_release_output_slew_down_permille_per_s,
                    output_slew_down_permille_per_s, 50000.0f);
        }
        maximum_output_delta = output_slew_down_permille_per_s * period_s;
    }
    if (WheelSpeedController_Abs(output_delta) > maximum_output_delta) {
        controller->output_slew_count++;
    }
    controller->output_permille = WheelSpeedController_RateLimit(
        controller->output_permille, limited_output,
        maximum_output_delta);
    if (controller->load_release_decay_active != 0U &&
        (target_sign * controller->error_rpm >=
            -load_release_threshold_rpm ||
         WheelSpeedController_Abs(
            controller->output_permille - limited_output) < 0.5f)) {
        controller->load_release_decay_active = 0U;
    }
    controller->update_count++;
    return (int16_t) ((controller->output_permille >= 0.0f) ?
        (controller->output_permille + 0.5f) :
        (controller->output_permille - 0.5f));
}
