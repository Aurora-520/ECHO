#include "display_task.h"

#include <stddef.h>
#include <string.h>

#include "bsp_encoder.h"
#include "bsp_esp_uart.h"
#include "bsp_i2c.h"
#include "bsp_reflectance.h"
#include "bsp_supply_voltage.h"
#include "bsp_time.h"
#include "bsp_zdt_uart.h"
#include "attitude_estimator.h"
#include "ball_vision.h"
#include "ball_balance_service.h"
#include "competition_page.h"
#include "competition_service.h"
#include "competition_storage.h"
#include "chassis_actuator.h"
#include "esp_uart_link_test.h"
#include "h_mission_service.h"
#include "imu_service.h"
#include "motor_profile.h"
#include "parameter_service.h"
#include "rtos_diagnostics.h"
#include "serial_tx.h"
#include "ssd1306.h"
#include "system_health.h"
#include "task.h"
#include "ti_msp_dl_config.h"
#include "ui_input.h"
#include "vehicle_bringup_config.h"
#include "zdt_stepper.h"

#define DISPLAY_POWER_UP_DELAY pdMS_TO_TICKS(100U)
#define DISPLAY_ONLINE_PERIOD  pdMS_TO_TICKS(50U)
#define DISPLAY_RETRY_PERIOD   pdMS_TO_TICKS(250U)
#define DISPLAY_IO_WINDOW_POLL_PERIOD pdMS_TO_TICKS(1U)
#define DISPLAY_IO_WINDOW_WAIT_TICKS  pdMS_TO_TICKS(5U)
#define DISPLAY_IO_WINDOW_RETRY_PERIOD pdMS_TO_TICKS(7U)
#define DISPLAY_IO_WINDOW_MAX_DEFERRALS 8U

volatile display_task_diagnostics_t g_display_task_diag;
volatile uint32_t g_display_debug_refresh_enable = 1U;
volatile uint32_t g_display_debug_force_offline;

static uint8_t s_consecutive_deferred_count;
static attitude_estimator_snapshot_t s_display_attitude_snapshot;
static chassis_actuator_diagnostics_t s_display_chassis_snapshot;
static competition_page_data_t s_display_page_data;
static imu_service_snapshot_t s_display_imu_snapshot;
static ball_balance_snapshot_t s_display_ball_snapshot;
static float s_display_roll_zero_deg;
static float s_display_pitch_zero_deg;
static float s_display_yaw_zero_deg;
static uint8_t s_attitude_display_zero_valid;

static float DisplayTask_WrapDegrees(float value)
{
    while (value > 180.0f) {
        value -= 360.0f;
    }
    while (value < -180.0f) {
        value += 360.0f;
    }
    return value;
}
static TickType_t DisplayTask_ActivePeriod(void)
{
    return (g_competition_service.state ==
        (uint8_t) COMPETITION_STATE_RUNNING ||
        g_competition_service.state ==
        (uint8_t) COMPETITION_STATE_COUNTDOWN) ?
        pdMS_TO_TICKS(20U) : DISPLAY_ONLINE_PERIOD;
}

static bool DisplayTask_TryBeginIoWindow(void)
{
    TickType_t start_tick = xTaskGetTickCount();

    SerialTx_RequestPriorityQuietWindow();
    for (;;) {
        if (SerialTx_TryBeginRequestedQuietWindow()) {
            g_display_task_diag.io_window_acquired_count++;
            return true;
        }
        if ((TickType_t) (xTaskGetTickCount() - start_tick) >=
            DISPLAY_IO_WINDOW_WAIT_TICKS) {
            g_display_task_diag.io_window_deferred_count++;
            return false;
        }
        vTaskDelay(DISPLAY_IO_WINDOW_POLL_PERIOD);
    }
}

static TickType_t DisplayTask_GetDeferredDelay(void)
{
    s_consecutive_deferred_count++;
    if (s_consecutive_deferred_count >= DISPLAY_IO_WINDOW_MAX_DEFERRALS) {
        s_consecutive_deferred_count = 0U;
        g_display_task_diag.io_window_skipped_count++;
        return DISPLAY_ONLINE_PERIOD;
    }
    return DISPLAY_IO_WINDOW_RETRY_PERIOD;
}

