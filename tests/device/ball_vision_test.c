#include <assert.h>
#include <stdint.h>

#include "ball_vision.h"

static void Feed(const uint8_t *data, uint8_t length, uint32_t now_us)
{
    uint8_t index;

    for (index = 0U; index < length; index++) {
        BallVision_ProcessByte(data[index], now_us);
    }
}

static uint16_t Crc16(const uint8_t *data, uint8_t length)
{
    uint16_t crc = 0xFFFFU;
    uint8_t index;

    for (index = 0U; index < length; index++) {
        uint8_t bit;

        crc ^= (uint16_t) data[index] << 8;
        for (bit = 0U; bit < 8U; bit++) {
            crc = (crc & 0x8000U) != 0U ?
                (uint16_t) ((crc << 1) ^ 0x1021U) :
                (uint16_t) (crc << 1);
        }
    }
    return crc;
}

static void SetSequence(uint8_t *packet, uint16_t sequence)
{
    uint16_t crc;

    packet[6] = (uint8_t) sequence;
    packet[7] = (uint8_t) (sequence >> 8);
    crc = Crc16(&packet[2], 18U);
    packet[20] = (uint8_t) crc;
    packet[21] = (uint8_t) (crc >> 8);
}

int main(void)
{
    static const uint8_t fixed_packet[BALL_VISION_PACKET_BYTES] = {
        0xAAU, 0x55U, 0x01U, 0x01U, 0x16U, 0x23U,
        0xE8U, 0x03U, 0x40U, 0xE2U, 0x01U, 0x00U,
        0xEAU, 0x00U, 0x82U, 0xFFU, 0xE6U, 0x00U,
        0x03U, 0x00U, 0x21U, 0x9AU
    };
    static const uint8_t temperature_warning_packet[
        BALL_VISION_PACKET_BYTES] = {
        0xAAU, 0x55U, 0x01U, 0x01U, 0x16U, 0x63U,
        0xE8U, 0x03U, 0x40U, 0xE2U, 0x01U, 0x00U,
        0xEAU, 0x00U, 0x82U, 0xFFU, 0xE6U, 0x00U,
        0x03U, 0x00U, 0x28U, 0xB0U
    };
    uint8_t bad_packet[BALL_VISION_PACKET_BYTES];
    uint8_t restarted_packet[BALL_VISION_PACKET_BYTES];
    ball_vision_snapshot_t snapshot;
    uint8_t index;

    BallVision_Init();
    Feed(fixed_packet, BALL_VISION_PACKET_BYTES, 100000U);
    assert(BallVision_GetSnapshot(101000U, &snapshot));
    assert(snapshot.valid_packet_count == 1U);
    assert(snapshot.packet_sequence == 1000U);
    assert(snapshot.position_decimm == 234);
    assert(snapshot.velocity_mm_s == -126);
    assert(snapshot.confidence == 230U);
    assert(snapshot.control_valid != 0U);

    BallVision_Init();
    Feed(temperature_warning_packet, BALL_VISION_PACKET_BYTES, 110000U);
    assert(BallVision_GetSnapshot(111000U, &snapshot));
    assert((snapshot.flags & BALL_VISION_FLAG_TEMPERATURE_WARNING) != 0U);
    assert(snapshot.control_valid != 0U);

    for (index = 0U; index < BALL_VISION_PACKET_BYTES; index++) {
        bad_packet[index] = fixed_packet[index];
    }
    bad_packet[12] ^= 0x01U;
    Feed(bad_packet, BALL_VISION_PACKET_BYTES, 120000U);
    assert(g_ball_vision_diag.snapshot.crc_error_count == 1U);

    BallVision_Update(260001U);
    assert(BallVision_GetSnapshot(260001U, &snapshot));
    assert(snapshot.online == 0U);
    assert(snapshot.control_valid == 0U);
    assert(snapshot.timeout_count == 1U);

    for (index = 0U; index < BALL_VISION_PACKET_BYTES; index++) {
        restarted_packet[index] = fixed_packet[index];
    }
    SetSequence(restarted_packet, 0U);
    Feed(restarted_packet, BALL_VISION_PACKET_BYTES, 270000U);
    assert(BallVision_GetSnapshot(271000U, &snapshot));
    assert(snapshot.online != 0U);
    assert(snapshot.valid_packet_count == 2U);
    assert(snapshot.packet_sequence == 0U);
    assert(snapshot.out_of_order_packet_count == 0U);
    return 0;
}
