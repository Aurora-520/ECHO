#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp_i2c.h"
#include "bsp_reflectance.h"
#include "line_follower_6ch.h"

#define TEST_REGISTER_COUNT 30U

static uint8_t s_registers[TEST_REGISTER_COUNT];
static uint8_t s_fail_register = UINT8_MAX;
static uint32_t s_write_read_count;
static uint32_t s_now_us;

static void StoreU16Le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t) value;
    data[1] = (uint8_t) (value >> 8);
}

static void FillRegisters(void)
{
    static const uint16_t raw[LINE_FOLLOWER6_CHANNEL_COUNT] = {
        150U, 180U, 350U, 420U, 500U, 640U
    };
    static const uint16_t threshold[LINE_FOLLOWER6_CHANNEL_COUNT] = {
        100U, 200U, 300U, 400U, 550U, 600U
    };
    uint8_t index;

    (void) memset(s_registers, 0, sizeof(s_registers));
    s_registers[LINE_FOLLOWER6_DIGITAL_REG] = 0xABU;
    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        StoreU16Le(&s_registers[LINE_FOLLOWER6_ANALOG_FIRST_REG +
            (uint16_t) index * LINE_FOLLOWER6_REGISTER_STRIDE], raw[index]);
        StoreU16Le(&s_registers[LINE_FOLLOWER6_THRESHOLD_FIRST_REG +
            (uint16_t) index * LINE_FOLLOWER6_REGISTER_STRIDE],
            threshold[index]);
    }
}

static void ResetTransport(void)
{
    s_fail_register = UINT8_MAX;
    s_write_read_count = 0U;
    s_now_us = 0U;
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
    if (reg == s_fail_register) {
        return BSP_I2C_RESULT_NACK;
    }
    if ((reg == LINE_FOLLOWER6_DIGITAL_REG) && (read_length == 1U)) {
        read_data[0] = s_registers[reg];
        return BSP_I2C_RESULT_OK;
    }
    if ((read_length == 2U) &&
        (((reg >= LINE_FOLLOWER6_ANALOG_FIRST_REG) &&
          (reg < LINE_FOLLOWER6_THRESHOLD_FIRST_REG) &&
          (((reg - LINE_FOLLOWER6_ANALOG_FIRST_REG) % 2U) == 0U)) ||
         ((reg >= LINE_FOLLOWER6_THRESHOLD_FIRST_REG) &&
          (reg < TEST_REGISTER_COUNT) &&
          (((reg - LINE_FOLLOWER6_THRESHOLD_FIRST_REG) % 2U) == 0U)))) {
        read_data[0] = s_registers[reg];
        read_data[1] = s_registers[reg + 1U];
        return BSP_I2C_RESULT_OK;
    }
    return BSP_I2C_RESULT_INVALID_ARGUMENT;
}

uint32_t BSP_Time_GetUs(void)
{
    return s_now_us;
}

static void TestParsing(void)
{
    line_follower6_snapshot_t snapshot;
    uint8_t analog[LINE_FOLLOWER6_ANALOG_BYTES];
    uint8_t threshold[LINE_FOLLOWER6_THRESHOLD_BYTES];
    uint8_t index;

    (void) memset(&snapshot, 0, sizeof(snapshot));
    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        (void) memcpy(&analog[(uint16_t) index * 2U],
            &s_registers[LINE_FOLLOWER6_ANALOG_FIRST_REG +
                (uint16_t) index * LINE_FOLLOWER6_REGISTER_STRIDE], 2U);
        (void) memcpy(&threshold[(uint16_t) index * 2U],
            &s_registers[LINE_FOLLOWER6_THRESHOLD_FIRST_REG +
                (uint16_t) index * LINE_FOLLOWER6_REGISTER_STRIDE], 2U);
    }

    assert(LineFollower6_ParseThresholds(threshold, sizeof(threshold),
        &snapshot) == LINE_FOLLOWER6_RESULT_OK);
    assert(LineFollower6_ParseSample(0xABU, analog, sizeof(analog),
        &snapshot) == LINE_FOLLOWER6_RESULT_OK);
    assert(snapshot.digital_mask == 0xABU);
    assert(snapshot.raw[0] == 150U);
    assert(snapshot.raw[5] == 640U);
    assert(snapshot.threshold[1] == 200U);
    assert(snapshot.margin[0] == 50);
    assert(snapshot.margin[1] == -20);
    assert(snapshot.margin[4] == -50);
    assert(snapshot.digital_bit[0] == 1U);
    assert(snapshot.digital_bit[2] == 0U);
    assert(snapshot.minimum_raw == 150U);
    assert(snapshot.maximum_raw == 640U);
    assert(LineFollower6_ParseSample(0U, analog, 3U, &snapshot) ==
        LINE_FOLLOWER6_RESULT_INVALID_ARGUMENT);
    assert(LineFollower6_ParseThresholds(threshold, 3U, &snapshot) ==
        LINE_FOLLOWER6_RESULT_INVALID_ARGUMENT);
}

