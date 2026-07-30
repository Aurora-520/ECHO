#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp_i2c.h"
#include "line_follower_6ch.h"

static uint8_t s_sensor_frame[LINE_FOLLOWER6_SENSOR_FRAME_BYTES];
static uint8_t s_threshold_frame[LINE_FOLLOWER6_THRESHOLD_FRAME_BYTES];
static uint8_t s_fail_sensor_read;
static uint8_t s_fail_threshold_read;
static uint32_t s_write_read_count;

static void StoreU16Le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) value;
    data[1] = (uint8_t) (value >> 8);
}

static void FillFrames(void)
{
    static const uint16_t raw[LINE_FOLLOWER6_CHANNEL_COUNT] = {
        150U, 180U, 350U, 420U, 500U, 640U
    };
    static const uint16_t threshold[LINE_FOLLOWER6_CHANNEL_COUNT] = {
        100U, 200U, 300U, 400U, 550U, 600U
    };
    uint8_t index;

    s_sensor_frame[0] = 0x2BU;
    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        StoreU16Le(&s_sensor_frame[1U + (uint16_t) index * 2U],
            raw[index]);
        StoreU16Le(&s_threshold_frame[(uint16_t) index * 2U],
            threshold[index]);
    }
}

bsp_i2c_result_t BSP_I2C_WriteRead(uint8_t address,
    const uint8_t *write_data, uint16_t write_length,
    uint8_t *read_data, uint16_t read_length)
{
    uint8_t reg;

    s_write_read_count++;
    if ((address != LINE_FOLLOWER6_I2C_ADDRESS) ||
        (write_data == NULL) || (write_length != 1U) ||
        (read_data == NULL)) {
        return BSP_I2C_RESULT_INVALID_ARGUMENT;
    }

    reg = write_data[0];
    if (reg == LINE_FOLLOWER6_DIGITAL_REG) {
        if (s_fail_sensor_read != 0U) {
            return BSP_I2C_RESULT_NACK;
        }
        if (read_length != LINE_FOLLOWER6_SENSOR_FRAME_BYTES) {
            return BSP_I2C_RESULT_INVALID_ARGUMENT;
        }
        (void) memcpy(read_data, s_sensor_frame, read_length);
        return BSP_I2C_RESULT_OK;
    }
    if (reg == LINE_FOLLOWER6_THRESHOLD_FIRST_REG) {
        if (s_fail_threshold_read != 0U) {
            return BSP_I2C_RESULT_NACK;
        }
        if (read_length != LINE_FOLLOWER6_THRESHOLD_FRAME_BYTES) {
            return BSP_I2C_RESULT_INVALID_ARGUMENT;
        }
        (void) memcpy(read_data, s_threshold_frame, read_length);
        return BSP_I2C_RESULT_OK;
    }

    return BSP_I2C_RESULT_NACK;
}

int main(void)
{
    line_follower6_snapshot_t snapshot;

    FillFrames();
    LineFollower6_Reset();
    assert(LineFollower6_ParseThresholdFrame(s_threshold_frame,
        sizeof(s_threshold_frame), &snapshot) ==
        LINE_FOLLOWER6_RESULT_OK);
    assert(LineFollower6_ParseSensorFrame(s_sensor_frame,
        sizeof(s_sensor_frame), &snapshot) ==
        LINE_FOLLOWER6_RESULT_OK);
    assert(snapshot.digital_mask == 0x2BU);
    assert(snapshot.raw[0] == 150U);
    assert(snapshot.raw[5] == 640U);
    assert(snapshot.threshold[1] == 200U);
    assert(snapshot.margin[0] == 50);
    assert(snapshot.margin[1] == -20);
    assert(snapshot.margin[4] == -50);
    assert(snapshot.target_detected[0] == 1U);
    assert(snapshot.target_detected[2] == 0U);
    assert(LineFollower6_ParseSensorFrame(s_sensor_frame, 3U, &snapshot) ==
        LINE_FOLLOWER6_RESULT_INVALID_ARGUMENT);

    s_write_read_count = 0U;
    assert(LineFollower6_Init());
    assert(LineFollower6_GetSnapshot(&snapshot));
    assert(snapshot.initialized == 1U);
    assert(snapshot.online == 1U);
    assert(snapshot.init_success_count == 1U);
    assert(snapshot.sample_count == 1U);
    assert(snapshot.success_count == 1U);
    assert(snapshot.threshold_read_count == 1U);
    assert(snapshot.last_i2c_result == (uint32_t) BSP_I2C_RESULT_OK);
    assert(s_write_read_count == 2U);

    s_fail_sensor_read = 1U;
    assert(!LineFollower6_Update());
    assert(LineFollower6_GetSnapshot(&snapshot));
    assert(snapshot.online == 0U);
    assert(snapshot.failure_count == 1U);
    assert(snapshot.offline_count == 1U);
    assert(snapshot.last_i2c_result == (uint32_t) BSP_I2C_RESULT_NACK);

    s_fail_sensor_read = 0U;
    assert(LineFollower6_Update());
    assert(LineFollower6_GetSnapshot(&snapshot));
    assert(snapshot.online == 1U);
    assert(snapshot.reconnect_count == 1U);
    assert(snapshot.sample_count == 2U);
    assert(snapshot.success_count == 2U);
    assert(snapshot.consecutive_failure_count == 0U);

    s_fail_threshold_read = 1U;
    assert(!LineFollower6_ReadThresholds());
    assert(LineFollower6_GetSnapshot(&snapshot));
    assert(snapshot.threshold_failure_count == 1U);

    puts("line_follower_6ch_test: PASS");
    return 0;
}
