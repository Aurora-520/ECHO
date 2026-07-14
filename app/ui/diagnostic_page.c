#include "diagnostic_page.h"

#include <stddef.h>

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

static uint32_t DiagnosticPage_StackValue(
    configSTACK_DEPTH_TYPE free_words)
{
    if ((uint32_t) free_words > 10000UL) {
        return 0U;
    }
    return (uint32_t) free_words;
}

static void DiagnosticPage_DrawLine(uint8_t row, const ui_line_t *line)
{
    Ssd1306_DrawText(0U, row, line->text);
}

static void DiagnosticPage_RenderStatus(
    const diagnostic_page_data_t *data)
{
    ui_line_t line;
    uint32_t rate_hz = (data->period_us > 0U) ?
        (1000000UL / data->period_us) : 0U;

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "ECHO OLED TEST");
    DiagnosticPage_DrawLine(0U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "I2C:");
    if (data->oled_online) {
        UiLine_AppendHex8(&line, data->oled_address);
        UiLine_AppendText(&line, " ONLINE");
    } else {
        UiLine_AppendText(&line, "-- OFFLINE");
    }
    DiagnosticPage_DrawLine(1U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "RATE:");
    UiLine_AppendU32(&line, rate_hz);
    UiLine_AppendText(&line, "HZ P:");
    UiLine_AppendU32(&line, data->period_us);
    DiagnosticPage_DrawLine(2U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "EXEC:");
    UiLine_AppendU32(&line, data->execution_us);
    UiLine_AppendText(&line, " JIT:");
    UiLine_AppendU32(&line, data->jitter_us);
    DiagnosticPage_DrawLine(3U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "MISS:");
    UiLine_AppendU32(&line, data->deadline_miss_count);
    UiLine_AppendText(&line, " FAULT:");
    UiLine_AppendU32(&line, data->fault_code);
    DiagnosticPage_DrawLine(4U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "KP:");
    UiLine_AppendFixed3(&line, data->kp);
    DiagnosticPage_DrawLine(5U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "KEY:");
    UiLine_AppendText(&line, UiInput_KeyName(data->last_key));
    UiLine_AppendText(&line, " CNT:");
    UiLine_AppendU32(&line, data->key_event_count);
    DiagnosticPage_DrawLine(6U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "PAGE:");
    UiLine_AppendU32(&line, (uint32_t) data->page_index + 1U);
    UiLine_AppendText(&line, "/");
    UiLine_AppendU32(&line, DIAGNOSTIC_PAGE_COUNT);
    DiagnosticPage_DrawLine(7U, &line);
}

static void DiagnosticPage_RenderRtos(
    const diagnostic_page_data_t *data)
{
    ui_line_t line;

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "RTOS DIAGNOSTICS");
    DiagnosticPage_DrawLine(0U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "SYS FREE:");
    UiLine_AppendU32(&line,
        DiagnosticPage_StackValue(data->system_stack_free_words));
    DiagnosticPage_DrawLine(1U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "SVC FREE:");
    UiLine_AppendU32(&line,
        DiagnosticPage_StackValue(data->service_stack_free_words));
    DiagnosticPage_DrawLine(2U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "TLM FREE:");
    UiLine_AppendU32(&line,
        DiagnosticPage_StackValue(data->telemetry_stack_free_words));
    DiagnosticPage_DrawLine(3U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "DSP FREE:");
    UiLine_AppendU32(&line,
        DiagnosticPage_StackValue(data->display_stack_free_words));
    DiagnosticPage_DrawLine(4U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "HEAP:");
    UiLine_AppendU32(&line, (uint32_t) data->heap_free_bytes);
    DiagnosticPage_DrawLine(5U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "I2C OK:");
    UiLine_AppendU32(&line, data->i2c_success_count);
    UiLine_AppendText(&line, " E:");
    UiLine_AppendU32(&line, data->i2c_error_count);
    DiagnosticPage_DrawLine(6U, &line);

    UiLine_Clear(&line);
    UiLine_AppendText(&line, "PAGE:");
    UiLine_AppendU32(&line, (uint32_t) data->page_index + 1U);
    UiLine_AppendText(&line, "/");
    UiLine_AppendU32(&line, DIAGNOSTIC_PAGE_COUNT);
    DiagnosticPage_DrawLine(7U, &line);
}

void DiagnosticPage_Render(const diagnostic_page_data_t *data)
{
    if (data == NULL) {
        return;
    }

    Ssd1306_Clear();
    if (data->page_index == 0U) {
        DiagnosticPage_RenderStatus(data);
    } else {
        DiagnosticPage_RenderRtos(data);
    }
}
