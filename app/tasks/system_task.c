#include "system_task.h"

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "bsp_encoder.h"
#include "bsp_time.h"
#include "chassis_actuator.h"
#include "motor_profile.h"
#include "queue.h"
#include "rtos_diagnostics.h"
#include "parameter_service.h"
#include "rtos_hooks.h"
#include "task.h"
#include "telemetry.h"
#include "zdt_stepper.h"

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
        uint32_t encoder_period_us;
        bsp_encoder_sample_t left_encoder;
        bsp_encoder_sample_t right_encoder;
        telemetry_control_sample_t sample;

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
            g_motor_profile_diag.left_output_rpm,
            g_motor_profile_diag.right_output_rpm,
            encoder_period_us);
        ZdtStepper_Service(start_us);
        ParameterService_ApplyPendingAtControlBoundary();

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

        if (g_chassis_actuator_diag.control_mode ==
                (uint8_t) CHASSIS_ACTUATOR_MODE_SPEED &&
            g_chassis_actuator_diag.output_permitted != 0U) {
            sample.setpoint =
                (float) g_chassis_actuator_diag.left_target_deci_rpm * 0.1f;
            sample.measurement = g_motor_profile_diag.left_output_rpm;
            sample.control_output = g_motor_profile_diag.right_output_rpm;
            sample.auxiliary =
                (float) g_chassis_actuator_diag.normalized_left_permille;
        } else {
            sample.setpoint =
                (g_chassis_actuator_diag.applied_left_permille != 0) ?
                (float) g_chassis_actuator_diag.applied_left_permille :
                (float) g_chassis_actuator_diag.applied_right_permille;
            sample.measurement = (float) left_encoder.delta_counts;
            sample.control_output = (float) right_encoder.delta_counts;
            sample.auxiliary = (float) right_encoder.delta_counts * 4.0f;
        }
        sample.loop_count = g_rtos_diag.system_task_run_count;
        sample.period_us = g_rtos_diag.system_last_period_us;
        sample.execution_us = g_rtos_diag.system_last_execution_us;
        sample.jitter_us = g_rtos_diag.system_last_jitter_us;
        sample.deadline_miss_count =
            g_rtos_diag.system_deadline_miss_count;
        sample.flags = TELEMETRY_CONTROL_FLAG_LEFT_ENCODER_RAW |
            TELEMETRY_CONTROL_FLAG_RIGHT_ENCODER_RAW_X1;
        if (g_chassis_actuator_diag.applied_left_permille != 0) {
            sample.flags |= TELEMETRY_CONTROL_FLAG_MOTOR_LEFT_ACTIVE;
        }
        if (g_chassis_actuator_diag.applied_right_permille != 0) {
            sample.flags |= TELEMETRY_CONTROL_FLAG_MOTOR_RIGHT_ACTIVE;
        }
        if (g_chassis_actuator_diag.control_mode ==
                (uint8_t) CHASSIS_ACTUATOR_MODE_SPEED &&
            g_chassis_actuator_diag.output_permitted != 0U) {
            sample.flags |= TELEMETRY_CONTROL_FLAG_SPEED_CLOSED_LOOP;
            if (g_chassis_actuator_diag.speed_phase ==
                (uint8_t) CHASSIS_SPEED_PHASE_BOOST) {
                sample.flags |= TELEMETRY_CONTROL_FLAG_SPEED_BOOST;
            } else if (g_chassis_actuator_diag.speed_phase ==
                (uint8_t) CHASSIS_SPEED_PHASE_TRACKING) {
                sample.flags |= TELEMETRY_CONTROL_FLAG_SPEED_TRACKING;
            }
        }
        (void) Telemetry_PublishControl(&sample);

        finish_us = BSP_Time_GetUs();
        RtosDiagnostics_RecordSystemTiming(
            start_us, finish_us, schedule_resynchronized);
    }
}