static void TestLegacyExpansion(void)
{
    const uint16_t source[LINE_FOLLOWER6_CHANNEL_COUNT] = {
        0U, 700U, 1400U, 2100U, 2800U, 3500U
    };
    uint16_t expanded[LINE_FOLLOWER6_LEGACY_CHANNEL_COUNT];
    uint8_t index;

    assert(!LineFollower6_ExpandRawToLegacy8(source,
        LINE_FOLLOWER6_CHANNEL_ORDER_UNCONFIRMED, expanded));
    assert(LineFollower6_ExpandRawToLegacy8(source,
        LINE_FOLLOWER6_CHANNEL_ORDER_1_TO_6, expanded));
    for (index = 0U; index < LINE_FOLLOWER6_LEGACY_CHANNEL_COUNT; index++) {
        assert(expanded[index] == (uint16_t) index * 500U);
    }

    assert(LineFollower6_ExpandRawToLegacy8(source,
        LINE_FOLLOWER6_CHANNEL_ORDER_6_TO_1, expanded));
    for (index = 0U; index < LINE_FOLLOWER6_LEGACY_CHANNEL_COUNT; index++) {
        assert(expanded[index] == (uint16_t) (3500U -
            (uint16_t) index * 500U));
    }
}

static void TestSynchronousApi(void)
{
    line_follower6_snapshot_t snapshot;

    ResetTransport();
    assert(LineFollower6_Init());
    assert(s_write_read_count == 13U);
    assert(LineFollower6_GetSnapshot(&snapshot));
    assert(snapshot.initialized == 1U);
    assert(snapshot.online == 1U);
    assert(snapshot.init_success_count == 1U);
    assert(snapshot.sample_count == 1U);
    assert(snapshot.success_count == 1U);
    assert(snapshot.threshold_read_count == 1U);
    assert(snapshot.threshold_valid == 1U);

    s_fail_register = 10U;
    assert(!LineFollower6_Update());
    assert(s_write_read_count == 17U);
    assert(LineFollower6_GetSnapshot(&snapshot));
    assert(snapshot.online == 0U);
    assert(snapshot.failure_count == 1U);
    assert(snapshot.offline_count == 1U);
    assert(snapshot.threshold_valid == 0U);
    assert(snapshot.last_register == 10U);
    assert(snapshot.last_i2c_result == (uint32_t) BSP_I2C_RESULT_NACK);

    s_fail_register = UINT8_MAX;
    assert(LineFollower6_Update());
    assert(LineFollower6_GetSnapshot(&snapshot));
    assert(snapshot.online == 1U);
    assert(snapshot.reconnect_count == 1U);
    assert(snapshot.sample_count == 3U);
    assert(snapshot.success_count == 2U);
    assert(snapshot.consecutive_failure_count == 0U);
}

static bool ServiceOnce(uint32_t now_us, line_follower6_sample_t *sample)
{
    uint32_t before = s_write_read_count;
    bool complete = LineFollower6_Service(now_us, sample);

    assert((s_write_read_count - before) <= 1U);
    return complete;
}

