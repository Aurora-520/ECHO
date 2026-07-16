#include "command_service.h"

#include <stdbool.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "bsp_time.h"
#include "chassis_actuator.h"
#include "parameter_service.h"
#include "serial_rx.h"
#include "telemetry.h"
#include "zdt_stepper.h"

#define COMMAND_SYNC_0 0xA5U
#define COMMAND_SYNC_1 0x5AU
#define COMMAND_FRAME_TIMEOUT_US 50000U
#define COMMAND_PARAMETER_PAYLOAD_BYTES 12U
#define COMMAND_ACTUATOR_PAYLOAD_BYTES 20U
#define COMMAND_ZDT_PAYLOAD_BYTES 28U
#define COMMAND_MAX_PAYLOAD_BYTES COMMAND_ZDT_PAYLOAD_BYTES
#define COMMAND_MAX_FRAME_BYTES (16U + COMMAND_MAX_PAYLOAD_BYTES)

#define COMMAND_ZDT_MAGIC         0x5A445442UL
#define COMMAND_ZDT_MAGIC_INVERSE 0xA5BBABBDUL

#define COMMAND_ZDT_AXIS_GEN1 0U
#define COMMAND_ZDT_AXIS_GEN2 1U
#define COMMAND_ZDT_AXIS_BOTH 2U

#define COMMAND_ZDT_OP_STATUS   0U
#define COMMAND_ZDT_OP_SELECT   1U
#define COMMAND_ZDT_OP_DESELECT 2U
#define COMMAND_ZDT_OP_ENABLE   3U
#define COMMAND_ZDT_OP_SPEED    4U
#define COMMAND_ZDT_OP_POSITION 5U
#define COMMAND_ZDT_OP_STOP     6U

#define COMMAND_ZDT_STATUS_ACCEPTED       0U
#define COMMAND_ZDT_STATUS_BAD_MAGIC      1U
#define COMMAND_ZDT_STATUS_BAD_OPERATION  2U
#define COMMAND_ZDT_STATUS_BAD_AXIS       3U
#define COMMAND_ZDT_STATUS_SELECT_FAILED  4U
#define COMMAND_ZDT_STATUS_DISABLED       5U
#define COMMAND_ZDT_STATUS_BUSY           6U
#define COMMAND_ZDT_STATUS_DUPLICATE      7U
#define COMMAND_ZDT_STATUS_INVALID        8U
#define COMMAND_ZDT_STATUS_PARTIAL        9U

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

