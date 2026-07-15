#ifndef ECHO_BSP_RESET_H
#define ECHO_BSP_RESET_H

#include <stdint.h>

typedef enum {
    BSP_RESET_REASON_UNKNOWN = 0U,
    BSP_RESET_REASON_POWER,
    BSP_RESET_REASON_EXTERNAL,
    BSP_RESET_REASON_SOFTWARE,
    BSP_RESET_REASON_DEBUG,
    BSP_RESET_REASON_WATCHDOG,
    BSP_RESET_REASON_CPU_LOCKUP,
    BSP_RESET_REASON_FLASH_ECC,
    BSP_RESET_REASON_CLOCK_OR_PARITY,
    BSP_RESET_REASON_SHUTDOWN_WAKE,
    BSP_RESET_REASON_BOOTLOADER
} bsp_reset_reason_t;

typedef struct {
    uint32_t raw_cause;
    uint8_t reason;
    uint8_t valid;
    uint8_t captured;
    uint8_t reserved;
} bsp_reset_diagnostics_t;

extern volatile bsp_reset_diagnostics_t g_bsp_reset_diag;

/* Call once before SYSCFG_DL_init so later initialization cannot obscure it. */
void BSP_Reset_Capture(void);
const char *BSP_Reset_ReasonName(bsp_reset_reason_t reason);

#endif
