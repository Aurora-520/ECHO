#include "ball_vision.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#if defined(__ARMCC_VERSION) || defined(__ARM_ARCH)
#include "cmsis_compiler.h"
#define BALL_VISION_MEMORY_BARRIER() __DMB()
#else
#define BALL_VISION_MEMORY_BARRIER() do { } while (0)
#endif

#define BALL_VISION_HEADER_0             0xAAU
#define BALL_VISION_HEADER_1             0x55U
#define BALL_VISION_VERSION              0x01U
#define BALL_VISION_TYPE_STATE           0x01U
#define BALL_VISION_CRC_INIT             0xFFFFU
#define BALL_VISION_CRC_POLY             0x1021U
#define BALL_VISION_TIMEOUT_US            150000U
#define BALL_VISION_MAX_MEASUREMENT_AGE_MS    80U
#define BALL_VISION_MAX_ABS_POSITION_DECIMM  1350
#define BALL_VISION_MAX_ABS_VELOCITY_MM_S    2500

static uint8_t s_frame[BALL_VISION_PACKET_BYTES];
static uint8_t s_frame_index;
static uint8_t s_sequence_valid;
static uint16_t s_last_packet_sequence;
static volatile uint32_t s_snapshot_guard;

volatile ball_vision_diagnostics_t g_ball_vision_diag;

static uint16_t BallVision_GetU16(const uint8_t *data)
{
    return (uint16_t) data[0] | ((uint16_t) data[1] << 8);
}

static uint32_t BallVision_GetU32(const uint8_t *data)
{
    return (uint32_t) data[0] |
        ((uint32_t) data[1] << 8) |
        ((uint32_t) data[2] << 16) |
        ((uint32_t) data[3] << 24);
}

static int32_t BallVision_AbsI32(int32_t value)
{
    return value < 0 ? -value : value;
}

