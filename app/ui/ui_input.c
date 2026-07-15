#include "ui_input.h"

#include "FreeRTOS.h"
#include "task.h"

#define UI_INPUT_INACTIVITY_TIMEOUT pdMS_TO_TICKS(10000U)

volatile uint32_t g_ui_debug_key_request;
volatile uint32_t g_ui_debug_event_kind_request;

static TickType_t s_last_activity_tick;
static bool s_timeout_emitted;

void UiInput_Init(void)
{
    g_ui_debug_key_request = (uint32_t) UI_KEY_NONE;
    g_ui_debug_event_kind_request = (uint32_t) UI_EVENT_NONE;
    s_last_activity_tick = xTaskGetTickCount();
    s_timeout_emitted = false;
}

bool UiInput_InjectEvent(ui_key_t key, ui_event_kind_t kind)
{
    bool accepted = false;

    if ((key <= UI_KEY_NONE) || (key > UI_KEY_OK) ||
        (kind < UI_EVENT_PRESS) || (kind > UI_EVENT_REPEAT)) {
        return false;
    }

    taskENTER_CRITICAL();
    if (g_ui_debug_key_request == (uint32_t) UI_KEY_NONE) {
        g_ui_debug_key_request = (uint32_t) key;
        g_ui_debug_event_kind_request = (uint32_t) kind;
        accepted = true;
    }
    taskEXIT_CRITICAL();
    return accepted;
}

bool UiInput_Inject(ui_key_t key)
{
    return UiInput_InjectEvent(key, UI_EVENT_PRESS);
}

ui_input_event_t UiInput_PollEvent(void)
{
    ui_input_event_t event = { UI_KEY_NONE, UI_EVENT_NONE };
    uint32_t key_request;
    uint32_t kind_request;
    TickType_t now = xTaskGetTickCount();

    taskENTER_CRITICAL();
    key_request = g_ui_debug_key_request;
    kind_request = g_ui_debug_event_kind_request;
    g_ui_debug_key_request = (uint32_t) UI_KEY_NONE;
    g_ui_debug_event_kind_request = (uint32_t) UI_EVENT_NONE;
    taskEXIT_CRITICAL();

    if ((key_request > (uint32_t) UI_KEY_NONE) &&
        (key_request <= (uint32_t) UI_KEY_OK)) {
        if (kind_request == (uint32_t) UI_EVENT_NONE) {
            kind_request = (uint32_t) UI_EVENT_PRESS;
        }
        if ((kind_request >= (uint32_t) UI_EVENT_PRESS) &&
            (kind_request <= (uint32_t) UI_EVENT_REPEAT)) {
            event.key = (ui_key_t) key_request;
            event.kind = (ui_event_kind_t) kind_request;
            s_last_activity_tick = now;
            s_timeout_emitted = false;
            return event;
        }
    }

    if (!s_timeout_emitted &&
        (TickType_t) (now - s_last_activity_tick) >=
            UI_INPUT_INACTIVITY_TIMEOUT) {
        event.kind = UI_EVENT_TIMEOUT;
        s_timeout_emitted = true;
    }
    return event;
}

const char *UiInput_KeyName(ui_key_t key)
{
    switch (key) {
        case UI_KEY_UP:
            return "UP";
        case UI_KEY_DOWN:
            return "DOWN";
        case UI_KEY_LEFT:
            return "LEFT";
        case UI_KEY_RIGHT:
            return "RIGHT";
        case UI_KEY_OK:
            return "OK";
        default:
            return "NONE";
    }
}

const char *UiInput_EventName(ui_event_kind_t kind)
{
    switch (kind) {
        case UI_EVENT_PRESS:
            return "PRESS";
        case UI_EVENT_LONG_PRESS:
            return "LONG";
        case UI_EVENT_REPEAT:
            return "REPEAT";
        case UI_EVENT_TIMEOUT:
            return "TIMEOUT";
        default:
            return "NONE";
    }
}
