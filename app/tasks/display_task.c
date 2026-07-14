#include "display_task.h"

#include <stddef.h>

#include "bsp_i2c.h"
#include "diagnostic_page.h"
#include "parameter_service.h"
#include "rtos_diagnostics.h"
#include "serial_tx.h"
#include "ssd1306.h"
#include "ui_input.h"

#define DISPLAY_POWER_UP_DELAY pdMS_TO_TICKS(100U)
#define DISPLAY_ONLINE_PERIOD  pdMS_TO_TICKS(500U)
#define DISPLAY_RETRY_PERIOD   pdMS_TO_TICKS(1000U)
#define DISPLAY_IO_WINDOW_POLL_PERIOD pdMS_TO_TICKS(1U)
#define DISPLAY_IO_WINDOW_WAIT_TICKS  pdMS_TO_TICKS(5U)
#define DISPLAY_IO_WINDOW_RETRY_PERIOD pdMS_TO_TICKS(7U)
#define DISPLAY_IO_WINDOW_MAX_DEFERRALS 8U

volatile display_task_diagnostics_t g_display_task_diag;
volatile uint32_t g_display_debug_refresh_enable = 1U;

static void DisplayTask_HandleKey(ui_key_t key)
{
    if (key == UI_KEY_NONE) {
        return;
    }

    g_display_task_diag.last_key = (uint8_t) key;
    g_display_task_diag.key_event_count++;

    switch (key) {
        case UI_KEY_UP:
        case UI_KEY_LEFT:
            if (g_display_task_diag.page_index == 0U) {
                g_display_task_diag.page_index =
                    (uint8_t) (DIAGNOSTIC_PAGE_COUNT - 1U);
            } else {
                g_display_task_diag.page_index--;
            }
            break;

        case UI_KEY_DOWN:
        case UI_KEY_RIGHT:
            g_display_task_diag.page_index++;
            if (g_display_task_diag.page_index >= DIAGNOSTIC_PAGE_COUNT) {
                g_display_task_diag.page_index = 0U;
            }
            break;

        case UI_KEY_OK:
        default:
            break;
    }
}

static uint32_t DisplayTask_I2cErrorCount(
    const volatile bsp_i2c_diagnostics_t *diag)
{
    return diag->nack_count + diag->arbitration_lost_count +
        diag->bus_busy_timeout_count + diag->transfer_timeout_count +
        diag->mutex_timeout_count + diag->fifo_error_count;
}

static void DisplayTask_Render(void)
{
    diagnostic_page_data_t data;
    rtos_ui_snapshot_t rtos;
    control_tuning_parameters_t tuning;
    const volatile bsp_i2c_diagnostics_t *i2c = BSP_I2C_GetDiagnostics();

    RtosDiagnostics_GetUiSnapshot(&rtos);
    ParameterService_GetSnapshot(&tuning);

    data.oled_online = Ssd1306_IsOnline();
    data.oled_address = Ssd1306_GetAddress();
    data.page_index = g_display_task_diag.page_index;
    data.last_key = (ui_key_t) g_display_task_diag.last_key;
    data.key_event_count = g_display_task_diag.key_event_count;
    data.period_us = rtos.system_last_period_us;
    data.execution_us = rtos.system_last_execution_us;
    data.jitter_us = rtos.system_last_jitter_us;
    data.deadline_miss_count = rtos.system_deadline_miss_count;
    data.fault_code = rtos.fault_code;
    data.kp = tuning.kp;
    data.system_stack_free_words = rtos.system_stack_min_free_words;
    data.service_stack_free_words = rtos.service_stack_min_free_words;
    data.telemetry_stack_free_words = rtos.telemetry_stack_min_free_words;
    data.display_stack_free_words = rtos.display_stack_min_free_words;
    data.heap_free_bytes = rtos.heap_free_bytes;
    data.i2c_success_count = i2c->write_success_count;
    data.i2c_error_count = DisplayTask_I2cErrorCount(i2c);

    DiagnosticPage_Render(&data);
}