static uint16_t BallVision_Crc16(const uint8_t *data, uint8_t length)
{
    uint16_t crc = BALL_VISION_CRC_INIT;
    uint8_t index;

    for (index = 0U; index < length; index++) {
        uint8_t bit;

        crc ^= (uint16_t) data[index] << 8;
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t) ((crc << 1) ^ BALL_VISION_CRC_POLY);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static bool BallVision_ControlFieldsValid(uint8_t flags,
    int16_t position_decimm, int16_t velocity_mm_s,
    uint16_t measurement_age_ms)
{
    const uint8_t required = BALL_VISION_FLAG_DETECTED |
        BALL_VISION_FLAG_VELOCITY_VALID |
        BALL_VISION_FLAG_CALIBRATION_VALID;
    const uint8_t forbidden = BALL_VISION_FLAG_PREDICTED |
        BALL_VISION_FLAG_LOW_CONFIDENCE |
        BALL_VISION_FLAG_REFERENCE_MISMATCH;

    return (flags & required) == required &&
        (flags & forbidden) == 0U &&
        measurement_age_ms <= BALL_VISION_MAX_MEASUREMENT_AGE_MS &&
        BallVision_AbsI32(position_decimm) <=
            BALL_VISION_MAX_ABS_POSITION_DECIMM &&
        BallVision_AbsI32(velocity_mm_s) <=
            BALL_VISION_MAX_ABS_VELOCITY_MM_S;
}

static void BallVision_BeginSnapshotWrite(void)
{
    s_snapshot_guard++;
    BALL_VISION_MEMORY_BARRIER();
}

static void BallVision_EndSnapshotWrite(void)
{
    BALL_VISION_MEMORY_BARRIER();
    s_snapshot_guard++;
}

static bool BallVision_RecordSequence(uint16_t sequence)
{
    if (s_sequence_valid != 0U) {
        uint16_t delta = (uint16_t) (sequence - s_last_packet_sequence);

        if (delta == 0U) {
            g_ball_vision_diag.snapshot.duplicate_packet_count++;
            return false;
        } else if (delta < 0x8000U) {
            if (delta > 1U) {
                g_ball_vision_diag.snapshot.dropped_packet_count +=
                    (uint32_t) delta - 1U;
            }
        } else {
            g_ball_vision_diag.snapshot.out_of_order_packet_count++;
            return false;
        }
    }
    s_last_packet_sequence = sequence;
    s_sequence_valid = 1U;
    return true;
}

static bool BallVision_FrameValid(void)
{
    uint16_t expected_crc;
    uint16_t actual_crc;

    if (s_frame[0] != BALL_VISION_HEADER_0 ||
        s_frame[1] != BALL_VISION_HEADER_1 ||
        s_frame[2] != BALL_VISION_VERSION ||
        s_frame[3] != BALL_VISION_TYPE_STATE ||
        s_frame[4] != BALL_VISION_PACKET_BYTES ||
        s_frame[17] != 0U) {
        g_ball_vision_diag.snapshot.format_error_count++;
        return false;
    }
    expected_crc = BallVision_GetU16(&s_frame[20]);
    actual_crc = BallVision_Crc16(&s_frame[2], 18U);
    if (actual_crc != expected_crc) {
        g_ball_vision_diag.snapshot.crc_error_count++;
        return false;
    }
    return true;
}

static void BallVision_AcceptFrame(uint32_t now_us)
{
    uint16_t sequence = BallVision_GetU16(&s_frame[6]);
    int16_t position_decimm = (int16_t) BallVision_GetU16(&s_frame[12]);
    int16_t velocity_mm_s = (int16_t) BallVision_GetU16(&s_frame[14]);
    uint16_t measurement_age_ms = BallVision_GetU16(&s_frame[18]);
    uint8_t flags = s_frame[5];

    /* A restarted camera begins its packet sequence at zero. Once the
     * previous stream has timed out, treat the first valid frame as a new
     * sequence baseline instead of rejecting it until the counter catches up. */
    if (g_ball_vision_diag.snapshot.online == 0U) {
        s_sequence_valid = 0U;
    }

    BallVision_BeginSnapshotWrite();
    if (!BallVision_RecordSequence(sequence)) {
        BallVision_EndSnapshotWrite();
        return;
    }
    g_ball_vision_diag.snapshot.update_sequence++;
    g_ball_vision_diag.snapshot.capture_time_ms =
        BallVision_GetU32(&s_frame[8]);
    g_ball_vision_diag.snapshot.received_at_us = now_us;
    g_ball_vision_diag.snapshot.receive_age_us = 0U;
    g_ball_vision_diag.snapshot.valid_packet_count++;
    g_ball_vision_diag.snapshot.position_decimm = position_decimm;
    g_ball_vision_diag.snapshot.velocity_mm_s = velocity_mm_s;
    g_ball_vision_diag.snapshot.packet_sequence = sequence;
    g_ball_vision_diag.snapshot.measurement_age_ms = measurement_age_ms;
    g_ball_vision_diag.snapshot.flags = flags;
    g_ball_vision_diag.snapshot.confidence = s_frame[16];
    g_ball_vision_diag.snapshot.online = 1U;
    g_ball_vision_diag.snapshot.control_valid =
        BallVision_ControlFieldsValid(flags, position_decimm,
            velocity_mm_s, measurement_age_ms) ? 1U : 0U;
    BallVision_EndSnapshotWrite();
}

static void BallVision_Resynchronize(void)
{
    uint8_t index;

    g_ball_vision_diag.parser_resync_count++;
    for (index = 1U; index + 1U < BALL_VISION_PACKET_BYTES; index++) {
        if (s_frame[index] == BALL_VISION_HEADER_0 &&
            s_frame[index + 1U] == BALL_VISION_HEADER_1) {
            uint8_t remaining = (uint8_t) (BALL_VISION_PACKET_BYTES - index);

            memmove(s_frame, &s_frame[index], remaining);
            s_frame_index = remaining;
            g_ball_vision_diag.parser_index = s_frame_index;
            return;
        }
    }
    if (s_frame[BALL_VISION_PACKET_BYTES - 1U] ==
        BALL_VISION_HEADER_0) {
        s_frame[0] = BALL_VISION_HEADER_0;
        s_frame_index = 1U;
    } else {
        s_frame_index = 0U;
    }
    g_ball_vision_diag.parser_index = s_frame_index;
}

void BallVision_Init(void)
{
    memset(s_frame, 0, sizeof(s_frame));
    memset((void *) &g_ball_vision_diag, 0,
        sizeof(g_ball_vision_diag));
    s_frame_index = 0U;
    s_sequence_valid = 0U;
    s_last_packet_sequence = 0U;
    s_snapshot_guard = 0U;
    g_ball_vision_diag.initialized = 1U;
}

void BallVision_ProcessByte(uint8_t byte, uint32_t now_us)
{
    g_ball_vision_diag.rx_byte_count++;
    if (s_frame_index == 0U) {
        if (byte == BALL_VISION_HEADER_0) {
            s_frame[0] = byte;
            s_frame_index = 1U;
        }
    } else if (s_frame_index == 1U) {
        if (byte == BALL_VISION_HEADER_1) {
            s_frame[1] = byte;
            s_frame_index = 2U;
        } else if (byte != BALL_VISION_HEADER_0) {
            s_frame_index = 0U;
        }
    } else {
        s_frame[s_frame_index++] = byte;
        if (s_frame_index >= BALL_VISION_PACKET_BYTES) {
            if (BallVision_FrameValid()) {
                BallVision_AcceptFrame(now_us);
                s_frame_index = 0U;
            } else {
                BallVision_Resynchronize();
            }
        }
    }
    g_ball_vision_diag.parser_index = s_frame_index;
}

void BallVision_Update(uint32_t now_us)
{
    if (g_ball_vision_diag.snapshot.online != 0U &&
        (uint32_t) (now_us -
            g_ball_vision_diag.snapshot.received_at_us) >
            BALL_VISION_TIMEOUT_US) {
        BallVision_BeginSnapshotWrite();
        g_ball_vision_diag.snapshot.online = 0U;
        g_ball_vision_diag.snapshot.control_valid = 0U;
        g_ball_vision_diag.snapshot.timeout_count++;
        BallVision_EndSnapshotWrite();
    }
}

bool BallVision_GetSnapshot(uint32_t now_us,
    ball_vision_snapshot_t *snapshot)
{
    uint8_t attempts;

    if (snapshot == NULL || g_ball_vision_diag.initialized == 0U) {
        return false;
    }
    for (attempts = 0U; attempts < 3U; attempts++) {
        uint32_t before = s_snapshot_guard;
        uint32_t after;

        if ((before & 1U) != 0U) {
            continue;
        }
        BALL_VISION_MEMORY_BARRIER();
        *snapshot = g_ball_vision_diag.snapshot;
        BALL_VISION_MEMORY_BARRIER();
        after = s_snapshot_guard;
        if (before == after && (after & 1U) == 0U) {
            snapshot->receive_age_us = snapshot->valid_packet_count == 0U ?
                UINT32_MAX : (uint32_t) (now_us - snapshot->received_at_us);
            if (snapshot->receive_age_us > BALL_VISION_TIMEOUT_US) {
                snapshot->online = 0U;
                snapshot->control_valid = 0U;
            }
            return true;
        }
    }
    return false;
}
