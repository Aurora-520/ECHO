#include "system_task.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "bsp_encoder.h"
#include "bsp_time.h"
#include "chassis_actuator.h"
#include "h_mission_service.h"
#include "motor_profile.h"
#include "queue.h"
#include "rtos_diagnostics.h"
#include "parameter_service.h"
#include "rtos_hooks.h"
#include "task.h"
#include "telemetry.h"
#include "vehicle_bringup_config.h"

#define SYSTEM_TASK_PERIOD \
    pdMS_TO_TICKS(RTOS_DIAGNOSTICS_SYSTEM_PERIOD_US / 1000U)
#define SYSTEM_HEARTBEAT_PERIOD pdMS_TO_TICKS(1000U)

void SystemTask_Entry(void *context)
{
    QueueHandle_t heartbeat_queue = (QueueHandle_t) context;
    TickType_t last_wake_time      = xTaskGetTickCount();
    TickType_t last_heartbeat_time = last_wake_time;
    uint32_t heartbeat_sequence    = 0U;

    configASSERT(heartbeat_queue != NULL);
    g_rtos_diag.scheduler_started = 1U;

    for (;;) {
        BaseType_t delayed;
        TickType_t now;
        uint32_t start_us;
        uint32_t finish_us;
        bool schedule_resynchronized;
        bool speed_telemetry;
        uint32_t encoder_period_us;
        bsp_encoder_sample_t left_encoder;
        bsp_encoder_sample_t right_encoder;
        telemetry_control_sample_t sample;
        control_tuning_parameters_t tuning;

        delayed = xTaskDelayUntil(&last_wake_time, SYSTEM_TASK_PERIOD);
        now = xTaskGetTickCount();
        schedule_resynchronized = (delayed == pdFALSE);

        if (schedule_resynchronized) {
            last_wake_time = now;
        }

        start_us = BSP_Time_GetUs();
        BSP_Encoder_SampleLeft(&left_encoder);
        BSP_Encoder_SampleRight(&right_encoder);
        encoder_period_us = g_rtos_diag.system_last_period_us;
        if (encoder_period_us == 0U) {
            encoder_period_us = RTOS_DIAGNOSTICS_SYSTEM_PERIOD_US;
        }
        MotorProfile_UpdateEncoderSpeeds(left_encoder.delta_counts,
            right_encoder.delta_counts, encoder_period_us);
        ChassisActuator_ServiceAtControlBoundary(
            schedule_resynchronized,
            left_encoder.delta_counts,
            right_encoder.delta_counts,
            g_motor_profile_diag.left_output_rpm,
            g_motor_profile_diag.right_output_rpm,
            encoder_period_us);
        ParameterService_ApplyPendingAtControlBoundary();
        ParameterService_GetSnapshot(&tuning);

        g_rtos_diag.system_task_run_count++;
        g_rtos_diag.system_task_last_wake_tick = now;

        if ((TickType_t) (now - last_heartbeat_time) >=
            SYSTEM_HEARTBEAT_PERIOD) {
            last_heartbeat_time = now;
            heartbeat_sequence++;

            if (xQueueOverwrite(heartbeat_queue, &heartbeat_sequence) !=
                pdPASS) {
                g_rtos_diag.queue_send_fail_count++;
                RtosFault_Halt(RTOS_FAULT_QUEUE_SEND,
                    xTaskGetCurrentTaskHandle(), "System", 0);
            }
            g_rtos_diag.queue_send_count++;
        }

        speed_telemetry =
            g_chassis_actuator_diag.control_mode ==
                (uint8_t) CHASSIS_ACTUATOR_MODE_SPEED ||
            g_chassis_actuator_diag.control_mode ==
                (uint8_t) CHASSIS_ACTUATOR_MODE_HEADING ||
            g_chassis_actuator_diag.control_mode ==
                (uint8_t) CHASSIS_ACTUATOR_MODE_DISTANCE;
        if (speed_telemetry) {
            sample.setpoint =
                (float) g_chassis_actuator_diag.left_target_deci_rpm * 0.1f;
            sample.measurement = g_motor_profile_diag.left_output_rpm;
            sample.control_output = g_motor_profile_diag.right_output_rpm;
            sample.auxiliary =
                (float) g_chassis_actuator_diag.compensated_left_permille;
            sample.right_auxiliary =
                (float) g_chassis_actuator_diag.compensated_right_permille;
            sample.right_setpoint =
                (float) g_chassis_actuator_diag.right_target_deci_rpm * 0.1f;
            sample.left_pid_proportional =
                g_chassis_actuator_diag.left_pid_proportional_permille;
            sample.left_pid_integrator =
                g_chassis_actuator_diag.left_pid_integrator_permille;
            sample.left_pid_derivative =
                g_chassis_actuator_diag.left_pid_derivative_permille;
            sample.left_pid_feedforward =
                g_chassis_actuator_diag.left_pid_feedforward_permille;
            sample.right_pid_proportional =
                g_chassis_actuator_diag.right_pid_proportional_permille;
            sample.right_pid_integrator =
                g_chassis_actuator_diag.right_pid_integrator_permille;
            sample.right_pid_derivative =
                g_chassis_actuator_diag.right_pid_derivative_permille;
            sample.right_pid_feedforward =
                g_chassis_actuator_diag.right_pid_feedforward_permille;
        } else {
            sample.setpoint =
                (g_chassis_actuator_diag.applied_left_permille != 0) ?
                (float) g_chassis_actuator_diag.applied_left_permille :
                (float) g_chassis_actuator_diag.applied_right_permille;
            sample.measurement = (float) left_encoder.delta_counts;
            sample.control_output = (float) right_encoder.delta_counts;
            sample.auxiliary = (float) right_encoder.delta_counts * 4.0f;
            sample.right_auxiliary =
                (float) g_chassis_actuator_diag.applied_right_permille;
            sample.right_setpoint = 0.0f;
            sample.left_pid_proportional = 0.0f;
            sample.left_pid_integrator = 0.0f;
            sample.left_pid_derivative = 0.0f;
            sample.left_pid_feedforward = 0.0f;
            sample.right_pid_proportional = 0.0f;
            sample.right_pid_integrator = 0.0f;
            sample.right_pid_derivative = 0.0f;
            sample.right_pid_feedforward = 0.0f;
        }
        sample.active_kp = tuning.kp;
        sample.active_ki = tuning.ki;
        sample.active_kd = tuning.kd;
        sample.parameter_apply_sequence = tuning.update_sequence;
        sample.loop_count = g_rtos_diag.system_task_run_count;
        sample.period_us = g_rtos_diag.system_last_period_us;
        sample.execution_us = g_rtos_diag.system_last_execution_us;
        sample.jitter_us = g_rtos_diag.system_last_jitter_us;
        sample.deadline_miss_count =
            g_rtos_diag.system_deadline_miss_count;
        sample.flags = 0U;
        if (!speed_telemetry) {
            sample.flags |= TELEMETRY_CONTROL_FLAG_LEFT_ENCODER_RAW |
                TELEMETRY_CONTROL_FLAG_RIGHT_ENCODER_RAW_X1;
        }
        if (g_chassis_actuator_diag.applied_left_permille != 0) {
            sample.flags |= TELEMETRY_CONTROL_FLAG_MOTOR_LEFT_ACTIVE;
        }
        if (g_chassis_actuator_diag.applied_right_permille != 0) {
            sample.flags |= TELEMETRY_CONTROL_FLAG_MOTOR_RIGHT_ACTIVE;
        }
        if (speed_telemetry &&
            g_chassis_actuator_diag.output_permitted != 0U) {
            sample.flags |= TELEMETRY_CONTROL_FLAG_SPEED_CLOSED_LOOP;
            if (g_chassis_actuator_diag.speed_phase ==
                (uint8_t) CHASSIS_SPEED_PHASE_BOOST) {
                sample.flags |= TELEMETRY_CONTROL_FLAG_SPEED_BOOST;
            } else if (g_chassis_actuator_diag.speed_phase ==
                (uint8_t) CHASSIS_SPEED_PHASE_TRACKING) {
                sample.flags |= TELEMETRY_CONTROL_FLAG_SPEED_TRACKING;
            }
            if (g_chassis_actuator_diag.control_mode ==
                (uint8_t) CHASSIS_ACTUATOR_MODE_HEADING) {
                sample.flags |=
                    TELEMETRY_CONTROL_FLAG_HEADING_CLOSED_LOOP;
            } else if (g_chassis_actuator_diag.control_mode ==
                (uint8_t) CHASSIS_ACTUATOR_MODE_DISTANCE) {
                sample.flags |=
                    TELEMETRY_CONTROL_FLAG_HEADING_CLOSED_LOOP |
                    TELEMETRY_CONTROL_FLAG_DISTANCE_CLOSED_LOOP;
            }
        }
        if (g_h_mission_diag.active_mission < H_MISSION_COUNT) {
            uint32_t mission_code =
                (uint32_t) g_h_mission_diag.active_mission + 1U;
            uint32_t line_lost = g_h_mission_diag.line_lost_streak;
            uint32_t finish_evidence =
                g_h_mission_diag.line_finish_evidence_count;

            if (line_lost > 0x1FU) {
                line_lost = 0x1FU;
            }
            if (finish_evidence > 0x07U) {
                finish_evidence = 0x07U;
            }
            sample.flags |=
                (mission_code << TELEMETRY_CONTROL_FLAG_H_MISSION_SHIFT) &
                    TELEMETRY_CONTROL_FLAG_H_MISSION_MASK;
            if (g_h_mission_diag.line_valid != 0U) {
                sample.flags |= TELEMETRY_CONTROL_FLAG_H_LINE_VALID;
            }
            if (g_h_mission_diag.line_finish_armed != 0U) {
                sample.flags |= TELEMETRY_CONTROL_FLAG_H_FINISH_ARMED;
            }
            if (g_h_mission_diag.line_finish_streak != 0U) {
                sample.flags |= TELEMETRY_CONTROL_FLAG_H_FINISH_DETECTED;
            }
            sample.flags |= ((uint32_t)
                g_h_mission_diag.line_terminal_status <<
                    TELEMETRY_CONTROL_FLAG_H_TERMINAL_SHIFT) &
                TELEMETRY_CONTROL_FLAG_H_TERMINAL_MASK;
            sample.flags |= line_lost <<
                TELEMETRY_CONTROL_FLAG_H_LINE_LOST_SHIFT;
            sample.flags |= finish_evidence <<
                TELEMETRY_CONTROL_FLAG_H_FINISH_EVID_SHIFT;
            if (g_h_mission_diag.line_start_cleared != 0U) {
                sample.flags |= TELEMETRY_CONTROL_FLAG_H_START_CLEARED;
            }
        }
#if !ECHO_IMU_DIAGNOSTIC_CAPTURE
        (void) Telemetry_PublishControl(&sample);
#endif

        finish_us = BSP_Time_GetUs();
        RtosDiagnostics_RecordSystemTiming(
            start_us, finish_us, schedule_resynchronized);
    }
}
