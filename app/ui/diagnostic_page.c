#include "diagnostic_page.h"

#include <stddef.h>

#include "FreeRTOS.h"
#include "bsp_reset.h"
#include "ssd1306.h"

#define UI_LINE_MAX_CHARS 21U

typedef struct {
    char text[UI_LINE_MAX_CHARS + 1U];
    uint8_t length;
} ui_line_t;

static void UiLine_Clear(ui_line_t *line)
{
    line->length = 0U;
    line->text[0] = '\0';
}

static void UiLine_AppendChar(ui_line_t *line, char character)
{
    if (line->length < UI_LINE_MAX_CHARS) {
        line->text[line->length] = character;
        line->length++;
        line->text[line->length] = '\0';
    }
}

static void UiLine_AppendText(ui_line_t *line, const char *text)
{
    while ((text != NULL) && (*text != '\0') &&
        (line->length < UI_LINE_MAX_CHARS)) {
        UiLine_AppendChar(line, *text);
        text++;
    }
}

static void UiLine_AppendU32(ui_line_t *line, uint32_t value)
{
    char digits[10];
    uint8_t count = 0U;

    do {
        digits[count] = (char) ('0' + (value % 10U));
        value /= 10U;
        count++;
    } while ((value != 0U) && (count < (uint8_t) sizeof(digits)));

    while (count > 0U) {
        count--;
        UiLine_AppendChar(line, digits[count]);
    }
}

static void UiLine_AppendPadded3(ui_line_t *line, uint32_t value)
{
    UiLine_AppendChar(line, (char) ('0' + ((value / 100U) % 10U)));
    UiLine_AppendChar(line, (char) ('0' + ((value / 10U) % 10U)));
    UiLine_AppendChar(line, (char) ('0' + (value % 10U)));
}

static void UiLine_AppendFixed3(ui_line_t *line, float value)
{
    float scaled = value * 1000.0f;
    int32_t milli;
    uint32_t magnitude;

    if (scaled > 9999999.0f) {
        scaled = 9999999.0f;
    } else if (scaled < -9999999.0f) {
        scaled = -9999999.0f;
    }
    milli = (scaled >= 0.0f) ?
        (int32_t) (scaled + 0.5f) : (int32_t) (scaled - 0.5f);
    if (milli < 0) {
        UiLine_AppendChar(line, '-');
        magnitude = 0U - (uint32_t) milli;
    } else {
        magnitude = (uint32_t) milli;
    }
    UiLine_AppendU32(line, magnitude / 1000U);
    UiLine_AppendChar(line, '.');
    UiLine_AppendPadded3(line, magnitude % 1000U);
}

static void UiLine_AppendHex8(ui_line_t *line, uint8_t value)
{
    static const char hex_digits[] = "0123456789ABCDEF";

    UiLine_AppendChar(line, hex_digits[(value >> 4U) & 0x0FU]);
    UiLine_AppendChar(line, hex_digits[value & 0x0FU]);
}

static void DiagnosticPage_DrawLine(uint8_t row, const ui_line_t *line)
{
    Ssd1306_DrawText(0U, row, line->text);
}

static void DiagnosticPage_DrawFooter(uint8_t page_index)
{
    ui_line_t line;

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "PAGE ");
    UiLine_AppendU32(&line, (uint32_t) page_index + 1U);
    UiLine_AppendText(&line, "/");
    UiLine_AppendU32(&line, DIAGNOSTIC_PAGE_COUNT);
    DiagnosticPage_DrawLine(7U, &line);
}

static void DiagnosticPage_RenderOverview(
    const diagnostic_page_data_t *data)
{
    const system_health_issue_descriptor_t *active =
        SystemHealth_GetIssueDescriptor(
            (system_health_issue_t) data->health.active_issue);
    const system_health_issue_descriptor_t *first =
        SystemHealth_GetIssueDescriptor(
            (system_health_issue_t) data->health.first_fault_issue);
    ui_line_t line;

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "ECHO P1F OVERVIEW");
    DiagnosticPage_DrawLine(0U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "UP:");
    UiLine_AppendU32(&line,
        data->health.uptime_ticks / (uint32_t) configTICK_RATE_HZ);
    UiLine_AppendText(&line, "s H:");
    UiLine_AppendText(&line, SystemHealth_LevelName(
        (system_health_level_t) data->health.level));
    DiagnosticPage_DrawLine(1U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "ACTIVE:");
    UiLine_AppendText(&line, active->short_name);
    DiagnosticPage_DrawLine(2U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "FIRST:");
    UiLine_AppendText(&line, data->health.first_fault_valid ?
        first->short_name : "NONE");
    DiagnosticPage_DrawLine(3U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "SNAP:");
    UiLine_AppendU32(&line, data->health.update_sequence);
    UiLine_AppendText(&line, " STICKY:");
    UiLine_AppendText(&line,
        (data->health.sticky_issue_mask != 0U) ? "Y" : "N");
    DiagnosticPage_DrawLine(4U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "RESET:");
    UiLine_AppendText(&line, data->health.reset_reason_valid ?
        BSP_Reset_ReasonName(
            (bsp_reset_reason_t) data->health.reset_reason) : "UNKNOWN");
    DiagnosticPage_DrawLine(5U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "OUTPUT:");
    UiLine_AppendText(&line,
        data->health.actuator_output_permitted ? "ENABLED" : "LOCKED");
    DiagnosticPage_DrawLine(6U, &line);
    DiagnosticPage_DrawFooter(data->page_index);
}