static int32_t Command_GetI32(const uint8_t *data)
{
    return (int32_t) Command_GetU32(data);
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
    if (frame_type == TELEMETRY_FRAME_TYPE_ZDT_COMMAND) {
        return COMMAND_ZDT_PAYLOAD_BYTES;
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

static uint8_t Command_MapZdtStatus(
    zdt_stepper_request_status_t status)
{
    switch (status) {
        case ZDT_STEPPER_REQUEST_ACCEPTED:
            return COMMAND_ZDT_STATUS_ACCEPTED;
        case ZDT_STEPPER_REQUEST_DISABLED:
            return COMMAND_ZDT_STATUS_DISABLED;
        case ZDT_STEPPER_REQUEST_BUSY:
            return COMMAND_ZDT_STATUS_BUSY;
        case ZDT_STEPPER_REQUEST_DUPLICATE:
            return COMMAND_ZDT_STATUS_DUPLICATE;
        default:
            return COMMAND_ZDT_STATUS_INVALID;
    }
}

static zdt_stepper_request_status_t Command_ApplyZdtAxis(
    uint8_t operation, zdt_stepper_axis_t axis, int32_t value,
    uint8_t position_mode, uint16_t speed_rpm,
    uint32_t acceleration_rpm_s)
{
    switch (operation) {
        case COMMAND_ZDT_OP_ENABLE:
            return ZdtStepper_RequestEnable(axis, value != 0);
        case COMMAND_ZDT_OP_SPEED:
            if ((value < INT16_MIN) || (value > INT16_MAX)) {
                return ZDT_STEPPER_REQUEST_INVALID;
            }
            return ZdtStepper_RequestSpeed(
                axis, (int16_t) value, acceleration_rpm_s);
        case COMMAND_ZDT_OP_POSITION:
            return ZdtStepper_RequestPosition(axis, value, speed_rpm,
                acceleration_rpm_s,
                (zdt_position_mode_t) position_mode);
        case COMMAND_ZDT_OP_STOP:
            return ZdtStepper_RequestStop(axis);
        default:
            return ZDT_STEPPER_REQUEST_INVALID;
    }
}

static uint8_t Command_ZdtFlags(void)
{
    uint8_t flags = 0U;

    if (g_zdt_stepper_diag.backend_selected != 0U) {
        flags |= 1U << 0;
    }
    if (g_zdt_stepper_diag.shutdown_pending != 0U) {
        flags |= 1U << 1;
    }
    if (g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN1].online != 0U) {
        flags |= 1U << 2;
    }
    if (g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN2].online != 0U) {
        flags |= 1U << 3;
    }
    if (g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN1].enabled != 0U) {
        flags |= 1U << 4;
    }
    if (g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN2].enabled != 0U) {
        flags |= 1U << 5;
    }
    if (g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN1].motion_active != 0U) {
        flags |= 1U << 6;
    }
    if (g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN2].motion_active != 0U) {
        flags |= 1U << 7;
    }
    return flags;
}

static void Command_FillZdtAxisSnapshot(
    telemetry_zdt_axis_snapshot_t *snapshot, zdt_stepper_axis_t axis)
{
    volatile zdt_stepper_axis_diagnostics_t *diagnostics =
        &g_zdt_stepper_diag.axis[axis];

    snapshot->tx_command_count = diagnostics->tx_command_count;
    snapshot->tx_query_count = diagnostics->tx_query_count;
    snapshot->invalid_response_count = diagnostics->invalid_response_count;
    snapshot->position_reached_count = diagnostics->position_reached_count;
    snapshot->speed_lease_expired_count =
        diagnostics->speed_lease_expired_count;
    snapshot->position_counts = diagnostics->position_counts;
    snapshot->position_millidegrees = diagnostics->position_millidegrees;
    snapshot->speed_rpm = diagnostics->speed_rpm;
    snapshot->firmware_version = diagnostics->firmware_version;
    snapshot->hardware_version = diagnostics->hardware_version;
    snapshot->motor_status_flags = diagnostics->motor_status_flags;
    snapshot->last_function = diagnostics->last_function;
    snapshot->last_reply_status = diagnostics->last_reply_status;
    snapshot->stalled = diagnostics->stalled;
    snapshot->stall_protected = diagnostics->stall_protected;
}