static void DisplayTask_Render(void)
{
    attitude_estimator_snapshot_t *attitude =
        &s_display_attitude_snapshot;
    chassis_actuator_diagnostics_t *chassis =
        &s_display_chassis_snapshot;
    competition_page_data_t *data = &s_display_page_data;
    imu_service_snapshot_t *imu = &s_display_imu_snapshot;
    const parameter_metadata_t *metadata;
    bool attitude_valid;
    ball_vision_snapshot_t vision;
    ball_balance_snapshot_t *ball = &s_display_ball_snapshot;

    memset(data, 0, sizeof(*data));
    CompetitionService_GetSnapshot(&data->competition);
    if (BallBalanceService_GetSnapshot(ball)) {
        data->ball_position_mm =
            (float) ball->measured_position_decimm * 0.1f;
        data->ball_target_mm =
            (float) ball->target_position_decimm * 0.1f;
        data->ball_error_mm =
            (float) ball->position_error_decimm * 0.1f;
        data->ball_velocity_mm_s = (float) ball->velocity_mm_s;
        data->ball_valid = ball->vision_valid;
    }
    if (!SystemHealth_GetSnapshot(&data->health)) {
        memset(&data->health, 0, sizeof(data->health));
        data->health.level = SYSTEM_HEALTH_UNKNOWN;
    }
    data->battery_mv = g_bsp_supply_voltage_diag.battery_mv;
    data->supply_valid =
        (g_bsp_supply_voltage_diag.last_conversion_ok != 0U) &&
        data->battery_mv >= 8000U && data->battery_mv <= 18000U;
    data->imu_available = ECHO_ENABLE_IMU;
    data->oled_available = ECHO_ENABLE_OLED;
    data->reflectance_available = ECHO_ENABLE_REFLECTANCE;
    data->esp_available = ECHO_ENABLE_ESP_LINK;
    data->lidar_available = ECHO_ENABLE_TFMINI;
    data->vision_available = ECHO_ENABLE_BALL_VISION;
    data->uptime_s = data->health.uptime_ticks / configTICK_RATE_HZ;
    attitude_valid = AttitudeEstimator_GetSnapshot(attitude);
    if (ImuService_GetSnapshot(imu)) {
        data->temperature_c = imu->temperature_c;
        data->imu_ready = imu->calibrated;
        data->imu_state = imu->state;
        data->imu_calibration_samples = imu->calibration_samples;
        data->imu_calibration_target_samples =
            imu->calibration_target_samples;
    }
    if ((data->imu_ready != 0U) && attitude_valid) {
        if (s_attitude_display_zero_valid == 0U) {
            s_display_roll_zero_deg = attitude->roll_deg;
            s_display_pitch_zero_deg = attitude->pitch_deg;
            s_display_yaw_zero_deg = attitude->yaw_deg;
            s_attitude_display_zero_valid = 1U;
        }
        data->roll_deg = DisplayTask_WrapDegrees(
            attitude->roll_deg - s_display_roll_zero_deg);
        data->pitch_deg = DisplayTask_WrapDegrees(
            attitude->pitch_deg - s_display_pitch_zero_deg);
        data->yaw_deg = DisplayTask_WrapDegrees(
            attitude->yaw_deg - s_display_yaw_zero_deg);
    } else if (data->imu_ready == 0U) {
        s_attitude_display_zero_valid = 0U;
    }
    data->left_rpm = g_motor_profile_diag.left_output_rpm;
    data->right_rpm = g_motor_profile_diag.right_output_rpm;
    data->distance_progress_mm =
        g_chassis_actuator_diag.distance_progress_mm;
    taskENTER_CRITICAL();
    *chassis = g_chassis_actuator_diag;
    taskEXIT_CRITICAL();
    data->tune_left_target_rpm =
        (float) chassis->left_target_deci_rpm * 0.1f;
    data->tune_right_target_rpm =
        (float) chassis->right_target_deci_rpm * 0.1f;
    data->tune_left_measured_rpm = chassis->left_measured_rpm;
    data->tune_right_measured_rpm = chassis->right_measured_rpm;
    data->tune_left_error_rpm = data->tune_left_target_rpm -
        data->tune_left_measured_rpm;
    data->tune_right_error_rpm = data->tune_right_target_rpm -
        data->tune_right_measured_rpm;
    data->tune_heading_error_deg = chassis->heading_error_deg;
    data->tune_heading_correction_rpm = chassis->heading_correction_rpm;
    data->tune_output_permitted = chassis->output_permitted;
    data->tune_boost_active = chassis->output_permitted != 0U &&
        chassis->speed_phase == (uint8_t) CHASSIS_SPEED_PHASE_BOOST;
    if (chassis->output_permitted != 0U) {
        const motor_profile_t *profile = MotorProfile_GetActive();
        int16_t left_pwm_permille = chassis->compensated_left_permille;
        int16_t right_pwm_permille = chassis->compensated_right_permille;

        if (chassis->control_mode ==
                (uint8_t) CHASSIS_ACTUATOR_MODE_ELECTRICAL &&
            profile != NULL) {
            left_pwm_permille = (int16_t) (
                chassis->applied_left_permille *
                profile->wheel[MOTOR_WHEEL_LEFT].motor_output_sign);
            right_pwm_permille = (int16_t) (
                chassis->applied_right_permille *
                profile->wheel[MOTOR_WHEEL_RIGHT].motor_output_sign);
        }
        data->tune_left_pwm_percent = (float) left_pwm_permille * 0.1f;
        data->tune_right_pwm_percent = (float) right_pwm_permille * 0.1f;
        if ((profile != NULL) && (profile->maximum_pwm_permille != 0U) &&
            ((uint16_t) ((left_pwm_permille < 0) ? -left_pwm_permille :
                left_pwm_permille) >= profile->maximum_pwm_permille ||
             (uint16_t) ((right_pwm_permille < 0) ? -right_pwm_permille :
                right_pwm_permille) >= profile->maximum_pwm_permille)) {
            data->tune_pwm_saturated = 1U;
        }
    }
    data->encoders_ready =
        g_bsp_encoder_diag.left.initialized != 0U &&
        g_bsp_encoder_diag.right.initialized != 0U;
    data->reflectance_mask = g_bsp_reflectance_diag.valid_channel_mask;
    data->line_runtime_calibration_state =
        g_h_mission_diag.line_runtime_calibration_state;
    data->line_runtime_calibration_mask =
        g_h_mission_diag.line_runtime_calibration_mask;
    data->line_runtime_calibration_samples =
        g_h_mission_diag.line_runtime_calibration_samples;
    data->line_runtime_calibration_applied =
        g_h_mission_diag.line_runtime_calibration_applied;
    data->line_runtime_white_captured =
        g_h_mission_diag.line_runtime_white_captured;
    data->line_runtime_black_captured =
        g_h_mission_diag.line_runtime_black_captured;
    data->reflectance_backend = ECHO_REFLECTANCE_BACKEND;
    data->esp_ready = g_bsp_esp_uart_diag.initialized != 0U &&
        g_esp_uart_link_test.link_online != 0U;
    data->esp_rtt_us = g_esp_uart_link_test.average_rtt_us;
    data->lidar_online = 0U;
#if ECHO_ENABLE_BALL_VISION
    if (BallVision_GetSnapshot(BSP_Time_GetUs(), &vision)) {
        data->vision_online = vision.online;
        data->vision_valid = vision.control_valid;
    }
#endif
    data->zdt_gen1_online =
        g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN1].online;
    data->zdt_gen2_online =
        g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN2].online;
    data->zdt_gen1_available =
        BSP_ZdtUart_IsAvailable(BSP_ZDT_UART_GEN1);
    data->zdt_gen2_available =
        BSP_ZdtUart_IsAvailable(BSP_ZDT_UART_GEN2);
    data->health_check_pass = data->supply_valid != 0U &&
        (ECHO_ENABLE_IMU == 0U || data->imu_ready != 0U) &&
        data->encoders_ready != 0U &&
        (ECHO_ENABLE_OLED == 0U || data->health.oled_online != 0U) &&
        data->health.i2c_error_count == 0U &&
        (ECHO_ENABLE_REFLECTANCE == 0U ||
            data->reflectance_mask == 0xFFU) &&
        (ECHO_ENABLE_ESP_LINK == 0U || data->esp_ready != 0U) &&
        (ECHO_ENABLE_BALL_VISION == 0U || data->vision_online != 0U) &&
        (data->zdt_gen1_available == 0U ||
            data->zdt_gen1_online != 0U) &&
        (data->zdt_gen2_available == 0U ||
            data->zdt_gen2_online != 0U);
    metadata = ParameterService_GetMetadataByIndex(
        data->competition.advanced_parameter_index);
    data->advanced_metadata = metadata;
    if (metadata != NULL) {
        (void) ParameterService_GetValue(metadata->id,
            &data->advanced_value);
    }
    CompetitionPage_Render(data);
}

