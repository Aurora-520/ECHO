#include "line_follower_6ch.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "bsp_i2c.h"

#define LINE_FOLLOWER6_SERVICE_STAGE_IDLE UINT8_MAX

volatile line_follower6_snapshot_t g_line_follower6_snapshot;

static volatile uint32_t s_snapshot_write_sequence;
static uint8_t s_service_stage;
static uint8_t s_service_digital_mask;
static uint8_t s_service_analog[LINE_FOLLOWER6_ANALOG_BYTES];
static uint8_t s_service_threshold[LINE_FOLLOWER6_THRESHOLD_BYTES];
static uint8_t s_service_threshold_channel;
static uint8_t s_service_schedule_valid;
static uint32_t s_next_frame_start_us;

static void LineFollower6_BeginSnapshotWrite(void)
{
    s_snapshot_write_sequence++;
}

static void LineFollower6_EndSnapshotWrite(void)
{
    s_snapshot_write_sequence++;
}

static uint16_t LineFollower6_DecodeU16Le(const uint8_t *data)
{
    return (uint16_t) ((uint16_t) data[0] |
        (uint16_t) ((uint16_t) data[1] << 8));
}

static void LineFollower6_UpdateDerived(
    line_follower6_snapshot_t *snapshot)
{
    uint8_t index;

    if (snapshot == NULL) {
        return;
    }

    snapshot->minimum_raw = snapshot->raw[0];
    snapshot->maximum_raw = snapshot->raw[0];
    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        snapshot->digital_bit[index] =
            (uint8_t) ((snapshot->digital_mask >> index) & 0x01U);
        if (snapshot->raw[index] < snapshot->minimum_raw) {
            snapshot->minimum_raw = snapshot->raw[index];
        }
        if (snapshot->raw[index] > snapshot->maximum_raw) {
            snapshot->maximum_raw = snapshot->raw[index];
        }
        if (snapshot->threshold_valid != 0U) {
            snapshot->margin[index] =
                (int32_t) snapshot->raw[index] -
                (int32_t) snapshot->threshold[index];
        } else {
            snapshot->margin[index] = 0;
        }
    }
}

static bool LineFollower6_TimeReached(uint32_t now_us, uint32_t due_us)
{
    return (int32_t) (now_us - due_us) >= 0;
}

static bool LineFollower6_ReadRegister(
    uint8_t reg, uint8_t *data, uint16_t length)
{
    bsp_i2c_result_t result;

    result = BSP_I2C_WriteRead(
        LINE_FOLLOWER6_I2C_ADDRESS, &reg, 1U, data, length);
    LineFollower6_BeginSnapshotWrite();
    g_line_follower6_snapshot.last_i2c_result = (uint32_t) result;
    g_line_follower6_snapshot.last_register = reg;
    LineFollower6_EndSnapshotWrite();
    return result == BSP_I2C_RESULT_OK;
}

static void LineFollower6_RecordSampleAttempt(
    uint32_t now_us, bool timestamp_valid)
{
    LineFollower6_BeginSnapshotWrite();
    g_line_follower6_snapshot.sample_count++;
    if (timestamp_valid) {
        g_line_follower6_snapshot.last_attempt_us = now_us;
    }
    LineFollower6_EndSnapshotWrite();
}

static void LineFollower6_RecordFailure(line_follower6_result_t error)
{
    uint8_t index;

    LineFollower6_BeginSnapshotWrite();
    if (g_line_follower6_snapshot.online != 0U) {
        g_line_follower6_snapshot.offline_count++;
    }
    g_line_follower6_snapshot.online = 0U;
    g_line_follower6_snapshot.threshold_valid = 0U;
    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        g_line_follower6_snapshot.margin[index] = 0;
    }
    g_line_follower6_snapshot.failure_count++;
    g_line_follower6_snapshot.consecutive_failure_count++;
    g_line_follower6_snapshot.last_error = (uint8_t) error;
    LineFollower6_EndSnapshotWrite();
    s_service_threshold_channel = 0U;
}

