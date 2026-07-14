#include "FreeRTOS.h"
#include "app_tasks.h"
#include "bsp_time.h"
#include "rtos_diagnostics.h"
#include "rtos_hooks.h"
#include "task.h"
#include "ti_msp_dl_config.h"

int main(void)
{
    SYSCFG_DL_init();
    BSP_Time_Init();
    RtosDiagnostics_Init(BSP_TIME_FREQUENCY_HZ);
    AppTasks_CreateAll();
    vTaskStartScheduler();

    RtosFault_Halt(RTOS_FAULT_SCHEDULER_RETURNED, NULL, NULL, 0);
}
