#include "service_task.h"

#include <stdint.h>

#include "FreeRTOS.h"
#include "bsp_buttons.h"
#include "bsp_esp_uart.h"
#include "bsp_led.h"
#include "bsp_i2c.h"
#include "bsp_reflectance.h"
#include "bsp_supply_voltage.h"
#include "bsp_tfmini_uart.h"
#include "bsp_time.h"
#include "attitude_estimator.h"
#include "ball_balance_service.h"
#include "ball_vision.h"
#include "command_service.h"
#include "competition_service.h"
#include "esp_uart_link_test.h"
#include "h_mission_service.h"
#include "imu_service.h"
#include "motor_profile.h"
#include "queue.h"
#include "rtos_diagnostics.h"
#include "rtos_hooks.h"
#include "serial_tx.h"
#include "system_health.h"
#include "task.h"
#include "telemetry.h"
#include "tfmini_s.h"
#include "tfmini_transport_config.h"
#include "ui_input.h"
#include "vehicle_bringup_config.h"
#include "zdt_stepper.h"

#define SERVICE_TASK_PERIOD pdMS_TO_TICKS(1U)
#define SERVICE_HEARTBEAT_TIMEOUT pdMS_TO_TICKS(1500U)
#define SERVICE_HEALTH_REFRESH_PERIOD pdMS_TO_TICKS(100U)
#define SERVICE_HEALTH_TELEMETRY_PERIOD pdMS_TO_TICKS(1000U)
#define SERVICE_REFLECTANCE_SCAN_DIVIDER 1U
#define SERVICE_REFLECTANCE_TELEMETRY_DECIMATION 5U
#define SERVICE_SUPPLY_SAMPLE_PERIOD pdMS_TO_TICKS(10U)
#define SERVICE_SUPPLY_TELEMETRY_PERIOD pdMS_TO_TICKS(100U)
#define SERVICE_BUTTON_SAMPLE_PERIOD pdMS_TO_TICKS(5U)
#define SERVICE_TFMINI_TELEMETRY_PERIOD pdMS_TO_TICKS(50U)
#define SERVICE_TFMINI_I2C_PERIOD pdMS_TO_TICKS(20U)
#define SERVICE_TFMINI_I2C_ADDRESS 0x10U
#define SERVICE_TFMINI_I2C_RESPONSE_DELAY_US 1000U
#if ECHO_IMU_DIAGNOSTIC_CAPTURE
#define SERVICE_IMU_TELEMETRY_PERIOD pdMS_TO_TICKS(10U)
#define SERVICE_ATTITUDE_TELEMETRY_PERIOD pdMS_TO_TICKS(10U)
#else
#define SERVICE_IMU_TELEMETRY_PERIOD pdMS_TO_TICKS(40U)
#define SERVICE_ATTITUDE_TELEMETRY_PERIOD pdMS_TO_TICKS(40U)
#endif
#define SERVICE_ESP_LINK_TELEMETRY_PERIOD pdMS_TO_TICKS(1000U)
#define SERVICE_BALL_BALANCE_TELEMETRY_PERIOD pdMS_TO_TICKS(40U)
#define SERVICE_BALL_VISION_RX_STALL_PERIOD pdMS_TO_TICKS(80U)
#define SERVICE_BALL_VISION_RX_STARTUP_STALL_PERIOD pdMS_TO_TICKS(500U)
#define SERVICE_TFMINI_TRANSPORT_I2C 1U
#define SERVICE_TFMINI_TRANSPORT_MIGRATION 2U
#define SERVICE_TFMINI_MIGRATION_MIN_VALID_FRAMES 5U
#define SERVICE_TFMINI_MIGRATION_SETTLE_PERIOD pdMS_TO_TICKS(100U)
#define SERVICE_TFMINI_MIGRATION_COMPLETE_PERIOD pdMS_TO_TICKS(500U)

typedef enum {
    SERVICE_TFMINI_MIGRATION_WAIT_VALID = 0U,
    SERVICE_TFMINI_MIGRATION_WAIT_SWITCH_SETTLE,
    SERVICE_TFMINI_MIGRATION_WAIT_SAVE_SETTLE,
    SERVICE_TFMINI_MIGRATION_COMPLETE
} service_tfmini_migration_state_t;

volatile tfmini_i2c_migration_diagnostics_t
    g_tfmini_i2c_migration_diag;