static void LineFollower6_CommitSample(
    const line_follower6_snapshot_t *parsed,
    uint32_t now_us, bool timestamp_valid)
{
    uint8_t index;

    LineFollower6_BeginSnapshotWrite();
    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        g_line_follower6_snapshot.raw[index] = parsed->raw[index];
        g_line_follower6_snapshot.margin[index] = parsed->margin[index];
        g_line_follower6_snapshot.digital_bit[index] =
            parsed->digital_bit[index];
    }
    g_line_follower6_snapshot.minimum_raw = parsed->minimum_raw;
    g_line_follower6_snapshot.maximum_raw = parsed->maximum_raw;
    g_line_follower6_snapshot.digital_mask = parsed->digital_mask;
    if ((g_line_follower6_snapshot.online == 0U) &&
        (g_line_follower6_snapshot.failure_count != 0U)) {
        g_line_follower6_snapshot.reconnect_count++;
    }
    g_line_follower6_snapshot.online = 1U;
    g_line_follower6_snapshot.initialized = 1U;
    g_line_follower6_snapshot.success_count++;
    g_line_follower6_snapshot.consecutive_failure_count = 0U;
    g_line_follower6_snapshot.last_error =
        (uint8_t) LINE_FOLLOWER6_RESULT_OK;
    if (timestamp_valid) {
        g_line_follower6_snapshot.last_success_us = now_us;
    }
    LineFollower6_EndSnapshotWrite();
}

static void LineFollower6_CommitThresholds(
    const line_follower6_snapshot_t *parsed)
{
    uint8_t index;

    LineFollower6_BeginSnapshotWrite();
    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        g_line_follower6_snapshot.threshold[index] =
            parsed->threshold[index];
        g_line_follower6_snapshot.margin[index] = parsed->margin[index];
    }
    g_line_follower6_snapshot.threshold_valid = 1U;
    g_line_follower6_snapshot.threshold_read_count++;
    LineFollower6_EndSnapshotWrite();
}

static void LineFollower6_FillSample(
    const line_follower6_snapshot_t *snapshot,
    line_follower6_sample_t *sample)
{
    uint8_t index;

    sample->scan_sequence = snapshot->success_count;
    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        sample->raw[index] = snapshot->raw[index];
        sample->threshold[index] = snapshot->threshold[index];
        sample->margin[index] = snapshot->margin[index];
        sample->digital_bit[index] = snapshot->digital_bit[index];
    }
    sample->minimum_raw = snapshot->minimum_raw;
    sample->maximum_raw = snapshot->maximum_raw;
    sample->digital_mask = snapshot->digital_mask;
    sample->threshold_valid = snapshot->threshold_valid;
    sample->reserved = 0U;
}

static bool LineFollower6_ServiceThresholdStep(uint32_t now_us)
{
    line_follower6_snapshot_t parsed;
    uint8_t reg = (uint8_t) (LINE_FOLLOWER6_THRESHOLD_FIRST_REG +
        s_service_threshold_channel * LINE_FOLLOWER6_REGISTER_STRIDE);
    uint16_t offset =
        (uint16_t) s_service_threshold_channel * 2U;

    if (!LineFollower6_ReadRegister(
            reg, &s_service_threshold[offset], 2U)) {
        LineFollower6_BeginSnapshotWrite();
        g_line_follower6_snapshot.threshold_failure_count++;
        LineFollower6_EndSnapshotWrite();
        LineFollower6_RecordFailure(LINE_FOLLOWER6_RESULT_I2C_ERROR);
        s_next_frame_start_us =
            now_us + LINE_FOLLOWER6_OFFLINE_RETRY_PERIOD_US;
        s_service_schedule_valid = 1U;
        return false;
    }

    s_service_threshold_channel++;
    if (s_service_threshold_channel < LINE_FOLLOWER6_CHANNEL_COUNT) {
        return false;
    }

    if (!LineFollower6_GetSnapshot(&parsed) ||
        LineFollower6_ParseThresholds(s_service_threshold,
            sizeof(s_service_threshold), &parsed) !=
            LINE_FOLLOWER6_RESULT_OK) {
        LineFollower6_BeginSnapshotWrite();
        g_line_follower6_snapshot.threshold_failure_count++;
        LineFollower6_EndSnapshotWrite();
        LineFollower6_RecordFailure(
            LINE_FOLLOWER6_RESULT_INVALID_ARGUMENT);
        s_service_threshold_channel = 0U;
        s_next_frame_start_us =
            now_us + LINE_FOLLOWER6_OFFLINE_RETRY_PERIOD_US;
        return false;
    }

    LineFollower6_CommitThresholds(&parsed);
    s_service_threshold_channel = 0U;
    return true;
}

