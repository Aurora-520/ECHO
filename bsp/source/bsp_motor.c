#include "bsp_motor.h"

#include "ti_msp_dl_config.h"

#define BSP_MOTOR_PERMILLE_MAX       1000
#define BSP_MOTOR_PWM_PERIOD_COUNTS  400U

volatile bsp_motor_diagnostics_t g_bsp_motor_diag;

static const DL_TIMER_CC_INDEX s_input_index[BSP_MOTOR_COUNT][2] = {
    { DL_TIMER_CC_0_INDEX, DL_TIMER_CC_1_INDEX },
    { DL_TIMER_CC_2_INDEX, DL_TIMER_CC_3_INDEX }
};

static void BSP_Motor_ForceChannelLow(DL_TIMER_CC_INDEX index)
{
    DL_Timer_overrideCCPOut(CHASSIS_PWM_INST, DL_TIMER_FORCE_OUT_LOW,
        DL_TIMER_FORCE_CMPL_OUT_DISABLED, index);
}

static void BSP_Motor_ReleaseChannel(DL_TIMER_CC_INDEX index)
{
    DL_Timer_overrideCCPOut(CHASSIS_PWM_INST,
        DL_TIMER_FORCE_OUT_DISABLED, DL_TIMER_FORCE_CMPL_OUT_DISABLED,
        index);
}

static uint32_t BSP_Motor_CompareForPermille(uint16_t permille)
{
    uint32_t active_counts =
        ((uint32_t) permille * BSP_MOTOR_PWM_PERIOD_COUNTS + 999U) /
        1000U;

    if (active_counts == 0U) {
        active_counts = 1U;
    }
    if (active_counts >= BSP_MOTOR_PWM_PERIOD_COUNTS) {
        return 0U;
    }
    return BSP_MOTOR_PWM_PERIOD_COUNTS - active_counts;
}

void BSP_Motor_Init(void)
{
    bsp_motor_channel_t channel;

    for (channel = BSP_MOTOR_LEFT; channel < BSP_MOTOR_COUNT;
         channel = (bsp_motor_channel_t) ((uint32_t) channel + 1U)) {
        BSP_Motor_ForceChannelLow(s_input_index[channel][0]);
        BSP_Motor_ForceChannelLow(s_input_index[channel][1]);
        g_bsp_motor_diag.applied_permille[channel] = 0;
    }

    DL_TimerA_setCoreHaltBehavior(
        CHASSIS_PWM_INST, DL_TIMER_CORE_HALT_DELAYED);
    DL_TimerA_startCounter(CHASSIS_PWM_INST);

    g_bsp_motor_diag.update_count = 0U;
    g_bsp_motor_diag.force_safe_count = 0U;
    g_bsp_motor_diag.rejected_command_count = 0U;
    g_bsp_motor_diag.initialized = 1U;
    g_bsp_motor_diag.timer_running = 1U;
    g_bsp_motor_diag.output_active = 0U;
}

void BSP_Motor_ForceSafe(void)
{
    bsp_motor_channel_t channel;

    for (channel = BSP_MOTOR_LEFT; channel < BSP_MOTOR_COUNT;
         channel = (bsp_motor_channel_t) ((uint32_t) channel + 1U)) {
        BSP_Motor_ForceChannelLow(s_input_index[channel][0]);
        BSP_Motor_ForceChannelLow(s_input_index[channel][1]);
        g_bsp_motor_diag.applied_permille[channel] = 0;
    }
    g_bsp_motor_diag.output_active = 0U;
    g_bsp_motor_diag.force_safe_count++;
}

bool BSP_Motor_SetFastDecay(
    bsp_motor_channel_t channel, int16_t electrical_permille)
{
    uint16_t magnitude;
    uint32_t compare_value;
    DL_TIMER_CC_INDEX pwm_index;
    DL_TIMER_CC_INDEX low_index;

    if ((uint32_t) channel >= (uint32_t) BSP_MOTOR_COUNT ||
        electrical_permille < -BSP_MOTOR_PERMILLE_MAX ||
        electrical_permille > BSP_MOTOR_PERMILLE_MAX) {
        g_bsp_motor_diag.rejected_command_count++;
        return false;
    }

    BSP_Motor_ForceChannelLow(s_input_index[channel][0]);
    BSP_Motor_ForceChannelLow(s_input_index[channel][1]);

    if (electrical_permille == 0) {
        g_bsp_motor_diag.applied_permille[channel] = 0;
        g_bsp_motor_diag.output_active =
            (g_bsp_motor_diag.applied_permille[BSP_MOTOR_LEFT] != 0) ||
            (g_bsp_motor_diag.applied_permille[BSP_MOTOR_RIGHT] != 0);
        g_bsp_motor_diag.update_count++;
        return true;
    }

    magnitude = (electrical_permille < 0) ?
        (uint16_t) (-electrical_permille) :
        (uint16_t) electrical_permille;
    compare_value = BSP_Motor_CompareForPermille(magnitude);
    pwm_index = (electrical_permille > 0) ?
        s_input_index[channel][0] : s_input_index[channel][1];
    low_index = (electrical_permille > 0) ?
        s_input_index[channel][1] : s_input_index[channel][0];

    DL_TimerA_setCaptureCompareValue(
        CHASSIS_PWM_INST, compare_value, pwm_index);
    BSP_Motor_ForceChannelLow(low_index);
    BSP_Motor_ReleaseChannel(pwm_index);

    g_bsp_motor_diag.applied_permille[channel] = electrical_permille;
    g_bsp_motor_diag.output_active = 1U;
    g_bsp_motor_diag.update_count++;
    return true;
}
