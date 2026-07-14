#ifndef ECHO_DIAGNOSTIC_PAGE_H
#define ECHO_DIAGNOSTIC_PAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "ui_input.h"

#define DIAGNOSTIC_PAGE_COUNT 2U

typedef struct {
    bool oled_online;
    uint8_t oled_address;
    uint8_t page_index;
    ui_key_t last_key;
    uint32_t key_event_count;
    uint32_t period_us;
    uint32_t execution_us;
    uint32_t jitter_us;
    uint32_t deadline_miss_count;
    uint32_t fault_code;
    float kp;
    configSTACK_DEPTH_TYPE system_stack_free_words;
    configSTACK_DEPTH_TYPE service_stack_free_words;
    configSTACK_DEPTH_TYPE telemetry_stack_free_words;
    configSTACK_DEPTH_TYPE display_stack_free_words;
    size_t heap_free_bytes;
    uint32_t i2c_success_count;
    uint32_t i2c_error_count;
} diagnostic_page_data_t;

void DiagnosticPage_Render(const diagnostic_page_data_t *data);

#endif
