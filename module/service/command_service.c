#include "command_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "bsp_time.h"
#include "chassis_actuator.h"
#include "parameter_service.h"
#include "serial_rx.h"
#include "telemetry.h"

#define COMMAND_SYNC_0 0xA5U
#define COMMAND_SYNC_1 0x5AU
#define COMMAND_FRAME_TIMEOUT_US 50000U
#define COMMAND_PARAMETER_PAYLOAD_BYTES 12U
#define COMMAND_ACTUATOR_PAYLOAD_BYTES 20U
#define COMMAND_MAX_PAYLOAD_BYTES COMMAND_ACTUATOR_PAYLOAD_BYTES
#define COMMAND_MAX_FRAME_BYTES (16U + COMMAND_MAX_PAYLOAD_BYTES)

volatile command_service_diagnostics_t g_command_service_diag;

static uint8_t s_frame[COMMAND_MAX_FRAME_BYTES];
static uint16_t s_frame_length;
static uint16_t s_expected_length;
static uint32_t s_last_byte_us;
static uint32_t s_seen_rx_overflow_count;

static uint16_t Command_GetU16(const uint8_t *data)
{
    return (uint16_t) data[0] |
        (uint16_t) ((uint16_t) data[1] << 8);
}

static int16_t Command_GetI16(const uint8_t *data)
{
    return (int16_t) Command_GetU16(data);
}

static uint32_t Command_GetU32(const uint8_t *data)
{
    return (uint32_t) data[0] |
        ((uint32_t) data[1] << 8) |
        ((uint32_t) data[2] << 16) |
        ((uint32_t) data[3] << 24);
}

static float Command_GetFloat(const uint8_t *data)
{
    float value;

    memcpy(&value, data, sizeof(value));
    return value;
}

static uint16_t Command_Crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;

    for (index = 0U; index < length; index++) {
        uint8_t bit;

        crc ^= (uint16_t) data[index] << 8;
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t) ((crc << 1) ^ 0x1021U);
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

static uint16_t Command_ExpectedPayloadLength(uint8_t frame_type)
{
    if (frame_type == TELEMETRY_FRAME_TYPE_PARAMETER_SET) {
        return COMMAND_PARAMETER_PAYLOAD_BYTES;
    }
    if (frame_type == TELEMETRY_FRAME_TYPE_ACTUATOR_COMMAND) {
        return COMMAND_ACTUATOR_PAYLOAD_BYTES;
    }
    return 0U;
}

static bool Command_HeaderIsValidAt(const uint8_t *frame)
{
    uint16_t expected_payload;

    if (frame[2] != TELEMETRY_PROTOCOL_VERSION) {
        return false;
    }
    expected_payload = Command_ExpectedPayloadLength(frame[3]);
    return (expected_payload != 0U) &&
        (Command_GetU16(&frame[4]) == expected_payload);
}

static void Command_ResetParser(void)
{
    s_frame_length = 0U;
    s_expected_length = 0U;
}

static void Command_ResyncParser(void)
{
    uint16_t index;

    for (index = 1U; (index + 1U) < s_frame_length; index++) {
        uint16_t remaining;

        if ((s_frame[index] != COMMAND_SYNC_0) ||
            (s_frame[index + 1U] != COMMAND_SYNC_1)) {
            continue;
        }
        remaining = (uint16_t) (s_frame_length - index);
        if ((remaining >= 6U) &&
            !Command_HeaderIsValidAt(&s_frame[index])) {
            continue;
        }
        memmove(s_frame, &s_frame[index], remaining);
        s_frame_length = remaining;
        if (remaining >= 6U) {
            s_expected_length = (uint16_t) (16U +
                Command_GetU16(&s_frame[4]));
        } else {
            s_expected_length = 0U;
        }
        g_command_service_diag.resync_count++;
        return;
    }

    if ((s_frame_length != 0U) &&
        (s_frame[s_frame_length - 1U] == COMMAND_SYNC_0)) {
        s_frame[0] = COMMAND_SYNC_0;
        s_frame_length = 1U;
        s_expected_length = 0U;
    } else {
        Command_ResetParser();
    }
    g_command_service_diag.resync_count++;
}

static void Command_HandleParameterFrame(const uint8_t *payload)
{
    ParameterService_HandleUartSet(
        Command_GetU32(&payload[0]),
        Command_GetU16(&payload[4]),
        payload[6], payload[7], Command_GetFloat(&payload[8]));
    g_command_service_diag.parameter_frame_count++;
}