static void DiagnosticPage_RenderRtos(const diagnostic_page_data_t *data)
{
    ui_line_t line;

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "RTOS");
    DiagnosticPage_DrawLine(0U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "PER:");
    UiLine_AppendU32(&line, data->health.system_period_us);
    UiLine_AppendText(&line, " EXE:");
    UiLine_AppendU32(&line, data->health.system_execution_us);
    DiagnosticPage_DrawLine(1U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "JIT:");
    UiLine_AppendU32(&line, data->health.system_jitter_us);
    UiLine_AppendText(&line, " MISS:");
    UiLine_AppendU32(&line, data->health.system_deadline_miss_count);
    DiagnosticPage_DrawLine(2U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "STACK S:");
    UiLine_AppendU32(&line, data->health.system_stack_free_words);
    UiLine_AppendText(&line, " V:");
    UiLine_AppendU32(&line, data->health.service_stack_free_words);
    DiagnosticPage_DrawLine(3U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "STACK T:");
    UiLine_AppendU32(&line, data->health.telemetry_stack_free_words);
    UiLine_AppendText(&line, " D:");
    UiLine_AppendU32(&line, data->health.display_stack_free_words);
    DiagnosticPage_DrawLine(4U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "IDLE:");
    UiLine_AppendU32(&line, data->health.idle_stack_free_words);
    UiLine_AppendText(&line, " TMR:");
    UiLine_AppendU32(&line, data->health.timer_stack_free_words);
    DiagnosticPage_DrawLine(5U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "HEAP:");
    UiLine_AppendU32(&line, data->health.heap_free_bytes);
    UiLine_AppendText(&line, " MIN:");
    UiLine_AppendU32(&line, data->health.heap_min_ever_free_bytes);
    DiagnosticPage_DrawLine(6U, &line);
    DiagnosticPage_DrawFooter(data->page_index);
}

static void DiagnosticPage_RenderComm(const diagnostic_page_data_t *data)
{
    ui_line_t line;

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "COMM");
    DiagnosticPage_DrawLine(0U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "RX OVF:");
    UiLine_AppendU32(&line, data->health.serial_rx_overflow_count);
    DiagnosticPage_DrawLine(1U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "TX DROP:");
    UiLine_AppendU32(&line, data->health.serial_tx_drop_count);
    UiLine_AppendText(&line, " HW:");
    UiLine_AppendU32(&line, data->health.serial_ring_high_water_bytes);
    DiagnosticPage_DrawLine(2U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "TLM Q:");
    UiLine_AppendU32(&line, data->health.telemetry_publish_drop_count);
    UiLine_AppendText(&line, " X:");
    UiLine_AppendU32(&line, data->health.telemetry_transport_drop_count);
    DiagnosticPage_DrawLine(3U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "PARAM ERR:");
    UiLine_AppendU32(&line, data->health.parameter_error_count);
    DiagnosticPage_DrawLine(4U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "PARAM:");
    UiLine_AppendText(&line, ParameterService_StatusName(
        (parameter_status_t) data->health.parameter_last_status));
    UiLine_AppendText(&line, " P:");
    UiLine_AppendText(&line, data->health.parameter_pending ? "Y" : "N");
    DiagnosticPage_DrawLine(5U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "HEALTH SEQ:");
    UiLine_AppendU32(&line, data->health.update_sequence);
    DiagnosticPage_DrawLine(6U, &line);
    DiagnosticPage_DrawFooter(data->page_index);
}

