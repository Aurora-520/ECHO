#include "bsp_reflectance.h"

#include "vehicle_bringup_config.h"

#if ECHO_REFLECTANCE_BACKEND == ECHO_REFLECTANCE_BACKEND_ADC8

#include <stddef.h>
#include <string.h>

#include "bsp_time.h"
#include "ti_msp_dl_config.h"

#define BSP_REFLECTANCE_ADC_TIMEOUT_US 100U
#define BSP_REFLECTANCE_ALL_CHANNELS_MASK 0xFFU

volatile bsp_reflectance_diagnostics_t g_bsp_reflectance_diag;

static uint8_t s_selected_channel;
static uint8_t s_valid_channel_mask;
static uint8_t s_last_complete_channel_mask;

static void BSP_Reflectance_SelectChannel(uint8_t channel)
{
    uint32_t set_mask = 0U;
    const uint32_t address_mask =
        GPIO_REFLECTANCE_MUX_REFLECTANCE_AD0_PIN |
        GPIO_REFLECTANCE_MUX_REFLECTANCE_AD1_PIN |
        GPIO_REFLECTANCE_MUX_REFLECTANCE_AD2_PIN;

    if ((channel & 0x01U) != 0U) {
        set_mask |= GPIO_REFLECTANCE_MUX_REFLECTANCE_AD0_PIN;
    }
    if ((channel & 0x02U) != 0U) {
        set_mask |= GPIO_REFLECTANCE_MUX_REFLECTANCE_AD1_PIN;
    }
    if ((channel & 0x04U) != 0U) {
        set_mask |= GPIO_REFLECTANCE_MUX_REFLECTANCE_AD2_PIN;
    }

    DL_GPIO_writePinsVal(
        GPIO_REFLECTANCE_MUX_PORT, address_mask, set_mask);
}

static bool BSP_Reflectance_ReadAdc(uint16_t *value)
{
    uint32_t start_us;

    if (value == NULL) {
        return false;
    }

    DL_ADC12_clearInterruptStatus(REFLECTANCE_ADC_INST,
        DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED);
    DL_ADC12_enableConversions(REFLECTANCE_ADC_INST);
    DL_ADC12_startConversion(REFLECTANCE_ADC_INST);
    start_us = BSP_Time_GetUs();

    while ((DL_ADC12_getRawInterruptStatus(REFLECTANCE_ADC_INST,
                DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED) &
            DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED) == 0U) {
        if ((uint32_t) (BSP_Time_GetUs() - start_us) >=
            BSP_REFLECTANCE_ADC_TIMEOUT_US) {
            DL_ADC12_stopConversion(REFLECTANCE_ADC_INST);
            DL_ADC12_disableConversions(REFLECTANCE_ADC_INST);
            DL_ADC12_clearInterruptStatus(REFLECTANCE_ADC_INST,
                DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED);
            return false;
        }
    }

    *value = (uint16_t) DL_ADC12_getMemResult(
        REFLECTANCE_ADC_INST, REFLECTANCE_ADC_ADCMEM_0);
    DL_ADC12_clearInterruptStatus(REFLECTANCE_ADC_INST,
        DL_ADC12_INTERRUPT_MEM0_RESULT_LOADED);
    return true;
}

void BSP_Reflectance_Init(void)
{
    memset((void *) &g_bsp_reflectance_diag, 0,
        sizeof(g_bsp_reflectance_diag));
    s_selected_channel = 0U;
    s_valid_channel_mask = 0U;
    s_last_complete_channel_mask = 0U;
    BSP_Reflectance_SelectChannel(s_selected_channel);
    g_bsp_reflectance_diag.selected_channel = s_selected_channel;
    g_bsp_reflectance_diag.initialized = 1U;
}

bool BSP_Reflectance_Service(bsp_reflectance_sample_t *sample)
{
    uint16_t raw;
    bool complete = false;
    uint8_t next_channel;

    if ((sample == NULL) ||
        (g_bsp_reflectance_diag.initialized == 0U)) {
        return false;
    }

    if (BSP_Reflectance_ReadAdc(&raw)) {
        g_bsp_reflectance_diag.raw[s_selected_channel] = raw;
        g_bsp_reflectance_diag.sample_count++;
        g_bsp_reflectance_diag.last_conversion_ok = 1U;
        s_valid_channel_mask |= (uint8_t) (1U << s_selected_channel);
    } else {
        g_bsp_reflectance_diag.conversion_timeout_count++;
        g_bsp_reflectance_diag.last_conversion_ok = 0U;
    }

    if (s_selected_channel ==
        (uint8_t) (BSP_REFLECTANCE_CHANNEL_COUNT - 1U)) {
        if (s_valid_channel_mask == BSP_REFLECTANCE_ALL_CHANNELS_MASK) {
            uint8_t channel;
            uint16_t minimum = g_bsp_reflectance_diag.raw[0];
            uint16_t maximum = minimum;

            for (channel = 0U; channel < BSP_REFLECTANCE_CHANNEL_COUNT;
                 channel++) {
                uint16_t channel_raw =
                    g_bsp_reflectance_diag.raw[channel];

                sample->raw[channel] = channel_raw;
                if (channel_raw < minimum) {
                    minimum = channel_raw;
                }
                if (channel_raw > maximum) {
                    maximum = channel_raw;
                }
            }
            g_bsp_reflectance_diag.scan_sequence++;
            g_bsp_reflectance_diag.minimum_raw = minimum;
            g_bsp_reflectance_diag.maximum_raw = maximum;
            sample->scan_sequence =
                g_bsp_reflectance_diag.scan_sequence;
            sample->minimum_raw = minimum;
            sample->maximum_raw = maximum;
            sample->conversion_timeout_count =
                g_bsp_reflectance_diag.conversion_timeout_count;
            sample->incomplete_scan_count =
                g_bsp_reflectance_diag.incomplete_scan_count;
            sample->sample_count =
                g_bsp_reflectance_diag.sample_count;
            complete = true;
        } else {
            g_bsp_reflectance_diag.incomplete_scan_count++;
        }
        s_last_complete_channel_mask = s_valid_channel_mask;
        s_valid_channel_mask = 0U;
    }

    next_channel = (uint8_t) ((s_selected_channel + 1U) & 0x07U);
    BSP_Reflectance_SelectChannel(next_channel);
    s_selected_channel = next_channel;
    g_bsp_reflectance_diag.selected_channel = next_channel;
    /* Expose the result of the last finished scan, not the next scan's
     * transient progress. This lets diagnostics report a stable OK/FAIL. */
    g_bsp_reflectance_diag.valid_channel_mask =
        s_last_complete_channel_mask;
    return complete;
}

#endif