static bool LineFollower6_ServiceSampleStep(
    uint32_t now_us, line_follower6_sample_t *sample)
{
    line_follower6_snapshot_t parsed;
    uint8_t reg;
    uint16_t offset;

    if (s_service_stage == 0U) {
        reg = LINE_FOLLOWER6_DIGITAL_REG;
        if (!LineFollower6_ReadRegister(
                reg, &s_service_digital_mask, 1U)) {
            LineFollower6_RecordFailure(
                LINE_FOLLOWER6_RESULT_I2C_ERROR);
            s_service_stage = LINE_FOLLOWER6_SERVICE_STAGE_IDLE;
            s_next_frame_start_us =
                now_us + LINE_FOLLOWER6_OFFLINE_RETRY_PERIOD_US;
            LineFollower6_BeginSnapshotWrite();
            g_line_follower6_snapshot.service_stage = s_service_stage;
            LineFollower6_EndSnapshotWrite();
            return false;
        }
        s_service_stage = 1U;
    } else {
        reg = (uint8_t) (LINE_FOLLOWER6_ANALOG_FIRST_REG +
            (s_service_stage - 1U) * LINE_FOLLOWER6_REGISTER_STRIDE);
        offset = (uint16_t) (s_service_stage - 1U) * 2U;
        if (!LineFollower6_ReadRegister(
                reg, &s_service_analog[offset], 2U)) {
            LineFollower6_RecordFailure(
                LINE_FOLLOWER6_RESULT_I2C_ERROR);
            s_service_stage = LINE_FOLLOWER6_SERVICE_STAGE_IDLE;
            s_next_frame_start_us =
                now_us + LINE_FOLLOWER6_OFFLINE_RETRY_PERIOD_US;
            LineFollower6_BeginSnapshotWrite();
            g_line_follower6_snapshot.service_stage = s_service_stage;
            LineFollower6_EndSnapshotWrite();
            return false;
        }
        if (s_service_stage < LINE_FOLLOWER6_CHANNEL_COUNT) {
            s_service_stage++;
        } else {
            if (!LineFollower6_GetSnapshot(&parsed) ||
                LineFollower6_ParseSample(s_service_digital_mask,
                    s_service_analog, sizeof(s_service_analog), &parsed) !=
                    LINE_FOLLOWER6_RESULT_OK) {
                LineFollower6_RecordFailure(
                    LINE_FOLLOWER6_RESULT_INVALID_ARGUMENT);
                s_service_stage = LINE_FOLLOWER6_SERVICE_STAGE_IDLE;
                s_next_frame_start_us =
                    now_us + LINE_FOLLOWER6_OFFLINE_RETRY_PERIOD_US;
                return false;
            }
            LineFollower6_CommitSample(&parsed, now_us, true);
            s_service_stage = LINE_FOLLOWER6_SERVICE_STAGE_IDLE;
            if (LineFollower6_TimeReached(
                    now_us, s_next_frame_start_us)) {
                s_next_frame_start_us =
                    now_us + LINE_FOLLOWER6_SERVICE_FRAME_PERIOD_US;
            }
            LineFollower6_BeginSnapshotWrite();
            g_line_follower6_snapshot.service_stage = s_service_stage;
            LineFollower6_EndSnapshotWrite();
            if (!LineFollower6_GetSnapshot(&parsed)) {
                return false;
            }
            LineFollower6_FillSample(&parsed, sample);
            return true;
        }
    }

    LineFollower6_BeginSnapshotWrite();
    g_line_follower6_snapshot.service_stage = s_service_stage;
    LineFollower6_EndSnapshotWrite();
    return false;
}

