#include "line_follower_6ch.h"

#include <string.h>

#include "bsp_i2c.h"

volatile line_follower6_snapshot_t g_line_follower6_snapshot;

static uint16_t LineFollower6_DecodeU16Le(const uint8_t *data)
{
    return (uint16_t) ((uint16_t) data[0] |
        (uint16_t) ((uint16_t) data[1] << 8));
}

static void LineFollower6_UpdateMargins(line_follower6_snapshot_t *snapshot)
{
    uint8_t index;

    if (snapshot == NULL) {
        return;
    }

    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        snapshot->target_detected[index] =
            (uint8_t) ((snapshot->digital_mask >> index) & 0x01U);
        if (snapshot->threshold_valid != 0U) {
            snapshot->margin[index] =
                (int32_t) snapshot->raw[index] -
                (int32_t) snapshot->threshold[index];
        } else {
            snapshot->margin[index] = 0;
        }
    }
}

static bool LineFollower6_ReadRegisters(
    uint8_t reg, uint8_t *data, uint16_t length)
{
    bsp_i2c_result_t result;

    result = BSP_I2C_WriteRead(
        LINE_FOLLOWER6_I2C_ADDRESS, &reg, 1U, data, length);
    g_line_follower6_snapshot.last_i2c_result = (uint32_t) result;
    g_line_follower6_snapshot.last_register = reg;
    return result == BSP_I2C_RESULT_OK;
}

static void LineFollower6_RecordOnline(void)
{
    if ((g_line_follower6_snapshot.online == 0U) &&
        (g_line_follower6_snapshot.failure_count != 0U)) {
        g_line_follower6_snapshot.reconnect_count++;
    }
    g_line_follower6_snapshot.online = 1U;
    g_line_follower6_snapshot.initialized = 1U;
    g_line_follower6_snapshot.consecutive_failure_count = 0U;
    g_line_follower6_snapshot.last_error =
        (uint8_t) LINE_FOLLOWER6_RESULT_OK;
}

static void LineFollower6_RecordFailure(line_follower6_result_t error)
{
    if (g_line_follower6_snapshot.online != 0U) {
        g_line_follower6_snapshot.offline_count++;
    }
    g_line_follower6_snapshot.online = 0U;
    g_line_follower6_snapshot.failure_count++;
    g_line_follower6_snapshot.consecutive_failure_count++;
    g_line_follower6_snapshot.last_error = (uint8_t) error;
}

void LineFollower6_Reset(void)
{
    (void) memset((void *) &g_line_follower6_snapshot, 0,
        sizeof(g_line_follower6_snapshot));
    g_line_follower6_snapshot.address = LINE_FOLLOWER6_I2C_ADDRESS;
    g_line_follower6_snapshot.last_i2c_result =
        (uint32_t) BSP_I2C_RESULT_NOT_INITIALIZED;
    g_line_follower6_snapshot.last_error =
        (uint8_t) LINE_FOLLOWER6_RESULT_NOT_INITIALIZED;
}

bool LineFollower6_Init(void)
{
    bool threshold_ok;
    bool sample_ok;

    LineFollower6_Reset();
    g_line_follower6_snapshot.init_attempt_count++;

    threshold_ok = LineFollower6_ReadThresholds();
    sample_ok = LineFollower6_Update();
    if (sample_ok && threshold_ok) {
        g_line_follower6_snapshot.init_success_count++;
        return true;
    }

    g_line_follower6_snapshot.init_failure_count++;
    g_line_follower6_snapshot.initialized = sample_ok ? 1U : 0U;
    return false;
}

bool LineFollower6_ReadThresholds(void)
{
    uint8_t data[LINE_FOLLOWER6_THRESHOLD_FRAME_BYTES];

    if (!LineFollower6_ReadRegisters(LINE_FOLLOWER6_THRESHOLD_FIRST_REG,
            data, sizeof(data))) {
        g_line_follower6_snapshot.threshold_failure_count++;
        g_line_follower6_snapshot.last_error =
            (uint8_t) LINE_FOLLOWER6_RESULT_I2C_ERROR;
        return false;
    }

    if (LineFollower6_ParseThresholdFrame(data, sizeof(data),
            (line_follower6_snapshot_t *) &g_line_follower6_snapshot) !=
        LINE_FOLLOWER6_RESULT_OK) {
        g_line_follower6_snapshot.threshold_failure_count++;
        return false;
    }

    g_line_follower6_snapshot.threshold_read_count++;
    return true;
}

bool LineFollower6_Update(void)
{
    uint8_t data[LINE_FOLLOWER6_SENSOR_FRAME_BYTES];

    if (!LineFollower6_ReadRegisters(
            LINE_FOLLOWER6_DIGITAL_REG, data, sizeof(data))) {
        LineFollower6_RecordFailure(LINE_FOLLOWER6_RESULT_I2C_ERROR);
        return false;
    }

    if (LineFollower6_ParseSensorFrame(data, sizeof(data),
            (line_follower6_snapshot_t *) &g_line_follower6_snapshot) !=
        LINE_FOLLOWER6_RESULT_OK) {
        LineFollower6_RecordFailure(
            LINE_FOLLOWER6_RESULT_INVALID_ARGUMENT);
        return false;
    }

    g_line_follower6_snapshot.sample_count++;
    g_line_follower6_snapshot.success_count++;
    LineFollower6_RecordOnline();
    return true;
}

bool LineFollower6_ReadAll(void)
{
    return LineFollower6_Update();
}

bool LineFollower6_GetSnapshot(line_follower6_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }

    (void) memcpy(snapshot,
        (const void *) &g_line_follower6_snapshot,
        sizeof(*snapshot));
    return true;
}

void LineFollower6_MarkOffline(void)
{
    LineFollower6_RecordFailure(LINE_FOLLOWER6_RESULT_I2C_ERROR);
}

line_follower6_result_t LineFollower6_ParseSensorFrame(
    const uint8_t *data, uint16_t length,
    line_follower6_snapshot_t *snapshot)
{
    uint8_t index;

    if ((data == NULL) || (snapshot == NULL) ||
        (length < LINE_FOLLOWER6_SENSOR_FRAME_BYTES)) {
        return LINE_FOLLOWER6_RESULT_INVALID_ARGUMENT;
    }

    snapshot->digital_mask = (uint8_t) (data[0] & 0x3FU);
    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        snapshot->raw[index] =
            LineFollower6_DecodeU16Le(&data[1U + (uint16_t) index * 2U]);
    }
    LineFollower6_UpdateMargins(snapshot);
    return LINE_FOLLOWER6_RESULT_OK;
}

line_follower6_result_t LineFollower6_ParseThresholdFrame(
    const uint8_t *data, uint16_t length,
    line_follower6_snapshot_t *snapshot)
{
    uint8_t index;

    if ((data == NULL) || (snapshot == NULL) ||
        (length < LINE_FOLLOWER6_THRESHOLD_FRAME_BYTES)) {
        return LINE_FOLLOWER6_RESULT_INVALID_ARGUMENT;
    }

    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        snapshot->threshold[index] =
            LineFollower6_DecodeU16Le(&data[(uint16_t) index * 2U]);
    }
    snapshot->threshold_valid = 1U;
    LineFollower6_UpdateMargins(snapshot);
    return LINE_FOLLOWER6_RESULT_OK;
}
