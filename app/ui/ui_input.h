#ifndef ECHO_UI_INPUT_H
#define ECHO_UI_INPUT_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_KEY_NONE = 0,
    UI_KEY_UP = 1,
    UI_KEY_DOWN = 2,
    UI_KEY_LEFT = 3,
    UI_KEY_RIGHT = 4,
    UI_KEY_OK = 5
} ui_key_t;

/*
 * Write 1..5 to this symbol from a debugger Watch window to simulate a key.
 * DisplayTask consumes each nonzero value once and clears it.
 */
extern volatile uint32_t g_ui_debug_key_request;

void UiInput_Init(void);
bool UiInput_Inject(ui_key_t key);
ui_key_t UiInput_Poll(void);
const char *UiInput_KeyName(ui_key_t key);

#endif