void LineFollower6_Reset(void)
{
    LineFollower6_BeginSnapshotWrite();
    (void) memset((void *) &g_line_follower6_snapshot, 0,
        sizeof(g_line_follower6_snapshot));
    g_line_follower6_snapshot.address = LINE_FOLLOWER6_I2C_ADDRESS;
    g_line_follower6_snapshot.last_i2c_result =
        (uint32_t) BSP_I2C_RESULT_NOT_INITIALIZED;
    g_line_follower6_snapshot.last_error =
        (uint8_t) LINE_FOLLOWER6_RESULT_NOT_INITIALIZED;
    g_line_follower6_snapshot.service_stage =
        LINE_FOLLOWER6_SERVICE_STAGE_IDLE;
    LineFollower6_EndSnapshotWrite();

    s_service_stage = LINE_FOLLOWER6_SERVICE_STAGE_IDLE;
    s_service_digital_mask = 0U;
    (void) memset(s_service_analog, 0, sizeof(s_service_analog));
    (void) memset(s_service_threshold, 0, sizeof(s_service_threshold));
    s_service_threshold_channel = 0U;
    s_service_schedule_valid = 0U;
    s_next_frame_start_us = 0U;
}

bool LineFollower6_Init(void)
{
    line_follower6_snapshot_t snapshot;
    bool sample_ok;
    bool threshold_ok = false;

    if (!LineFollower6_GetSnapshot(&snapshot) ||
        (snapshot.address != LINE_FOLLOWER6_I2C_ADDRESS)) {
        LineFollower6_Reset();
    }
    LineFollower6_BeginSnapshotWrite();
    g_line_follower6_snapshot.init_attempt_count++;
    LineFollower6_EndSnapshotWrite();

    sample_ok = LineFollower6_Update();
    if (sample_ok) {
        threshold_ok = LineFollower6_ReadThresholds();
    }

    LineFollower6_BeginSnapshotWrite();
    if (sample_ok && threshold_ok) {
        g_line_follower6_snapshot.init_success_count++;
    } else {
        g_line_follower6_snapshot.init_failure_count++;
    }
    LineFollower6_EndSnapshotWrite();
    return sample_ok && threshold_ok;
}

bool LineFollower6_ReadThresholds(void)
{
    line_follower6_snapshot_t parsed;
    uint8_t data[LINE_FOLLOWER6_THRESHOLD_BYTES];
    uint8_t index;

    if (!LineFollower6_GetSnapshot(&parsed)) {
        return false;
    }
    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        uint8_t reg = (uint8_t) (LINE_FOLLOWER6_THRESHOLD_FIRST_REG +
            index * LINE_FOLLOWER6_REGISTER_STRIDE);

        if (!LineFollower6_ReadRegister(
                reg, &data[(uint16_t) index * 2U], 2U)) {
            LineFollower6_BeginSnapshotWrite();
            g_line_follower6_snapshot.threshold_failure_count++;
            LineFollower6_EndSnapshotWrite();
            LineFollower6_RecordFailure(
                LINE_FOLLOWER6_RESULT_I2C_ERROR);
            return false;
        }
    }

    if (LineFollower6_ParseThresholds(data, sizeof(data), &parsed) !=
        LINE_FOLLOWER6_RESULT_OK) {
        LineFollower6_BeginSnapshotWrite();
        g_line_follower6_snapshot.threshold_failure_count++;
        LineFollower6_EndSnapshotWrite();
        LineFollower6_RecordFailure(
            LINE_FOLLOWER6_RESULT_INVALID_ARGUMENT);
        return false;
    }

    LineFollower6_CommitThresholds(&parsed);
    return true;
}

