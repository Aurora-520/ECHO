#ifndef ECHO_BSP_MOTOR_H
#define ECHO_BSP_MOTOR_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BSP_MOTOR_LEFT = 0U,
    BSP_MOTOR_RIGHT = 1U,
    BSP_MOTOR_COUNT
} bsp_motor_channel_t;

typedef struct {
    int16_t applied_permille[BSP_MOTOR_COUNT];
    uint32_t update_count;
    uint32_t force_safe_count;
    uint32_t rejected_command_count;
    uint8_t initialized;
    uint8_t timer_running;
    uint8_t output_active;
    uint8_t reserved;
} bsp_motor_diagnostics_t;

/* Watch/debug readers must treat this as read-only. */
extern volatile bsp_motor_diagnostics_t g_bsp_motor_diag;

void BSP_Motor_Init(void);
void BSP_Motor_ForceSafe(void);
bool BSP_Motor_SetFastDecay(
    bsp_motor_channel_t channel, int16_t electrical_permille);

#endif
