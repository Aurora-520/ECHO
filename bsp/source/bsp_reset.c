#include "bsp_reset.h"

#include "ti_msp_dl_config.h"

volatile bsp_reset_diagnostics_t g_bsp_reset_diag;

void BSP_Reset_Capture(void)
{
    DL_SYSCTL_RESET_CAUSE cause;
    bsp_reset_reason_t reason = BSP_RESET_REASON_UNKNOWN;
    uint8_t valid = 1U;

    if (g_bsp_reset_diag.captured != 0U) {
        return;
    }

    cause = DL_SYSCTL_getResetCause();
    switch (cause) {
        case DL_SYSCTL_RESET_CAUSE_POR_HW_FAILURE:
        case DL_SYSCTL_RESET_CAUSE_BOR_SUPPLY_FAILURE:
            reason = BSP_RESET_REASON_POWER;
            break;
        case DL_SYSCTL_RESET_CAUSE_POR_EXTERNAL_NRST:
        case DL_SYSCTL_RESET_CAUSE_BOOTRST_EXTERNAL_NRST:
            reason = BSP_RESET_REASON_EXTERNAL;
            break;
        case DL_SYSCTL_RESET_CAUSE_POR_SW_TRIGGERED:
        case DL_SYSCTL_RESET_CAUSE_BOOTRST_SW_TRIGGERED:
        case DL_SYSCTL_RESET_CAUSE_SYSRST_SW_TRIGGERED:
        case DL_SYSCTL_RESET_CAUSE_CPURST_SW_TRIGGERED:
            reason = BSP_RESET_REASON_SOFTWARE;
            break;
        case DL_SYSCTL_RESET_CAUSE_SYSRST_DEBUG_TRIGGERED:
        case DL_SYSCTL_RESET_CAUSE_CPURST_DEBUG_TRIGGERED:
            reason = BSP_RESET_REASON_DEBUG;
            break;
        case DL_SYSCTL_RESET_CAUSE_SYSRST_WWDT0_VIOLATION:
        case DL_SYSCTL_RESET_CAUSE_SYSRST_WWDT1_VIOLATION:
            reason = BSP_RESET_REASON_WATCHDOG;
            break;
        case DL_SYSCTL_RESET_CAUSE_SYSRST_CPU_LOCKUP_VIOLATION:
            reason = BSP_RESET_REASON_CPU_LOCKUP;
            break;
        case DL_SYSCTL_RESET_CAUSE_SYSRST_FLASH_ECC_ERROR:
            reason = BSP_RESET_REASON_FLASH_ECC;
            break;
        case DL_SYSCTL_RESET_CAUSE_BOOTRST_NON_PMU_PARITY_FAULT:
        case DL_SYSCTL_RESET_CAUSE_BOOTRST_CLOCK_FAULT:
            reason = BSP_RESET_REASON_CLOCK_OR_PARITY;
            break;
        case DL_SYSCTL_RESET_CAUSE_BOR_WAKE_FROM_SHUTDOWN:
            reason = BSP_RESET_REASON_SHUTDOWN_WAKE;
            break;
        case DL_SYSCTL_RESET_CAUSE_SYSRST_BSL_EXIT:
        case DL_SYSCTL_RESET_CAUSE_SYSRST_BSL_ENTRY:
            reason = BSP_RESET_REASON_BOOTLOADER;
            break;
        case DL_SYSCTL_RESET_CAUSE_NO_RESET:
        default:
            valid = 0U;
            break;
    }

    g_bsp_reset_diag.raw_cause = (uint32_t) cause;
    g_bsp_reset_diag.reason = (uint8_t) reason;
    g_bsp_reset_diag.valid = valid;
    g_bsp_reset_diag.captured = 1U;
}

const char *BSP_Reset_ReasonName(bsp_reset_reason_t reason)
{
    switch (reason) {
        case BSP_RESET_REASON_POWER:
            return "POWER";
        case BSP_RESET_REASON_EXTERNAL:
            return "EXTERNAL";
        case BSP_RESET_REASON_SOFTWARE:
            return "SOFTWARE";
        case BSP_RESET_REASON_DEBUG:
            return "DEBUG";
        case BSP_RESET_REASON_WATCHDOG:
            return "WATCHDOG";
        case BSP_RESET_REASON_CPU_LOCKUP:
            return "CPU LOCKUP";
        case BSP_RESET_REASON_FLASH_ECC:
            return "FLASH ECC";
        case BSP_RESET_REASON_CLOCK_OR_PARITY:
            return "CLOCK/PARITY";
        case BSP_RESET_REASON_SHUTDOWN_WAKE:
            return "SHUTDOWN";
        case BSP_RESET_REASON_BOOTLOADER:
            return "BOOTLOADER";
        default:
            return "UNKNOWN";
    }
}
