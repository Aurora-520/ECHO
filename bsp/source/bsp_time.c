#include "bsp_time.h"

#include "ti_msp_dl_config.h"

void BSP_Time_Init(void)
{
    DL_TimerG_setTimerCount(TIMEBASE_INST, 0U);
    DL_TimerG_setCoreHaltBehavior(
        TIMEBASE_INST, DL_TIMER_CORE_HALT_IMMEDIATE);
    DL_TimerG_startCounter(TIMEBASE_INST);
}

uint32_t BSP_Time_GetUs(void)
{
    return DL_TimerG_getTimerCount(TIMEBASE_INST);
}