static bool DisplayTask_TryBeginIoWindow(void)
{
    TickType_t start_tick = xTaskGetTickCount();

    for (;;) {
        if (SerialTx_TryBeginQuietWindow()) {
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

static TickType_t DisplayTask_GetDeferredDelay(
    uint8_t *consecutive_deferred_count)
{
    (*consecutive_deferred_count)++;
    if (*consecutive_deferred_count >= DISPLAY_IO_WINDOW_MAX_DEFERRALS) {
        *consecutive_deferred_count = 0U;
        g_display_task_diag.io_window_skipped_count++;
        return DISPLAY_ONLINE_PERIOD;
    }
    return DISPLAY_IO_WINDOW_RETRY_PERIOD;
}

void DisplayTask_Init(void)
{
    g_display_debug_refresh_enable = 1U;
    g_display_task_diag.run_count = 0U;
    g_display_task_diag.init_attempt_count = 0U;
    g_display_task_diag.init_success_count = 0U;
    g_display_task_diag.refresh_success_count = 0U;
    g_display_task_diag.refresh_fail_count = 0U;
    g_display_task_diag.offline_count = 0U;
    g_display_task_diag.io_window_acquired_count = 0U;
    g_display_task_diag.io_window_deferred_count = 0U;
    g_display_task_diag.io_window_skipped_count = 0U;
    g_display_task_diag.key_event_count = 0U;
    g_display_task_diag.last_wake_tick = 0U;
    g_display_task_diag.last_i2c_result =
        (uint32_t) BSP_I2C_RESULT_NOT_INITIALIZED;
    g_display_task_diag.online = 0U;
    g_display_task_diag.address = 0U;
    g_display_task_diag.page_index = 0U;
    g_display_task_diag.last_key = (uint8_t) UI_KEY_NONE;

    BSP_I2C_Init();
    UiInput_Init();
}

void DisplayTask_Entry(void *context)
{
    uint8_t consecutive_deferred_count = 0U;

    (void) context;
    vTaskDelay(DISPLAY_POWER_UP_DELAY);

    for (;;) {
        ui_key_t key = UiInput_Poll();
        TickType_t delay_ticks;

        g_display_task_diag.run_count++;
        g_display_task_diag.last_wake_tick = xTaskGetTickCount();
        g_rtos_diag.display_task_run_count++;
        g_rtos_diag.display_task_last_wake_tick =
            g_display_task_diag.last_wake_tick;

        DisplayTask_HandleKey(key);

        if (!Ssd1306_IsOnline()) {
            bool init_success;

            if (!DisplayTask_TryBeginIoWindow()) {
                delay_ticks = DisplayTask_GetDeferredDelay(
                    &consecutive_deferred_count);
                g_display_task_diag.last_i2c_result =
                    g_bsp_i2c_diag.last_result;
                vTaskDelay(delay_ticks);
                continue;
            }
            consecutive_deferred_count = 0U;
            g_display_task_diag.init_attempt_count++;
            init_success = Ssd1306_Init();
            SerialTx_EndQuietWindow();
            if (init_success) {
                g_display_task_diag.init_success_count++;
                g_display_task_diag.online = 1U;
                g_display_task_diag.address = Ssd1306_GetAddress();
            } else {
                g_display_task_diag.online = 0U;
                g_display_task_diag.address = 0U;
                g_display_task_diag.offline_count++;
            }
            g_display_task_diag.last_i2c_result =
                g_bsp_i2c_diag.last_result;
            vTaskDelay(init_success ? DISPLAY_ONLINE_PERIOD :
                DISPLAY_RETRY_PERIOD);
            continue;
        }

        if (Ssd1306_IsOnline() && (g_display_debug_refresh_enable != 0U)) {
            bool refresh_success;

            DisplayTask_Render();
            if (!DisplayTask_TryBeginIoWindow()) {
                delay_ticks = DisplayTask_GetDeferredDelay(
                    &consecutive_deferred_count);
                g_display_task_diag.last_i2c_result =
                    g_bsp_i2c_diag.last_result;
                vTaskDelay(delay_ticks);
                continue;
            }
            consecutive_deferred_count = 0U;
            refresh_success = Ssd1306_Refresh();
            SerialTx_EndQuietWindow();
            if (refresh_success) {
                g_display_task_diag.refresh_success_count++;
                g_display_task_diag.online = 1U;
                delay_ticks = DISPLAY_ONLINE_PERIOD;
            } else {
                g_display_task_diag.refresh_fail_count++;
                g_display_task_diag.online = 0U;
                g_display_task_diag.address = 0U;
                g_display_task_diag.offline_count++;
                delay_ticks = DISPLAY_RETRY_PERIOD;
            }
        } else if (Ssd1306_IsOnline()) {
            consecutive_deferred_count = 0U;
            g_display_task_diag.online = 1U;
            delay_ticks = DISPLAY_ONLINE_PERIOD;
        } else {
            delay_ticks = DISPLAY_RETRY_PERIOD;
        }

        g_display_task_diag.last_i2c_result = g_bsp_i2c_diag.last_result;
        vTaskDelay(delay_ticks);
    }
}
