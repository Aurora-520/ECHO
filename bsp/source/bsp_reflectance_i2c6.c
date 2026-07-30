/*
 * Drop-in backend for the existing bsp_reflectance.h API.
 *
 * Do not compile this file together with bsp_reflectance.c. Integration only
 * replaces that one source file; all application, telemetry, UI, and mission
 * callers continue using the existing BSP_Reflectance_* interface.
 */
#include "bsp_reflectance.h"

#include <limits.h>
#include <string.h>

#include "bsp_i2c.h"
#include "bsp_time.h"
#include "line_follower_6ch.h"
#include "line_follower_6ch_config.h"

#if (LINE_FOLLOWER6_REFLECTANCE_CHANNEL_ORDER != \
        LINE_FOLLOWER6_CHANNEL_ORDER_1_TO_6) && \
    (LINE_FOLLOWER6_REFLECTANCE_CHANNEL_ORDER != \
        LINE_FOLLOWER6_CHANNEL_ORDER_6_TO_1)
#error "Confirm LINE_FOLLOWER6_REFLECTANCE_CHANNEL_ORDER before using I2C6"
#endif

#define BSP_REFLECTANCE_I2C6_ALL_CHANNELS_MASK 0xFFU

volatile bsp_reflectance_diagnostics_t g_bsp_reflectance_diag;

static uint32_t s_seen_failure_count;

static bool BSP_Reflectance_I2cResultIsTimeout(uint32_t result)
{
    return (result == (uint32_t) BSP_I2C_RESULT_MUTEX_TIMEOUT) ||
        (result == (uint32_t) BSP_I2C_RESULT_BUS_BUSY_TIMEOUT) ||
        (result == (uint32_t) BSP_I2C_RESULT_TRANSFER_TIMEOUT);
}

static void BSP_Reflectance_UpdateFailureDiagnostics(
    const line_follower6_snapshot_t *driver)
{
    if (driver->failure_count != s_seen_failure_count) {
        uint32_t failure_delta =
            driver->failure_count - s_seen_failure_count;

        g_bsp_reflectance_diag.incomplete_scan_count += failure_delta;
        if (BSP_Reflectance_I2cResultIsTimeout(
                driver->last_i2c_result)) {
            g_bsp_reflectance_diag.conversion_timeout_count +=
                failure_delta;
        }
        g_bsp_reflectance_diag.last_conversion_ok = 0U;
        if (driver->online == 0U) {
            g_bsp_reflectance_diag.valid_channel_mask = 0U;
        }
        s_seen_failure_count = driver->failure_count;
    }

    if ((driver->last_register >= LINE_FOLLOWER6_ANALOG_FIRST_REG) &&
        (driver->last_register <=
            (LINE_FOLLOWER6_ANALOG_FIRST_REG +
             (LINE_FOLLOWER6_CHANNEL_COUNT - 1U) *
                LINE_FOLLOWER6_REGISTER_STRIDE))) {
        g_bsp_reflectance_diag.selected_channel = (uint8_t) (
            (driver->last_register - LINE_FOLLOWER6_ANALOG_FIRST_REG) /
            LINE_FOLLOWER6_REGISTER_STRIDE);
    }
}

void BSP_Reflectance_Init(void)
{
    (void) memset((void *) &g_bsp_reflectance_diag, 0,
        sizeof(g_bsp_reflectance_diag));
    LineFollower6_Reset();
    s_seen_failure_count = 0U;
    g_bsp_reflectance_diag.initialized = 1U;
}

bool BSP_Reflectance_Service(bsp_reflectance_sample_t *sample)
{
    line_follower6_sample_t source;
    line_follower6_snapshot_t driver;
    uint16_t expanded[BSP_REFLECTANCE_CHANNEL_COUNT];
    uint16_t minimum;
    uint16_t maximum;
    uint8_t index;
    bool complete;

    if ((sample == NULL) ||
        (g_bsp_reflectance_diag.initialized == 0U)) {
        return false;
    }

    complete = LineFollower6_Service(BSP_Time_GetUs(), &source);
    if (!LineFollower6_GetSnapshot(&driver)) {
        return false;
    }
    BSP_Reflectance_UpdateFailureDiagnostics(&driver);
    if (!complete) {
        return false;
    }

    if (!LineFollower6_ExpandRawToLegacy8(source.raw,
            (line_follower6_channel_order_t)
                LINE_FOLLOWER6_REFLECTANCE_CHANNEL_ORDER,
            expanded)) {
        return false;
    }

    minimum = expanded[0];
    maximum = expanded[0];
    for (index = 0U; index < BSP_REFLECTANCE_CHANNEL_COUNT; index++) {
        uint16_t raw = expanded[index];

        g_bsp_reflectance_diag.raw[index] = raw;
        sample->raw[index] = raw;
        if (raw < minimum) {
            minimum = raw;
        }
        if (raw > maximum) {
            maximum = raw;
        }
    }

    g_bsp_reflectance_diag.scan_sequence = source.scan_sequence;
    g_bsp_reflectance_diag.minimum_raw = minimum;
    g_bsp_reflectance_diag.maximum_raw = maximum;
    g_bsp_reflectance_diag.sample_count =
        driver.success_count * LINE_FOLLOWER6_CHANNEL_COUNT;
    g_bsp_reflectance_diag.last_conversion_ok = 1U;
    g_bsp_reflectance_diag.valid_channel_mask =
        BSP_REFLECTANCE_I2C6_ALL_CHANNELS_MASK;

    sample->scan_sequence = source.scan_sequence;
    sample->minimum_raw = minimum;
    sample->maximum_raw = maximum;
    sample->conversion_timeout_count =
        g_bsp_reflectance_diag.conversion_timeout_count;
    sample->incomplete_scan_count =
        g_bsp_reflectance_diag.incomplete_scan_count;
    sample->sample_count = g_bsp_reflectance_diag.sample_count;
    return true;
}