bool LineFollower6_Update(void)
{
    line_follower6_snapshot_t parsed;
    uint8_t analog_data[LINE_FOLLOWER6_ANALOG_BYTES];
    uint8_t digital_mask;
    uint8_t index;

    if (!LineFollower6_GetSnapshot(&parsed)) {
        return false;
    }
    LineFollower6_RecordSampleAttempt(0U, false);
    if (!LineFollower6_ReadRegister(
            LINE_FOLLOWER6_DIGITAL_REG, &digital_mask, 1U)) {
        LineFollower6_RecordFailure(LINE_FOLLOWER6_RESULT_I2C_ERROR);
        return false;
    }

    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        uint8_t reg = (uint8_t) (LINE_FOLLOWER6_ANALOG_FIRST_REG +
            index * LINE_FOLLOWER6_REGISTER_STRIDE);

        if (!LineFollower6_ReadRegister(
                reg, &analog_data[(uint16_t) index * 2U], 2U)) {
            LineFollower6_RecordFailure(
                LINE_FOLLOWER6_RESULT_I2C_ERROR);
            return false;
        }
    }

    if (LineFollower6_ParseSample(digital_mask, analog_data,
            sizeof(analog_data), &parsed) != LINE_FOLLOWER6_RESULT_OK) {
        LineFollower6_RecordFailure(
            LINE_FOLLOWER6_RESULT_INVALID_ARGUMENT);
        return false;
    }

    LineFollower6_CommitSample(&parsed, 0U, false);
    return true;
}

bool LineFollower6_ReadAll(void)
{
    return LineFollower6_Update();
}

bool LineFollower6_Service(
    uint32_t now_us, line_follower6_sample_t *sample)
{
    line_follower6_snapshot_t snapshot;

    if (sample == NULL) {
        return false;
    }
    if (!LineFollower6_GetSnapshot(&snapshot) ||
        (snapshot.address != LINE_FOLLOWER6_I2C_ADDRESS)) {
        LineFollower6_Reset();
    }

    LineFollower6_BeginSnapshotWrite();
    g_line_follower6_snapshot.service_call_count++;
    LineFollower6_EndSnapshotWrite();

    if (s_service_stage != LINE_FOLLOWER6_SERVICE_STAGE_IDLE) {
        return LineFollower6_ServiceSampleStep(now_us, sample);
    }

    if (s_service_schedule_valid == 0U) {
        s_next_frame_start_us = now_us;
        s_service_schedule_valid = 1U;
    }
    if (LineFollower6_TimeReached(now_us, s_next_frame_start_us)) {
        s_next_frame_start_us =
            now_us + LINE_FOLLOWER6_SERVICE_FRAME_PERIOD_US;
        s_service_stage = 0U;
        LineFollower6_RecordSampleAttempt(now_us, true);
        LineFollower6_BeginSnapshotWrite();
        g_line_follower6_snapshot.service_stage = s_service_stage;
        LineFollower6_EndSnapshotWrite();
        return LineFollower6_ServiceSampleStep(now_us, sample);
    }

    if (LineFollower6_GetSnapshot(&snapshot) &&
        (snapshot.online != 0U) && (snapshot.threshold_valid == 0U)) {
        (void) LineFollower6_ServiceThresholdStep(now_us);
        return false;
    }

    LineFollower6_BeginSnapshotWrite();
    g_line_follower6_snapshot.service_deferred_count++;
    LineFollower6_EndSnapshotWrite();
    return false;
}

bool LineFollower6_GetSnapshot(line_follower6_snapshot_t *snapshot)
{
    uint32_t before;
    uint32_t after;

    if (snapshot == NULL) {
        return false;
    }

    do {
        before = s_snapshot_write_sequence;
        if ((before & 0x01U) != 0U) {
            continue;
        }
        (void) memcpy(snapshot,
            (const void *) &g_line_follower6_snapshot,
            sizeof(*snapshot));
        after = s_snapshot_write_sequence;
    } while ((before != after) || ((after & 0x01U) != 0U));
    return true;
}