void DisplayTask_Init(void)
{
    memset((void *) &g_display_task_diag, 0,
        sizeof(g_display_task_diag));
    g_display_debug_refresh_enable = 1U;
    g_display_debug_force_offline = ECHO_ENABLE_OLED ? 0U : 1U;
    s_attitude_display_zero_valid = 0U;
    g_display_task_diag.last_i2c_result =
        (uint32_t) BSP_I2C_RESULT_NOT_INITIALIZED;
    BSP_I2C_Init();
    UiInput_Init();
}

void DisplayTask_Entry(void *context)
{
    bool refresh_in_progress = false;

    (void) context;
    vTaskDelay(DISPLAY_POWER_UP_DELAY);
    for (;;) {
        ui_input_event_t event = UiInput_PollEvent();
        TickType_t delay_ticks;
        uint32_t now_ms = (uint32_t) xTaskGetTickCount() *
            (uint32_t) portTICK_PERIOD_MS;

        g_display_task_diag.run_count++;
        g_display_task_diag.last_wake_tick = xTaskGetTickCount();
        g_rtos_diag.display_task_run_count++;
        g_rtos_diag.display_task_last_wake_tick =
            g_display_task_diag.last_wake_tick;
        if (event.kind != UI_EVENT_NONE) {
            g_display_task_diag.last_key = (uint8_t) event.key;
            g_display_task_diag.last_event_kind = (uint8_t) event.kind;
            g_display_task_diag.key_event_count++;
        }
        CompetitionService_HandleEvent(event, now_ms);
        CompetitionService_Service(now_ms);
        g_display_task_diag.page_index = g_competition_service.page;

        if (g_display_debug_force_offline != 0U) {
            if (Ssd1306_IsOnline()) {
                Ssd1306_MarkOffline();
                g_display_task_diag.forced_offline_count++;
                g_display_task_diag.offline_count++;
            }
            g_display_task_diag.online = 0U;
            vTaskDelay(DISPLAY_RETRY_PERIOD);
            continue;
        }
        if (!Ssd1306_IsOnline()) {
            bool init_success;

            if (!DisplayTask_TryBeginIoWindow()) {
                vTaskDelay(DisplayTask_GetDeferredDelay());
                continue;
            }
            s_consecutive_deferred_count = 0U;
            g_display_task_diag.init_attempt_count++;
            init_success = Ssd1306_Init();
            SerialTx_EndQuietWindow();
            if (init_success) {
                g_display_task_diag.init_success_count++;
                g_display_task_diag.online = 1U;
                g_display_task_diag.address = Ssd1306_GetAddress();
                refresh_in_progress = false;
            } else {
                g_display_task_diag.offline_count++;
            }
            vTaskDelay(init_success ? DISPLAY_ONLINE_PERIOD :
                DISPLAY_RETRY_PERIOD);
            continue;
        }
        if (g_display_debug_refresh_enable != 0U) {
            ssd1306_refresh_step_result_t refresh_result;

            if (!refresh_in_progress) {
                DisplayTask_Render();
                Ssd1306_BeginRefresh();
                refresh_in_progress = true;
            }
            if (!DisplayTask_TryBeginIoWindow()) {
                vTaskDelay(DisplayTask_GetDeferredDelay());
                continue;
            }
            refresh_result = Ssd1306_RefreshStep();
            SerialTx_EndQuietWindow();
            s_consecutive_deferred_count = 0U;
            if (refresh_result == SSD1306_REFRESH_STEP_COMPLETE) {
                refresh_in_progress = false;
                g_display_task_diag.refresh_success_count++;
                g_display_task_diag.online = 1U;
                delay_ticks = DisplayTask_ActivePeriod();
            } else if (refresh_result == SSD1306_REFRESH_STEP_FAILED) {
                refresh_in_progress = false;
                g_display_task_diag.refresh_fail_count++;
                g_display_task_diag.online = 0U;
                g_display_task_diag.offline_count++;
                delay_ticks = DISPLAY_RETRY_PERIOD;
            } else {
                delay_ticks = DISPLAY_IO_WINDOW_POLL_PERIOD;
            }
        } else {
            delay_ticks = DisplayTask_ActivePeriod();
        }
        g_display_task_diag.last_i2c_result = g_bsp_i2c_diag.last_result;
        vTaskDelay(delay_ticks);
    }
}
