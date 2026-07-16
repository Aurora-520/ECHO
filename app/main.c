#include "FreeRTOS.h"
#include "app_tasks.h"
#include "bsp_encoder.h"
#include "bsp_reset.h"
#include "bsp_time.h"
#include "chassis_actuator.h"
#include "command_service.h"
#include "motor_profile.h"
#include "parameter_service.h"
#include "rtos_diagnostics.h"
#include "serial_rx.h"
#include "serial_tx.h"
#include "rtos_hooks.h"
#include "system_health.h"
#include "task.h"
#include "ti_msp_dl_config.h"
#include "zdt_stepper.h"

int main(void)
{
    BSP_Reset_Capture();
    SYSCFG_DL_init();
    MotorProfile_Init();
    ChassisActuator_Init();
    BSP_Time_Init();
    BSP_Encoder_Init();
    ZdtStepper_Init();
    SerialTx_Init();
    SerialRx_Init();
    ParameterService_Init();
    CommandService_Init();
    RtosDiagnostics_Init(BSP_TIME_FREQUENCY_HZ);
    SystemHealth_Init();
    AppTasks_CreateAll();
    vTaskStartScheduler();

    RtosFault_Halt(RTOS_FAULT_SCHEDULER_RETURNED, NULL, NULL, 0);
}
