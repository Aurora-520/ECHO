#include "ui_input.h"

#include "FreeRTOS.h"
#include "task.h"

volatile uint32_t g_ui_debug_key_request;

void UiInput_Init(void)
{
    g_ui_debug_key_request = (uint32_t) UI_KEY_NONE;
}

bool UiInput_Inject(ui_key_t key)
{
    bool accepted = false;

    if ((key <= UI_KEY_NONE) || (key > UI_KEY_OK)) {
        return false;
    }

    taskENTER_CRITICAL();
    if (g_ui_debug_key_request == (uint32_t) UI_KEY_NONE) {
        g_ui_debug_key_request = (uint32_t) key;
        accepted = true;
    }
    taskEXIT_CRITICAL();
    return accepted;
}

ui_key_t UiInput_Poll(void)
{
    uint32_t request;

    taskENTER_CRITICAL();
    request = g_ui_debug_key_request;
    g_ui_debug_key_request = (uint32_t) UI_KEY_NONE;
    taskEXIT_CRITICAL();

    if ((request > (uint32_t) UI_KEY_NONE) &&
        (request <= (uint32_t) UI_KEY_OK)) {
        return (ui_key_t) request;
    }
    return UI_KEY_NONE;
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