void LineFollower6_MarkOffline(void)
{
    LineFollower6_RecordFailure(LINE_FOLLOWER6_RESULT_I2C_ERROR);
}

line_follower6_result_t LineFollower6_ParseSample(
    uint8_t digital_mask, const uint8_t *analog_data,
    uint16_t analog_length, line_follower6_snapshot_t *snapshot)
{
    uint8_t index;

    if ((analog_data == NULL) || (snapshot == NULL) ||
        (analog_length < LINE_FOLLOWER6_ANALOG_BYTES)) {
        return LINE_FOLLOWER6_RESULT_INVALID_ARGUMENT;
    }

    snapshot->digital_mask = digital_mask;
    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        snapshot->raw[index] = LineFollower6_DecodeU16Le(
            &analog_data[(uint16_t) index * 2U]);
    }
    LineFollower6_UpdateDerived(snapshot);
    return LINE_FOLLOWER6_RESULT_OK;
}

line_follower6_result_t LineFollower6_ParseThresholds(
    const uint8_t *data, uint16_t length,
    line_follower6_snapshot_t *snapshot)
{
    uint8_t index;

    if ((data == NULL) || (snapshot == NULL) ||
        (length < LINE_FOLLOWER6_THRESHOLD_BYTES)) {
        return LINE_FOLLOWER6_RESULT_INVALID_ARGUMENT;
    }

    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        snapshot->threshold[index] = LineFollower6_DecodeU16Le(
            &data[(uint16_t) index * 2U]);
    }
    snapshot->threshold_valid = 1U;
    LineFollower6_UpdateDerived(snapshot);
    return LINE_FOLLOWER6_RESULT_OK;
}

bool LineFollower6_ExpandRawToLegacy8(
    const uint16_t source[LINE_FOLLOWER6_CHANNEL_COUNT],
    line_follower6_channel_order_t order,
    uint16_t destination[LINE_FOLLOWER6_LEGACY_CHANNEL_COUNT])
{
    uint16_t ordered[LINE_FOLLOWER6_CHANNEL_COUNT];
    uint8_t index;

    if ((source == NULL) || (destination == NULL) ||
        ((order != LINE_FOLLOWER6_CHANNEL_ORDER_1_TO_6) &&
         (order != LINE_FOLLOWER6_CHANNEL_ORDER_6_TO_1))) {
        return false;
    }

    for (index = 0U; index < LINE_FOLLOWER6_CHANNEL_COUNT; index++) {
        uint8_t source_index = (order ==
            LINE_FOLLOWER6_CHANNEL_ORDER_1_TO_6) ? index :
            (uint8_t) (LINE_FOLLOWER6_CHANNEL_COUNT - 1U - index);

        ordered[index] = source[source_index];
    }

    for (index = 0U; index < LINE_FOLLOWER6_LEGACY_CHANNEL_COUNT;
         index++) {
        uint8_t scaled = (uint8_t) (index *
            (LINE_FOLLOWER6_CHANNEL_COUNT - 1U));
        uint8_t lower = (uint8_t) (scaled /
            (LINE_FOLLOWER6_LEGACY_CHANNEL_COUNT - 1U));
        uint8_t remainder = (uint8_t) (scaled %
            (LINE_FOLLOWER6_LEGACY_CHANNEL_COUNT - 1U));

        if ((lower >= (LINE_FOLLOWER6_CHANNEL_COUNT - 1U)) ||
            (remainder == 0U)) {
            destination[index] = ordered[lower];
        } else {
            uint32_t weighted =
                (uint32_t) ordered[lower] *
                    (LINE_FOLLOWER6_LEGACY_CHANNEL_COUNT - 1U -
                        remainder) +
                (uint32_t) ordered[lower + 1U] * remainder;

            destination[index] = (uint16_t) ((weighted + 3U) /
                (LINE_FOLLOWER6_LEGACY_CHANNEL_COUNT - 1U));
        }
    }
    return true;
}