static void TestIncrementalService(void)
{
    line_follower6_sample_t sample;
    line_follower6_snapshot_t snapshot;
    uint32_t now_us;
    uint8_t call;

    ResetTransport();
    LineFollower6_Reset();
    for (call = 0U; call < 6U; call++) {
        assert(!ServiceOnce((uint32_t) call * 1000U, &sample));
    }
    assert(ServiceOnce(6000U, &sample));
    assert(s_write_read_count == 7U);
    assert(sample.scan_sequence == 1U);
    assert(sample.raw[0] == 150U);
    assert(sample.raw[5] == 640U);

    assert(!ServiceOnce(7000U, &sample));
    assert(s_write_read_count == 8U);
    assert(!ServiceOnce(8000U, &sample));
    for (call = 9U; call < 14U; call++) {
        assert(!ServiceOnce((uint32_t) call * 1000U, &sample));
    }
    assert(ServiceOnce(14000U, &sample));
    assert(sample.scan_sequence == 2U);
    assert(LineFollower6_GetSnapshot(&snapshot));
    assert(snapshot.service_call_count == 15U);
    assert(snapshot.sample_count == 2U);
    assert(snapshot.success_count == 2U);
    assert(snapshot.last_success_us == 14000U);

    for (now_us = 15000U; now_us <= 47000U; now_us += 1000U) {
        bool expected_complete = (now_us == 22000U) ||
            (now_us == 30000U) || (now_us == 38000U) ||
            (now_us == 46000U);

        assert(ServiceOnce(now_us, &sample) == expected_complete);
    }
    assert(LineFollower6_GetSnapshot(&snapshot));
    assert(snapshot.threshold_valid == 1U);
    assert(snapshot.threshold_read_count == 1U);
    assert(snapshot.margin[0] == 50);
    assert(snapshot.service_call_count == 48U);
}

static void TestOfflineBackoffAndRecovery(void)
{
    line_follower6_sample_t sample;
    line_follower6_snapshot_t snapshot;
    uint32_t count_after_failure;
    uint8_t call;

    ResetTransport();
    LineFollower6_Reset();
    s_fail_register = LINE_FOLLOWER6_DIGITAL_REG;
    assert(!ServiceOnce(0U, &sample));
    count_after_failure = s_write_read_count;
    assert(!ServiceOnce(1000U, &sample));
    assert(!ServiceOnce(499000U, &sample));
    assert(s_write_read_count == count_after_failure);
    assert(LineFollower6_GetSnapshot(&snapshot));
    assert(snapshot.online == 0U);
    assert(snapshot.failure_count == 1U);

    s_fail_register = UINT8_MAX;
    assert(!ServiceOnce(500000U, &sample));
    for (call = 1U; call < 6U; call++) {
        assert(!ServiceOnce(500000U + (uint32_t) call * 1000U,
            &sample));
    }
    assert(ServiceOnce(506000U, &sample));
    assert(LineFollower6_GetSnapshot(&snapshot));
    assert(snapshot.online == 1U);
    assert(snapshot.reconnect_count == 1U);
    assert(snapshot.sample_count == 2U);
    assert(snapshot.success_count == 1U);
}

static void TestFiveMinuteServiceSimulation(void)
{
    line_follower6_sample_t sample;
    line_follower6_snapshot_t snapshot;
    uint32_t complete_count = 0U;
    uint32_t now_us;

    ResetTransport();
    LineFollower6_Reset();
    for (now_us = 0U; now_us < 300000000UL; now_us += 1000U) {
        if (ServiceOnce(now_us, &sample)) {
            complete_count++;
        }
    }

    assert(complete_count == 37500U);
    assert(LineFollower6_GetSnapshot(&snapshot));
    assert(snapshot.service_call_count == 300000U);
    assert(snapshot.sample_count == 37500U);
    assert(snapshot.success_count == 37500U);
    assert(snapshot.failure_count == 0U);
    assert(snapshot.threshold_read_count == 1U);
    assert(snapshot.online == 1U);
    assert(s_write_read_count == 262506U);
}

static void TestDropInReflectanceBackend(void)
{
    bsp_reflectance_sample_t sample;
    uint8_t call;

    ResetTransport();
    BSP_Reflectance_Init();
    for (call = 0U; call < 6U; call++) {
        s_now_us = (uint32_t) call * 1000U;
        assert(!BSP_Reflectance_Service(&sample));
    }
    s_now_us = 6000U;
    assert(BSP_Reflectance_Service(&sample));
    assert(sample.scan_sequence == 1U);
    assert(sample.raw[0] == 150U);
    assert(sample.raw[7] == 640U);
    assert(sample.minimum_raw == 150U);
    assert(sample.maximum_raw == 640U);
    assert(sample.sample_count == 6U);
    assert(g_bsp_reflectance_diag.valid_channel_mask == 0xFFU);
    assert(g_bsp_reflectance_diag.last_conversion_ok == 1U);
}

int main(void)
{
    FillRegisters();
    TestParsing();
    TestLegacyExpansion();
    TestSynchronousApi();
    TestIncrementalService();
    TestOfflineBackoffAndRecovery();
    TestFiveMinuteServiceSimulation();
    TestDropInReflectanceBackend();
    puts("line_follower_6ch_test: PASS");
    return 0;
}