static void Command_HandleActuatorFrame(const uint8_t *payload)
{
    chassis_actuator_debug_request_t request;
    telemetry_actuator_ack_t ack;

    request.magic = Command_GetU32(&payload[0]);
    request.magic_inverse = Command_GetU32(&payload[4]);
    request.sequence = Command_GetU32(&payload[8]);
    request.left_electrical_permille = Command_GetI16(&payload[12]);
    request.right_electrical_permille = Command_GetI16(&payload[14]);
    request.duration_ms = Command_GetU16(&payload[16]);
    request.reserved = Command_GetU16(&payload[18]);

    ack.sequence = request.sequence;
    ack.left_electrical_permille = request.left_electrical_permille;
    ack.right_electrical_permille = request.right_electrical_permille;
    ack.duration_ms = request.duration_ms;
    ack.status = (uint8_t)
        ChassisActuator_StageDebugRequest(&request);
    ack.reserved = (uint8_t) request.reserved;
    ack.accepted_request_count =
        g_chassis_actuator_diag.accepted_request_count;
    if (!Telemetry_PublishActuatorAck(&ack)) {
        g_command_service_diag.actuator_ack_drop_count++;
    }
    g_command_service_diag.actuator_frame_count++;
}

static void Command_HandleValidFrame(void)
{
    const uint8_t *payload = &s_frame[14];

    g_command_service_diag.received_frame_count++;
    g_command_service_diag.last_frame_type = s_frame[3];
    if (s_frame[3] == TELEMETRY_FRAME_TYPE_PARAMETER_SET) {
        Command_HandleParameterFrame(payload);
    } else {
        Command_HandleActuatorFrame(payload);
    }
}

static void Command_HandleRxOverflow(void)
{
    uint32_t overflow_count = SerialRx_GetOverflowCount();

    if (overflow_count == s_seen_rx_overflow_count) {
        return;
    }
    SerialRx_Flush();
    Command_ResetParser();
    s_seen_rx_overflow_count = overflow_count;
    g_command_service_diag.overflow_reset_count++;
}

static void Command_ConsumeByte(uint8_t byte)
{
    if (s_frame_length == 0U) {
        if (byte == COMMAND_SYNC_0) {
            s_frame[0] = byte;
            s_frame_length = 1U;
        }
        return;
    }

    if (s_frame_length == 1U) {
        if (byte == COMMAND_SYNC_1) {
            s_frame[s_frame_length++] = byte;
        } else {
            Command_ResetParser();
            if (byte == COMMAND_SYNC_0) {
                s_frame[0] = byte;
                s_frame_length = 1U;
            }
        }
        return;
    }

    if (s_frame_length >= COMMAND_MAX_FRAME_BYTES) {
        Command_ResetParser();
        return;
    }

    s_frame[s_frame_length++] = byte;
    if (s_frame_length == 6U) {
        uint16_t payload_length = Command_GetU16(&s_frame[4]);
        uint16_t expected_payload =
            Command_ExpectedPayloadLength(s_frame[3]);

        if (s_frame[2] != TELEMETRY_PROTOCOL_VERSION ||
            expected_payload == 0U) {
            g_command_service_diag.bad_type_count++;
            Command_ResyncParser();
            return;
        }
        if (payload_length != expected_payload) {
            g_command_service_diag.bad_length_count++;
            Command_ResyncParser();
            return;
        }
        s_expected_length = (uint16_t) (16U + payload_length);
    }

    if ((s_expected_length != 0U) &&
        (s_frame_length == s_expected_length)) {
        uint16_t payload_length = Command_GetU16(&s_frame[4]);
        uint16_t received_crc = Command_GetU16(
            &s_frame[14U + payload_length]);
        uint16_t calculated_crc = Command_Crc16(
            &s_frame[2], (uint16_t) (12U + payload_length));

        if (SerialRx_GetOverflowCount() != s_seen_rx_overflow_count) {
            Command_HandleRxOverflow();
            return;
        }
        if (received_crc != calculated_crc) {
            g_command_service_diag.crc_error_count++;
            Command_ResyncParser();
            return;
        }
        Command_HandleValidFrame();
        Command_ResetParser();
    }
}

void CommandService_Init(void)
{
    memset((void *) &g_command_service_diag, 0,
        sizeof(g_command_service_diag));
    Command_ResetParser();
    s_last_byte_us = BSP_Time_GetUs();
    s_seen_rx_overflow_count = SerialRx_GetOverflowCount();
    g_command_service_diag.initialized = 1U;
}

void CommandService_ProcessRx(void)
{
    uint8_t byte;
    uint16_t processed = 0U;
    uint32_t now_us = BSP_Time_GetUs();

    Command_HandleRxOverflow();
    if ((s_frame_length != 0U) &&
        ((uint32_t) (now_us - s_last_byte_us) >=
            COMMAND_FRAME_TIMEOUT_US)) {
        Command_ResetParser();
        g_command_service_diag.frame_timeout_count++;
    }
    while ((processed < 128U) && SerialRx_TryRead(&byte)) {
        if (SerialRx_GetOverflowCount() != s_seen_rx_overflow_count) {
            Command_HandleRxOverflow();
            break;
        }
        Command_ConsumeByte(byte);
        g_command_service_diag.processed_byte_count++;
        processed++;
        now_us = BSP_Time_GetUs();
        if (s_frame_length != 0U) {
            s_last_byte_us = now_us;
        }
    }
    Command_HandleRxOverflow();
}