static void Command_HandleZdtFrame(const uint8_t *payload)
{
    telemetry_zdt_ack_t ack;
    uint32_t magic = Command_GetU32(&payload[0]);
    uint32_t magic_inverse = Command_GetU32(&payload[4]);
    uint8_t operation = payload[12];
    uint8_t axis = payload[13];
    uint8_t position_mode = payload[14];
    int32_t value = Command_GetI32(&payload[16]);
    uint16_t speed_rpm = Command_GetU16(&payload[20]);
    uint32_t acceleration_rpm_s = Command_GetU32(&payload[24]);
    uint8_t status = COMMAND_ZDT_STATUS_ACCEPTED;

    memset(&ack, 0, sizeof(ack));
    ack.sequence = Command_GetU32(&payload[8]);
    ack.value = value;
    ack.operation = operation;
    ack.axis = axis;

    if ((magic != COMMAND_ZDT_MAGIC) ||
        (magic_inverse != COMMAND_ZDT_MAGIC_INVERSE) ||
        (ack.sequence == 0U)) {
        status = COMMAND_ZDT_STATUS_BAD_MAGIC;
    } else if (operation == COMMAND_ZDT_OP_SELECT) {
        if (!ZdtStepper_SelectBackupBackend()) {
            status = COMMAND_ZDT_STATUS_SELECT_FAILED;
        }
    } else if (operation == COMMAND_ZDT_OP_DESELECT) {
        ZdtStepper_DeselectBackupBackend();
    } else if (operation == COMMAND_ZDT_OP_STATUS) {
        status = COMMAND_ZDT_STATUS_ACCEPTED;
    } else if ((operation < COMMAND_ZDT_OP_ENABLE) ||
               (operation > COMMAND_ZDT_OP_STOP)) {
        status = COMMAND_ZDT_STATUS_BAD_OPERATION;
    } else if (axis == COMMAND_ZDT_AXIS_GEN1) {
        status = Command_MapZdtStatus(Command_ApplyZdtAxis(operation,
            ZDT_STEPPER_AXIS_GEN1, value, position_mode,
            speed_rpm, acceleration_rpm_s));
    } else if (axis == COMMAND_ZDT_AXIS_GEN2) {
        status = Command_MapZdtStatus(Command_ApplyZdtAxis(operation,
            ZDT_STEPPER_AXIS_GEN2, value, position_mode,
            speed_rpm, acceleration_rpm_s));
    } else if (axis == COMMAND_ZDT_AXIS_BOTH) {
        zdt_stepper_request_status_t first = Command_ApplyZdtAxis(
            operation, ZDT_STEPPER_AXIS_GEN1, value, position_mode,
            speed_rpm, acceleration_rpm_s);

        if (first != ZDT_STEPPER_REQUEST_ACCEPTED) {
            status = Command_MapZdtStatus(first);
        } else {
            zdt_stepper_request_status_t second = Command_ApplyZdtAxis(
                operation, ZDT_STEPPER_AXIS_GEN2, value, position_mode,
                speed_rpm, acceleration_rpm_s);
            if (second != ZDT_STEPPER_REQUEST_ACCEPTED) {
                ZdtStepper_DeselectBackupBackend();
                status = COMMAND_ZDT_STATUS_PARTIAL;
            }
        }
    } else {
        status = COMMAND_ZDT_STATUS_BAD_AXIS;
    }

    ack.status = status;
    ack.flags = Command_ZdtFlags();
    ack.gen1_response_count =
        g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN1].response_count;
    ack.gen2_response_count =
        g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN2].response_count;
    ack.gen1_timeout_count = (uint16_t)
        g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN1].response_timeout_count;
    ack.gen2_timeout_count = (uint16_t)
        g_zdt_stepper_diag.axis[ZDT_STEPPER_AXIS_GEN2].response_timeout_count;
    Command_FillZdtAxisSnapshot(
        &ack.axis_snapshot[0], ZDT_STEPPER_AXIS_GEN1);
    Command_FillZdtAxisSnapshot(
        &ack.axis_snapshot[1], ZDT_STEPPER_AXIS_GEN2);
    if (!Telemetry_PublishZdtAck(&ack)) {
        g_command_service_diag.zdt_ack_drop_count++;
    }
    g_command_service_diag.zdt_frame_count++;
}

static void Command_HandleValidFrame(void)
{
    const uint8_t *payload = &s_frame[14];

    g_command_service_diag.received_frame_count++;
    g_command_service_diag.last_frame_type = s_frame[3];
    if (s_frame[3] == TELEMETRY_FRAME_TYPE_PARAMETER_SET) {
        Command_HandleParameterFrame(payload);
    } else if (s_frame[3] == TELEMETRY_FRAME_TYPE_ACTUATOR_COMMAND) {
        Command_HandleActuatorFrame(payload);
    } else {
        Command_HandleZdtFrame(payload);
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