void ServiceTask_Entry(void *context)
{
    QueueHandle_t heartbeat_queue = (QueueHandle_t) context;
    TickType_t last_wake_time = xTaskGetTickCount();
    TickType_t last_heartbeat_time = last_wake_time;
    TickType_t last_health_refresh_time = last_wake_time;
    TickType_t last_health_telemetry_time = last_wake_time;
    TickType_t last_supply_sample_time = last_wake_time;
    TickType_t last_supply_telemetry_time = last_wake_time;
    TickType_t last_button_sample_time = last_wake_time;
    TickType_t last_tfmini_telemetry_time = last_wake_time;
#if ECHO_ENABLE_IMU
    TickType_t last_imu_telemetry_time = last_wake_time;
    TickType_t last_attitude_telemetry_time = last_wake_time;
    uint32_t last_attitude_source_sequence = 0U;
#endif
    TickType_t last_esp_link_telemetry_time = last_wake_time;
    TickType_t last_ball_balance_telemetry_time = last_wake_time;
#if ECHO_ENABLE_BALL_VISION && !ECHO_BALL_VISION_USE_UART2
    TickType_t last_ball_vision_rx_progress_time = last_wake_time;
    uint32_t last_ball_vision_rx_byte_count = 0U;
#endif
#if TFMINI_S_ENABLE_UART_TO_I2C_MIGRATION
    TickType_t last_tfmini_migration_time = last_wake_time;
#else
    TickType_t last_tfmini_i2c_time = last_wake_time;
#endif
    uint32_t heartbeat_sequence = 0U;
    uint8_t reflectance_scan_divider = 0U;
    uint8_t tfmini_query_attempt_count = 0U;

    configASSERT(heartbeat_queue != NULL);

    for (;;) {
        TickType_t now;
        bsp_reflectance_sample_t reflectance_sample;
        bsp_supply_voltage_sample_t supply_sample;
        uint32_t now_us;
#if TFMINI_S_ENABLE_UART_TO_I2C_MIGRATION
        uint8_t tfmini_byte;
#endif
#if ECHO_ENABLE_BALL_VISION
        uint8_t ball_vision_byte;
#endif

        (void) xTaskDelayUntil(&last_wake_time, SERVICE_TASK_PERIOD);
        now = xTaskGetTickCount();
        g_rtos_diag.service_task_run_count++;
        g_rtos_diag.service_task_last_wake_tick = now;

        CommandService_ProcessRx();
        now_us = BSP_Time_GetUs();
        ZdtStepper_Service(now_us);
#if ECHO_ENABLE_BALL_VISION
#if ECHO_BALL_VISION_USE_UART2
        while (BSP_EspUart_TryRead(&ball_vision_byte)) {
            BallVision_ProcessByte(ball_vision_byte, now_us);
        }
#else
        while (BSP_TfminiUart_TryRead(&ball_vision_byte)) {
            BallVision_ProcessByte(ball_vision_byte, now_us);
        }
        {
            const volatile bsp_tfmini_uart_diagnostics_t *vision_uart =
                BSP_TfminiUart_GetDiagnostics();

            if (vision_uart->rx_byte_count !=
                    last_ball_vision_rx_byte_count) {
                last_ball_vision_rx_byte_count =
                    vision_uart->rx_byte_count;
                last_ball_vision_rx_progress_time = now;
            } else {
                TickType_t stall_period =
                    vision_uart->rx_byte_count == 0U ?
                    SERVICE_BALL_VISION_RX_STARTUP_STALL_PERIOD :
                    SERVICE_BALL_VISION_RX_STALL_PERIOD;

                if ((TickType_t) (
                        now - last_ball_vision_rx_progress_time) >=
                        stall_period) {
                    BSP_TfminiUart_RecoverRx();
                    last_ball_vision_rx_progress_time = now;
                }
            }
        }
#endif
        BallVision_Update(now_us);
#endif
        BallBalanceService_Service(now_us);
#if ECHO_ENABLE_OLED
        if ((TickType_t) (now - last_button_sample_time) >=
            SERVICE_BUTTON_SAMPLE_PERIOD) {
            uint8_t pressed_mask = BSP_Buttons_ReadPressedMask();

            last_button_sample_time = now;
            CompetitionService_ServicePhysicalButtons(pressed_mask,
                (uint32_t) now);
            UiInput_ServicePhysical(pressed_mask,
                (uint32_t) now);
        }
#endif
#if ECHO_ENABLE_ESP_LINK && !ECHO_BALL_VISION_USE_UART2
        EspUartLinkTest_Process(now_us);
#endif
#if ECHO_ENABLE_TFMINI
#if TFMINI_S_ENABLE_UART_TO_I2C_MIGRATION
        BSP_TfminiUart_ServiceTx();
        while (BSP_TfminiUart_TryRead(&tfmini_byte)) {
            TfminiS_ProcessByte(tfmini_byte, now_us);
        }
        TfminiS_Update(now_us);

        {
            tfmini_s_snapshot_t snapshot;

            TfminiS_GetSnapshot(now_us, &snapshot);
            g_tfmini_i2c_migration_diag.observed_valid_frame_count =
                snapshot.valid_measurement_count;
            g_tfmini_i2c_migration_diag.command_response_count =
                snapshot.command_frame_count;

            if ((g_tfmini_i2c_migration_diag.state ==
                    SERVICE_TFMINI_MIGRATION_WAIT_VALID) &&
                (snapshot.online != 0U) &&
                (snapshot.valid_measurement_count >=
                    SERVICE_TFMINI_MIGRATION_MIN_VALID_FRAMES)) {
                uint8_t command[TFMINI_S_SET_INTERFACE_BYTES];
                uint8_t length = TfminiS_BuildSetI2cCommand(command);

                g_tfmini_i2c_migration_diag.switch_attempt_count++;
                if (BSP_TfminiUart_TryWrite(command, length)) {
                    g_tfmini_i2c_migration_diag.switch_sent_count++;
                    g_tfmini_i2c_migration_diag.state =
                        SERVICE_TFMINI_MIGRATION_WAIT_SWITCH_SETTLE;
                    g_tfmini_i2c_migration_diag.last_transition_tick = now;
                    last_tfmini_migration_time = now;
                }
            } else if ((g_tfmini_i2c_migration_diag.state ==
                    SERVICE_TFMINI_MIGRATION_WAIT_SWITCH_SETTLE) &&
                ((TickType_t) (now - last_tfmini_migration_time) >=
                    SERVICE_TFMINI_MIGRATION_SETTLE_PERIOD)) {
                uint8_t command[TFMINI_S_SAVE_SETTINGS_BYTES];
                uint8_t length =
                    TfminiS_BuildSaveSettingsCommand(command);

                g_tfmini_i2c_migration_diag.save_attempt_count++;
                if (BSP_TfminiUart_TryWrite(command, length)) {
                    g_tfmini_i2c_migration_diag.save_sent_count++;
                    g_tfmini_i2c_migration_diag.state =
                        SERVICE_TFMINI_MIGRATION_WAIT_SAVE_SETTLE;
                    g_tfmini_i2c_migration_diag.last_transition_tick = now;
                    last_tfmini_migration_time = now;
                }
            } else if ((g_tfmini_i2c_migration_diag.state ==
                    SERVICE_TFMINI_MIGRATION_WAIT_SAVE_SETTLE) &&
                ((TickType_t) (now - last_tfmini_migration_time) >=
                    SERVICE_TFMINI_MIGRATION_COMPLETE_PERIOD)) {
                g_tfmini_i2c_migration_diag.state =
                    SERVICE_TFMINI_MIGRATION_COMPLETE;
                g_tfmini_i2c_migration_diag.completed_count++;
                g_tfmini_i2c_migration_diag.last_transition_tick = now;
            }
        }
#else
#if ECHO_ENABLE_IMU
        if (ImuService_ApplyPendingRecalibration(now)) {
            AttitudeEstimator_Init();
        }
        if (ImuService_NeedsBusAccess(now)) {
            SerialTx_RequestPriorityQuietWindow();
            if (SerialTx_TryBeginRequestedQuietWindow()) {
                ImuService_Process(now);
                SerialTx_EndQuietWindow();
            }
        } else {
            ImuService_Process(now);
        }
#endif

        if ((TickType_t) (now - last_tfmini_i2c_time) >=
            SERVICE_TFMINI_I2C_PERIOD) {
            SerialTx_RequestPriorityQuietWindow();
            if (SerialTx_TryBeginRequestedQuietWindow()) {
                uint8_t query[TFMINI_S_I2C_QUERY_BYTES];
                uint8_t frame[TFMINI_S_DATA_FRAME_BYTES];
                uint8_t query_length =
                    TfminiS_BuildI2cMeasurementQuery(query);
                uint8_t index;

                last_tfmini_i2c_time += SERVICE_TFMINI_I2C_PERIOD;
                if ((TickType_t) (now - last_tfmini_i2c_time) >=
                    SERVICE_TFMINI_I2C_PERIOD) {
                    last_tfmini_i2c_time = now;
                }
                if (tfmini_query_attempt_count < UINT8_MAX) {
                    tfmini_query_attempt_count++;
                }
                if (BSP_I2C_WriteReadDelay(SERVICE_TFMINI_I2C_ADDRESS,
                        query, query_length, frame,
                        TFMINI_S_DATA_FRAME_BYTES,
                        SERVICE_TFMINI_I2C_RESPONSE_DELAY_US) ==
                        BSP_I2C_RESULT_OK) {
                    now_us = BSP_Time_GetUs();
                    for (index = 0U; index < TFMINI_S_DATA_FRAME_BYTES;
                         index++) {
                        TfminiS_ProcessByte(frame[index], now_us);
                    }
                }
                SerialTx_EndQuietWindow();
            }
        }
        TfminiS_Update(now_us);
#endif
#else
#if ECHO_ENABLE_IMU
        if (ImuService_ApplyPendingRecalibration(now)) {
            AttitudeEstimator_Init();
        }
        if (ImuService_NeedsBusAccess(now)) {
            SerialTx_RequestPriorityQuietWindow();
            if (SerialTx_TryBeginRequestedQuietWindow()) {
                ImuService_Process(now);
                SerialTx_EndQuietWindow();
            }
        } else {
            ImuService_Process(now);
        }
#endif
#endif

#if ECHO_ENABLE_BALL_VISION
        if ((TickType_t) (now - last_ball_balance_telemetry_time) >=
            SERVICE_BALL_BALANCE_TELEMETRY_PERIOD) {
            ball_balance_snapshot_t ball_balance_snapshot;

            last_ball_balance_telemetry_time = now;
            if (BallBalanceService_GetSnapshot(&ball_balance_snapshot)) {
                (void) Telemetry_PublishBallBalance(
                    &ball_balance_snapshot);
            }
        }
#endif

#if ECHO_ENABLE_IMU
        {
            imu_service_snapshot_t imu_snapshot;

            if (ImuService_GetSnapshot(&imu_snapshot) &&
                (imu_snapshot.update_sequence !=
                    last_attitude_source_sequence)) {
                attitude_estimator_input_t attitude_input;
                uint8_t axis;

                last_attitude_source_sequence =
                    imu_snapshot.update_sequence;
                attitude_input.source_update_sequence =
                    imu_snapshot.update_sequence;
                attitude_input.sample_count = imu_snapshot.sample_count;
                attitude_input.timestamp_us = imu_snapshot.timestamp_us;
                for (axis = 0U; axis < 3U; axis++) {
                    attitude_input.accel_sensor_g[axis] =
                        imu_snapshot.accel_g[axis];
                    attitude_input.gyro_sensor_dps[axis] =
                        imu_snapshot.gyro_filtered_dps[axis];
                }
                attitude_input.valid = imu_snapshot.valid;
                attitude_input.ready =
                    (imu_snapshot.state ==
                        (uint8_t) IMU_SERVICE_STATE_READY) ? 1U : 0U;
                (void) AttitudeEstimator_Process(&attitude_input);
            }
        }
#endif

        /* I2C gets its due slot before ServiceTask starts the next DMA block. */
        SerialTx_Service();

#if ECHO_ENABLE_ESP_LINK
        if ((TickType_t) (now - last_esp_link_telemetry_time) >=
            SERVICE_ESP_LINK_TELEMETRY_PERIOD) {
            esp_uart_link_test_snapshot_t esp_link_snapshot;

            last_esp_link_telemetry_time = now;
            EspUartLinkTest_GetSnapshot(&esp_link_snapshot);
            (void) Telemetry_PublishEspLink(&esp_link_snapshot);
        }
#endif

#if ECHO_ENABLE_IMU
        if ((TickType_t) (now - last_attitude_telemetry_time) >=
            SERVICE_ATTITUDE_TELEMETRY_PERIOD) {
            attitude_estimator_snapshot_t attitude_snapshot;

            last_attitude_telemetry_time = now;
            if (AttitudeEstimator_GetSnapshot(&attitude_snapshot)) {
                (void) Telemetry_PublishAttitude(&attitude_snapshot);
            }
        }

        if ((TickType_t) (now - last_imu_telemetry_time) >=
            SERVICE_IMU_TELEMETRY_PERIOD) {
            imu_service_snapshot_t imu_snapshot;

            last_imu_telemetry_time = now;
            if (ImuService_GetSnapshot(&imu_snapshot)) {
                (void) Telemetry_PublishImu(&imu_snapshot);
            }
        }
#endif

#if ECHO_ENABLE_TFMINI
        if ((TickType_t) (now - last_tfmini_telemetry_time) >=
            SERVICE_TFMINI_TELEMETRY_PERIOD) {
            telemetry_tfmini_sample_t tfmini_telemetry;
#if TFMINI_S_ENABLE_UART_TO_I2C_MIGRATION
            const volatile bsp_tfmini_uart_diagnostics_t *uart_diag =
                BSP_TfminiUart_GetDiagnostics();
#else
            const volatile bsp_i2c_diagnostics_t *i2c_diag =
                BSP_I2C_GetDiagnostics();
#endif

            last_tfmini_telemetry_time = now;
            TfminiS_GetSnapshot(now_us, &tfmini_telemetry.snapshot);
#if TFMINI_S_ENABLE_UART_TO_I2C_MIGRATION
            tfmini_telemetry.uart_rx_overflow_count =
                uart_diag->rx_overflow_count;
#else
            tfmini_telemetry.uart_rx_overflow_count = 0U;
#endif
            tfmini_telemetry.query_attempt_count =
                tfmini_query_attempt_count;
#if TFMINI_S_ENABLE_UART_TO_I2C_MIGRATION
            tfmini_telemetry.reserved[0] =
                SERVICE_TFMINI_TRANSPORT_MIGRATION;
            tfmini_telemetry.reserved[1] =
                (uint8_t) g_tfmini_i2c_migration_diag.state;
            tfmini_telemetry.reserved[2] =
                (uint8_t) g_tfmini_i2c_migration_diag.save_sent_count;
#else
            tfmini_telemetry.reserved[0] = SERVICE_TFMINI_TRANSPORT_I2C;
            tfmini_telemetry.reserved[1] = (uint8_t) i2c_diag->last_result;
            tfmini_telemetry.reserved[2] =
                (i2c_diag->nack_count > UINT8_MAX) ? UINT8_MAX :
                    (uint8_t) i2c_diag->nack_count;
#endif
            (void) Telemetry_PublishTfmini(&tfmini_telemetry);
        }
#endif

#if ECHO_ENABLE_REFLECTANCE
        reflectance_scan_divider++;
        if (reflectance_scan_divider >=
            SERVICE_REFLECTANCE_SCAN_DIVIDER) {
            reflectance_scan_divider = 0U;
            if (BSP_Reflectance_Service(&reflectance_sample)) {
                HMissionService_ProcessReflectance(
                    reflectance_sample.raw,
                    reflectance_sample.scan_sequence,
                    (uint32_t) now);
                if ((reflectance_sample.scan_sequence %
                        SERVICE_REFLECTANCE_TELEMETRY_DECIMATION) == 0U) {
                    (void) Telemetry_PublishReflectance(&reflectance_sample);
                }
            }
        }
#endif

        if ((TickType_t) (now - last_supply_sample_time) >=
            SERVICE_SUPPLY_SAMPLE_PERIOD) {
            last_supply_sample_time = now;
            if (BSP_SupplyVoltage_Sample(&supply_sample) &&
                (TickType_t) (now - last_supply_telemetry_time) >=
                    SERVICE_SUPPLY_TELEMETRY_PERIOD) {
                last_supply_telemetry_time = now;
                (void) Telemetry_PublishSupplyVoltage(&supply_sample);
            }
        }

        if (xQueueReceive(
                heartbeat_queue, &heartbeat_sequence, 0U) == pdPASS) {
            last_heartbeat_time = now;
            g_rtos_diag.queue_receive_count++;
            g_rtos_diag.last_heartbeat_sequence = heartbeat_sequence;

            BSP_LED_Toggle();
            g_rtos_diag.led_toggle_count++;
            g_rtos_diag.last_led_toggle_tick = now;
            RtosDiagnostics_Refresh();
        } else if ((TickType_t) (now - last_heartbeat_time) >=
                   SERVICE_HEARTBEAT_TIMEOUT) {
            RtosFault_Halt(RTOS_FAULT_HEARTBEAT_TIMEOUT,
                xTaskGetCurrentTaskHandle(), "Service", 0);
        }

        if ((TickType_t) (now - last_health_refresh_time) >=
            SERVICE_HEALTH_REFRESH_PERIOD) {
            system_health_snapshot_t health;

            last_health_refresh_time = now;
            SystemHealth_ServiceRefresh();
            if ((TickType_t) (now - last_health_telemetry_time) >=
                SERVICE_HEALTH_TELEMETRY_PERIOD) {
                last_health_telemetry_time = now;
                if (SystemHealth_GetSnapshot(&health)) {
                    (void) Telemetry_PublishHealth(&health);
                }
                (void) Telemetry_PublishMotorProfile(
                    MotorProfile_GetActive());
            }
        }
    }
}