static void DiagnosticPage_RenderDevice(
    const diagnostic_page_data_t *data)
{
    ui_line_t line;

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "DEVICE");
    DiagnosticPage_DrawLine(0U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "OLED:");
    if (data->health.oled_online) {
        UiLine_AppendHex8(&line, data->health.oled_address);
        UiLine_AppendText(&line, " ONLINE");
    } else {
        UiLine_AppendText(&line, "-- OFFLINE");
    }
    DiagnosticPage_DrawLine(1U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "I2C OK:");
    UiLine_AppendU32(&line, data->health.i2c_success_count);
    UiLine_AppendText(&line, " E:");
    UiLine_AppendU32(&line, data->health.i2c_error_count);
    DiagnosticPage_DrawLine(2U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "QUIET A:");
    UiLine_AppendU32(&line, data->health.quiet_acquired_count);
    UiLine_AppendText(&line, " R:");
    UiLine_AppendU32(&line, data->health.quiet_released_count);
    DiagnosticPage_DrawLine(3U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "QUIET MAX:");
    UiLine_AppendU32(&line, data->health.max_quiet_window_us);
    UiLine_AppendText(&line, data->health.quiet_window_active ? " ACTIVE" : "");
    DiagnosticPage_DrawLine(4U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "STORAGE:DEFERRED");
    DiagnosticPage_DrawLine(5U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "ACTUATOR:LOCKED");
    DiagnosticPage_DrawLine(6U, &line);
    DiagnosticPage_DrawFooter(data->page_index);
}

static void DiagnosticPage_RenderParameter(
    const diagnostic_page_data_t *data)
{
    const parameter_metadata_t *metadata = data->parameter_metadata;
    ui_line_t line;

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "PARAM ");
    UiLine_AppendU32(&line, (uint32_t) data->parameter_index + 1U);
    UiLine_AppendText(&line, "/");
    UiLine_AppendU32(&line, PARAMETER_COUNT);
    UiLine_AppendText(&line, " ");
    if (data->parameter_mode == PARAMETER_UI_EDIT) {
        UiLine_AppendText(&line, "EDIT");
    } else if (data->parameter_mode == PARAMETER_UI_DEFAULT_CONFIRM) {
        UiLine_AppendText(&line, "CONFIRM");
    } else {
        UiLine_AppendText(&line, "BROWSE");
    }
    DiagnosticPage_DrawLine(0U, &line);

    if (data->parameter_mode == PARAMETER_UI_DEFAULT_CONFIRM) {
        UiLine_Clear(&line);
        UiLine_AppendText(&line, "RESET ALL DEFAULTS");
        DiagnosticPage_DrawLine(1U, &line);
        UiLine_Clear(&line);
        UiLine_AppendText(&line, "CONFIRM?");
        DiagnosticPage_DrawLine(2U, &line);
    } else if (metadata != NULL) {
        UiLine_Clear(&line);
        UiLine_AppendText(&line, metadata->name);
        UiLine_AppendText(&line,
            (data->parameter_mode == PARAMETER_UI_EDIT) ? ">" : "=");
        UiLine_AppendFixed3(&line,
            (data->parameter_mode == PARAMETER_UI_EDIT) ?
            data->parameter_draft_value : data->parameter_value);
        if (metadata->units[0] != '\0') {
            UiLine_AppendText(&line, " ");
            UiLine_AppendText(&line, metadata->units);
        }
        DiagnosticPage_DrawLine(1U, &line);

        UiLine_Clear(&line);
        UiLine_AppendText(&line, "MIN:");
        UiLine_AppendFixed3(&line, metadata->minimum_value);
        DiagnosticPage_DrawLine(2U, &line);

        UiLine_Clear(&line);
        UiLine_AppendText(&line, "MAX:");
        UiLine_AppendFixed3(&line, metadata->maximum_value);
        DiagnosticPage_DrawLine(3U, &line);

        UiLine_Clear(&line);
        UiLine_AppendText(&line, "STEP:");
        UiLine_AppendFixed3(&line, metadata->step);
        DiagnosticPage_DrawLine(4U, &line);
    }

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "SEQ:");
    UiLine_AppendU32(&line, data->health.parameter_apply_sequence);
    UiLine_AppendText(&line, " PEND:");
    UiLine_AppendText(&line, data->health.parameter_pending ? "Y" : "N");
    DiagnosticPage_DrawLine(5U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "RESULT:");
    UiLine_AppendText(&line, ParameterService_StatusName(
        (parameter_status_t) data->parameter_result));
    DiagnosticPage_DrawLine(6U, &line);
    DiagnosticPage_DrawFooter(data->page_index);
}

void DiagnosticPage_Render(const diagnostic_page_data_t *data)
{
    if (data == NULL) {
        return;
    }

    Ssd1306_Clear();
    switch ((diagnostic_page_t) data->page_index) {
        case DIAGNOSTIC_PAGE_OVERVIEW:
            DiagnosticPage_RenderOverview(data);
            break;
        case DIAGNOSTIC_PAGE_RTOS:
            DiagnosticPage_RenderRtos(data);
            break;
        case DIAGNOSTIC_PAGE_COMM:
            DiagnosticPage_RenderComm(data);
            break;
        case DIAGNOSTIC_PAGE_DEVICE:
            DiagnosticPage_RenderDevice(data);
            break;
        case DIAGNOSTIC_PAGE_PARAMETER:
            DiagnosticPage_RenderParameter(data);
            break;
        default:
            DiagnosticPage_RenderOverview(data);
            break;
    }
}
