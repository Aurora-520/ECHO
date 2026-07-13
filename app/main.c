#include "FreeRTOS.h"
#include "task.h"
#include "app_tasks.h"
#include "rtos_diagnostics.h"
#include "rtos_hooks.h"
#include "ti_msp_dl_config.h"

int main(void)
{
    SYSCFG_DL_init();
    RtosDiagnostics_Init();
    AppTasks_CreateAll();
    vTaskStartScheduler();

    RtosFault_Halt(RTOS_FAULT_SCHEDULER_RETURNED, NULL, NULL, 0);
}
