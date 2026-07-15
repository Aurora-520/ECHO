#include "FreeRTOS.h"
#include "app_tasks.h"
#include "bsp_reset.h"
#include "bsp_time.h"
#include "parameter_service.h"
#include "rtos_diagnostics.h"
#include "serial_rx.h"
#include "serial_tx.h"
#include "rtos_hooks.h"
#include "system_health.h"
#include "task.h"
#include "ti_msp_dl_config.h"

int main(void)
{
    BSP_Reset_Capture();
    SYSCFG_DL_init();
    BSP_Time_Init();
    SerialTx_Init();
    SerialRx_Init();
    ParameterService_Init();
    RtosDiagnostics_Init(BSP_TIME_FREQUENCY_HZ);
    SystemHealth_Init();
    AppTasks_CreateAll();
    vTaskStartScheduler();

    RtosFault_Halt(RTOS_FAULT_SCHEDULER_RETURNED, NULL, NULL, 0);
}
