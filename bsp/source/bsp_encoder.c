#include "bsp_encoder.h"

#include <stddef.h>

#include "ti_msp_dl_config.h"

#define BSP_ENCODER_COUNTER_MODULUS 65536UL

volatile bsp_encoder_diagnostics_t g_bsp_encoder_diag;

static uint16_t s_left_previous_counter;
static int64_t s_left_total_counts;
static volatile int32_t s_right_interval_counts;
static volatile uint32_t s_right_edge_irq_count;
static volatile uint32_t s_right_late_irq_count;
static volatile int8_t s_right_last_direction;
static int64_t s_right_total_counts;

static int32_t BSP_Encoder_DecodeWrappedDelta(
    uint16_t current, uint16_t previous)
{
    uint16_t unsigned_delta = (uint16_t) (current - previous);

    if (unsigned_delta <= INT16_MAX) {
        return (int32_t) unsigned_delta;
    }
    return (int32_t) unsigned_delta - (int32_t) BSP_ENCODER_COUNTER_MODULUS;
}

void BSP_Encoder_Init(void)
{
    DL_TimerG_setCaptureCompareInputFilter(LEFT_ENCODER_QEI_INST,
        DL_TIMER_CC_INPUT_FILT_CPV_CONSEC_PER,
        DL_TIMER_CC_INPUT_FILT_FP_PER_8, DL_TIMER_CC_0_INDEX);
    DL_TimerG_setCaptureCompareInputFilter(LEFT_ENCODER_QEI_INST,
        DL_TIMER_CC_INPUT_FILT_CPV_CONSEC_PER,
        DL_TIMER_CC_INPUT_FILT_FP_PER_8, DL_TIMER_CC_1_INDEX);
    DL_TimerG_enableCaptureCompareInputFilter(
        LEFT_ENCODER_QEI_INST, DL_TIMER_CC_0_INDEX);
    DL_TimerG_enableCaptureCompareInputFilter(
        LEFT_ENCODER_QEI_INST, DL_TIMER_CC_1_INDEX);
    DL_TimerG_setCoreHaltBehavior(
        LEFT_ENCODER_QEI_INST, DL_TIMER_CORE_HALT_IMMEDIATE);
    DL_TimerG_setTimerCount(LEFT_ENCODER_QEI_INST, 0U);

    s_left_previous_counter = 0U;
    s_left_total_counts = 0;
    s_right_interval_counts = 0;
    s_right_edge_irq_count = 0U;
    s_right_late_irq_count = 0U;
    s_right_last_direction = 0;
    s_right_total_counts = 0;
    g_bsp_encoder_diag.left.total_counts = 0;
    g_bsp_encoder_diag.left.last_delta_counts = 0;
    g_bsp_encoder_diag.left.sample_count = 0U;
    g_bsp_encoder_diag.left.max_abs_delta_counts = 0U;
    g_bsp_encoder_diag.left.raw_counter = 0U;
    g_bsp_encoder_diag.left.initialized = 1U;
    g_bsp_encoder_diag.left.running = 0U;
    g_bsp_encoder_diag.right.total_counts = 0;
    g_bsp_encoder_diag.right.last_delta_counts = 0;
    g_bsp_encoder_diag.right.sample_count = 0U;
    g_bsp_encoder_diag.right.max_abs_delta_counts = 0U;
    g_bsp_encoder_diag.right.edge_irq_count = 0U;
    g_bsp_encoder_diag.right.late_irq_count = 0U;
    g_bsp_encoder_diag.right.max_edges_per_sample = 0U;
    g_bsp_encoder_diag.right.phase_a = 0U;
    g_bsp_encoder_diag.right.phase_b = 0U;
    g_bsp_encoder_diag.right.initialized = 1U;
    g_bsp_encoder_diag.right.running = 0U;
    g_bsp_encoder_diag.left_qei_error_count = 0U;

    DL_TimerG_enableInterrupt(
        LEFT_ENCODER_QEI_INST, DL_TIMER_INTERRUPT_QEIERR_EVENT);
    NVIC_ClearPendingIRQ(LEFT_ENCODER_QEI_INST_INT_IRQN);
    NVIC_EnableIRQ(LEFT_ENCODER_QEI_INST_INT_IRQN);
    DL_TimerG_startCounter(LEFT_ENCODER_QEI_INST);
    g_bsp_encoder_diag.left.running = 1U;

    DL_GPIO_clearInterruptStatus(GPIO_RIGHT_ENCODER_PORT,
        GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2A_PIN);
    NVIC_ClearPendingIRQ(GPIO_RIGHT_ENCODER_INT_IRQN);
    NVIC_SetPriority(GPIO_RIGHT_ENCODER_INT_IRQN, 0U);
    NVIC_EnableIRQ(GPIO_RIGHT_ENCODER_INT_IRQN);
    g_bsp_encoder_diag.right.running = 1U;
}

void BSP_Encoder_SampleLeft(bsp_encoder_sample_t *sample)
{
    uint16_t current;
    int32_t delta;
    uint32_t absolute_delta;

    if (sample == NULL) {
        return;
    }

    current = (uint16_t) DL_TimerG_getTimerCount(LEFT_ENCODER_QEI_INST);
    delta = BSP_Encoder_DecodeWrappedDelta(
        current, s_left_previous_counter);
    s_left_previous_counter = current;
    s_left_total_counts += (int64_t) delta;
    absolute_delta = (delta < 0) ? (uint32_t) (-delta) : (uint32_t) delta;

    g_bsp_encoder_diag.left.total_counts = s_left_total_counts;
    g_bsp_encoder_diag.left.last_delta_counts = delta;
    g_bsp_encoder_diag.left.sample_count++;
    g_bsp_encoder_diag.left.raw_counter = current;
    if (absolute_delta > g_bsp_encoder_diag.left.max_abs_delta_counts) {
        g_bsp_encoder_diag.left.max_abs_delta_counts = absolute_delta;
    }

    sample->total_counts = s_left_total_counts;
    sample->delta_counts = delta;
    sample->error_count = g_bsp_encoder_diag.left_qei_error_count;
    sample->edge_count = 0U;
    sample->raw_counter = current;
    sample->phase_a = 0U;
    sample->phase_b = 0U;
    sample->initialized = g_bsp_encoder_diag.left.initialized;
    sample->running = g_bsp_encoder_diag.left.running;
}

void BSP_Encoder_SampleRight(bsp_encoder_sample_t *sample)
{
    uint32_t primask;
    uint32_t pins;
    uint32_t edge_count;
    uint32_t late_irq_count;
    uint32_t absolute_delta;
    int32_t delta;

    if (sample == NULL) {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    delta = s_right_interval_counts;
    s_right_interval_counts = 0;
    edge_count = s_right_edge_irq_count;
    late_irq_count = s_right_late_irq_count;
    if (primask == 0U) {
        __enable_irq();
    }

    pins = DL_GPIO_readPins(GPIO_RIGHT_ENCODER_PORT,
        GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2A_PIN |
        GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2B_PIN);
    s_right_total_counts += (int64_t) delta;
    absolute_delta = (delta < 0) ? (uint32_t) (-delta) : (uint32_t) delta;

    g_bsp_encoder_diag.right.total_counts = s_right_total_counts;
    g_bsp_encoder_diag.right.last_delta_counts = delta;
    g_bsp_encoder_diag.right.sample_count++;
    g_bsp_encoder_diag.right.edge_irq_count = edge_count;
    g_bsp_encoder_diag.right.late_irq_count = late_irq_count;
    g_bsp_encoder_diag.right.phase_a =
        ((pins & GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2A_PIN) != 0U) ? 1U : 0U;
    g_bsp_encoder_diag.right.phase_b =
        ((pins & GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2B_PIN) != 0U) ? 1U : 0U;
    if (absolute_delta > g_bsp_encoder_diag.right.max_abs_delta_counts) {
        g_bsp_encoder_diag.right.max_abs_delta_counts = absolute_delta;
    }
    if (absolute_delta > g_bsp_encoder_diag.right.max_edges_per_sample) {
        g_bsp_encoder_diag.right.max_edges_per_sample = absolute_delta;
    }

    sample->total_counts = s_right_total_counts;
    sample->delta_counts = delta;
    sample->error_count = late_irq_count;
    sample->edge_count = edge_count;
    sample->raw_counter = 0U;
    sample->phase_a = g_bsp_encoder_diag.right.phase_a;
    sample->phase_b = g_bsp_encoder_diag.right.phase_b;
    sample->initialized = g_bsp_encoder_diag.right.initialized;
    sample->running = g_bsp_encoder_diag.right.running;
}

void LEFT_ENCODER_QEI_INST_IRQHandler(void)
{
    switch (DL_TimerG_getPendingInterrupt(LEFT_ENCODER_QEI_INST)) {
        case DL_TIMER_IIDX_QEIERR:
            g_bsp_encoder_diag.left_qei_error_count++;
            break;
        default:
            break;
    }
}

void GROUP1_IRQHandler(void)
{
    uint32_t status = DL_GPIO_getEnabledInterruptStatus(
        GPIO_RIGHT_ENCODER_PORT,
        GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2A_PIN);

    if ((status & GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2A_PIN) != 0U) {
        uint32_t pins = DL_GPIO_readPins(GPIO_RIGHT_ENCODER_PORT,
            GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2A_PIN |
            GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2B_PIN);
        int8_t direction = s_right_last_direction;

        if ((pins & GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2A_PIN) == 0U) {
            s_right_late_irq_count++;
        } else {
            direction =
                ((pins & GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2B_PIN) == 0U) ?
                1 : -1;
            s_right_last_direction = direction;
        }
        if (direction > 0) {
            s_right_interval_counts++;
        } else if (direction < 0) {
            s_right_interval_counts--;
        }
        s_right_edge_irq_count++;
        DL_GPIO_clearInterruptStatus(GPIO_RIGHT_ENCODER_PORT,
            GPIO_RIGHT_ENCODER_RIGHT_ENCODER_E2A_PIN);
    }
}
