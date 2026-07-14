#include "parameter_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "bsp_time.h"
#include "serial_rx.h"
#include "telemetry.h"
#include "task.h"

#define PARAMETER_FRAME_TYPE_SET 2U
#define PARAMETER_SET_PAYLOAD_BYTES 12U
#define PARAMETER_MAX_PAYLOAD_BYTES 32U
#define PARAMETER_MAX_FRAME_BYTES (16U + PARAMETER_MAX_PAYLOAD_BYTES)
#define PARAMETER_SYNC_0 0xA5U
#define PARAMETER_SYNC_1 0x5AU
#define PARAMETER_FRAME_TIMEOUT_US 50000U

typedef struct {
    uint32_t transaction_id;
    parameter_id_t parameter_id;
    float value;
} pending_parameter_t;

volatile control_tuning_parameters_t g_control_tuning_params = {
    1.0f, 0.0f, 0.0f, 0.0f, 0U
};
volatile parameter_service_diagnostics_t g_parameter_service_diag;

static uint8_t s_frame[PARAMETER_MAX_FRAME_BYTES];
static uint16_t s_frame_length;
static uint16_t s_expected_length;
static bool s_last_transaction_valid;
static telemetry_parameter_ack_t s_last_ack;
static bool s_pending_valid;
static pending_parameter_t s_pending;
static uint32_t s_last_byte_us;
static uint32_t s_seen_rx_overflow_count;

static uint16_t Parameter_GetU16(const uint8_t *data)
{
    return (uint16_t) data[0] | (uint16_t) ((uint16_t) data[1] << 8);
}

static uint32_t Parameter_GetU32(const uint8_t *data)
{
    return (uint32_t) data[0] |
        ((uint32_t) data[1] << 8) |
        ((uint32_t) data[2] << 16) |
        ((uint32_t) data[3] << 24);
}

static float Parameter_GetFloat(const uint8_t *data)
{
    float value;
    memcpy(&value, data, sizeof(value));
    return value;
}

static uint16_t Parameter_Crc16(
    const uint8_t *data, uint16_t length)
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

static bool Parameter_IsFinite(float value)
{
    uint32_t bits;
    uint32_t exponent;

    memcpy(&bits, &value, sizeof(bits));
    exponent = (bits >> 23) & 0xFFU;
    return exponent != 0xFFU;
}

static bool Parameter_IsAllowed(parameter_id_t id, float value)
{
    if (!Parameter_IsFinite(value)) {
        return false;
    }

    switch (id) {
        case PARAMETER_ID_KP:
        case PARAMETER_ID_KI:
        case PARAMETER_ID_KD:
            return (value >= 0.0f) && (value <= 1000.0f);

        case PARAMETER_ID_TARGET:
            return (value >= -10000.0f) && (value <= 10000.0f);

        default:
            return false;
    }
}

static void Parameter_SendAck(const telemetry_parameter_ack_t *ack)
{
    if (!Telemetry_PublishParameterAck(ack)) {
        g_parameter_service_diag.ack_publish_drop_count++;
    }
}

static void Parameter_RecordAck(
    const telemetry_parameter_ack_t *ack, bool cache)
{
    if (cache) {
        taskENTER_CRITICAL();
        s_last_ack = *ack;
        s_last_transaction_valid = true;
        taskEXIT_CRITICAL();
    }

    g_parameter_service_diag.last_transaction_id = ack->transaction_id;
    g_parameter_service_diag.last_parameter_id = ack->parameter_id;
    g_parameter_service_diag.last_status = ack->status;
    Parameter_SendAck(ack);
}

static void Parameter_HandleValidFrame(void)
{
    const uint8_t *payload = &s_frame[14];
    telemetry_parameter_ack_t ack;
    telemetry_parameter_ack_t cached_ack;
    uint16_t payload_length = Parameter_GetU16(&s_frame[4]);
    uint16_t received_crc = Parameter_GetU16(
        &s_frame[14U + payload_length]);
    uint16_t calculated_crc = Parameter_Crc16(&s_frame[2],
        (uint16_t) (12U + payload_length));
    uint32_t transaction_id;
    uint16_t raw_parameter_id;
    parameter_id_t parameter_id;
    uint8_t value_type;
    uint8_t flags;
    float value;
    bool request_valid = false;
    bool cached_duplicate = false;
    bool pending_duplicate = false;
    bool busy = false;
    bool cached_invalid = false;
    bool staged = false;

    if ((s_frame[2] != TELEMETRY_PROTOCOL_VERSION) ||
        (s_frame[3] != PARAMETER_FRAME_TYPE_SET)) {
        g_parameter_service_diag.bad_type_count++;
        return;
    }
    if (payload_length != PARAMETER_SET_PAYLOAD_BYTES) {
        g_parameter_service_diag.bad_length_count++;
        return;
    }
    if (received_crc != calculated_crc) {
        g_parameter_service_diag.crc_error_count++;
        return;
    }

    transaction_id = Parameter_GetU32(&payload[0]);
    raw_parameter_id = Parameter_GetU16(&payload[4]);
    value_type = payload[6];
    flags = payload[7];
    value = Parameter_GetFloat(&payload[8]);

    g_parameter_service_diag.received_frame_count++;
    ack.transaction_id = transaction_id;
    ack.parameter_id = raw_parameter_id;
    ack.status = PARAMETER_STATUS_BAD_FORMAT;
    ack.reserved = 0U;
    ack.applied_value = 0.0f;
    ack.apply_sequence = g_control_tuning_params.update_sequence;

    if ((value_type != 1U) || (flags != 0U)) {
        g_parameter_service_diag.bad_value_count++;
    } else if ((raw_parameter_id < (uint16_t) PARAMETER_ID_KP) ||
        (raw_parameter_id > (uint16_t) PARAMETER_ID_TARGET)) {
        ack.status = PARAMETER_STATUS_BAD_PARAMETER;
        g_parameter_service_diag.bad_value_count++;
    } else {
        parameter_id = (parameter_id_t) raw_parameter_id;
        if (!Parameter_IsAllowed(parameter_id, value)) {
            ack.status = PARAMETER_STATUS_BAD_VALUE;
            g_parameter_service_diag.bad_value_count++;
        } else {
            request_valid = true;
        }
    }

    taskENTER_CRITICAL();
    if (s_last_transaction_valid &&
        (transaction_id == s_last_ack.transaction_id)) {
        cached_ack = s_last_ack;
        cached_duplicate = true;
    } else if (s_pending_valid) {
        if (transaction_id == s_pending.transaction_id) {
            pending_duplicate = true;
        } else {
            busy = true;
        }
    } else if (request_valid) {
        s_pending.transaction_id = transaction_id;
        s_pending.parameter_id = parameter_id;
        s_pending.value = value;
        s_pending_valid = true;
        g_parameter_service_diag.pending = 1U;
        staged = true;
    } else {
        s_last_ack = ack;
        s_last_transaction_valid = true;
        cached_invalid = true;
    }
    taskEXIT_CRITICAL();

    if (cached_duplicate) {
        g_parameter_service_diag.duplicate_count++;
        Parameter_SendAck(&cached_ack);
        return;
    }
    if (pending_duplicate) {
        g_parameter_service_diag.duplicate_count++;
        return;
    }
    if (busy) {
        ack.status = PARAMETER_STATUS_BUSY;
        g_parameter_service_diag.busy_count++;
        Parameter_RecordAck(&ack, false);
        return;
    }
    if (staged) {
        g_parameter_service_diag.staged_count++;
        return;
    }
    if (cached_invalid) {
        Parameter_RecordAck(&ack, false);
    }
}

static void Parameter_ResetParser(void)
{
    s_frame_length = 0U;
    s_expected_length = 0U;
}

static bool Parameter_HeaderIsValid(void)
{
    return (s_frame_length >= 6U) &&
        (s_frame[2] == TELEMETRY_PROTOCOL_VERSION) &&
        (s_frame[3] == PARAMETER_FRAME_TYPE_SET) &&
        (Parameter_GetU16(&s_frame[4]) == PARAMETER_SET_PAYLOAD_BYTES);
}

static void Parameter_ResyncParser(void)
{
    uint16_t index;

    for (index = 1U; (index + 1U) < s_frame_length; index++) {
        uint16_t remaining;

        if ((s_frame[index] != PARAMETER_SYNC_0) ||
            (s_frame[index + 1U] != PARAMETER_SYNC_1)) {
            continue;
        }
        remaining = (uint16_t) (s_frame_length - index);
        if ((remaining >= 6U) &&
            ((s_frame[index + 2U] != TELEMETRY_PROTOCOL_VERSION) ||
             (s_frame[index + 3U] != PARAMETER_FRAME_TYPE_SET) ||
             (Parameter_GetU16(&s_frame[index + 4U]) !=
                PARAMETER_SET_PAYLOAD_BYTES))) {
            continue;
        }
        memmove(s_frame, &s_frame[index], remaining);
        s_frame_length = remaining;
        s_expected_length = (remaining >= 6U) ?
            (uint16_t) (16U + PARAMETER_SET_PAYLOAD_BYTES) : 0U;
        g_parameter_service_diag.resync_count++;
        return;
    }

    if ((s_frame_length != 0U) &&
        (s_frame[s_frame_length - 1U] == PARAMETER_SYNC_0)) {
        s_frame[0] = PARAMETER_SYNC_0;
        s_frame_length = 1U;
        s_expected_length = 0U;
    } else {
        Parameter_ResetParser();
    }
    g_parameter_service_diag.resync_count++;
}

static void Parameter_HandleRxOverflow(void)
{
    uint32_t overflow_count = SerialRx_GetOverflowCount();

    if (overflow_count == s_seen_rx_overflow_count) {
        return;
    }
    SerialRx_Flush();
    Parameter_ResetParser();
    s_seen_rx_overflow_count = overflow_count;
    g_parameter_service_diag.overflow_reset_count++;
}

static void Parameter_ConsumeByte(uint8_t byte)
{

    if (s_frame_length == 0U) {
        if (byte == PARAMETER_SYNC_0) {
            s_frame[0] = byte;
            s_frame_length = 1U;
        }
        return;
    }

    if (s_frame_length == 1U) {
        if (byte == PARAMETER_SYNC_1) {
            s_frame[s_frame_length++] = byte;
        } else {
            s_frame_length = 0U;
            if (byte == PARAMETER_SYNC_0) {
                s_frame[0] = byte;
                s_frame_length = 1U;
            }
        }
        return;
    }

    if (s_frame_length >= PARAMETER_MAX_FRAME_BYTES) {
        Parameter_ResetParser();
        return;
    }

    s_frame[s_frame_length++] = byte;
    if (s_frame_length == 6U) {
        if (!Parameter_HeaderIsValid()) {
            if ((s_frame[2] != TELEMETRY_PROTOCOL_VERSION) ||
                (s_frame[3] != PARAMETER_FRAME_TYPE_SET)) {
                g_parameter_service_diag.bad_type_count++;
            } else {
                g_parameter_service_diag.bad_length_count++;
            }
            Parameter_ResyncParser();
            return;
        }
        s_expected_length =
            (uint16_t) (16U + PARAMETER_SET_PAYLOAD_BYTES);
    }

    if ((s_expected_length != 0U) &&
        (s_frame_length == s_expected_length)) {
        uint16_t received_crc;
        uint16_t calculated_crc;

        if (SerialRx_GetOverflowCount() != s_seen_rx_overflow_count) {
            Parameter_HandleRxOverflow();
            return;
        }
        received_crc = Parameter_GetU16(
            &s_frame[14U + PARAMETER_SET_PAYLOAD_BYTES]);
        calculated_crc = Parameter_Crc16(
            &s_frame[2], (uint16_t) (12U + PARAMETER_SET_PAYLOAD_BYTES));
        if (received_crc != calculated_crc) {
            g_parameter_service_diag.crc_error_count++;
            Parameter_ResyncParser();
            return;
        }
        Parameter_HandleValidFrame();
        Parameter_ResetParser();
    }
}

void ParameterService_Init(void)
{
    s_frame_length = 0U;
    s_expected_length = 0U;
    s_last_byte_us = BSP_Time_GetUs();
    s_seen_rx_overflow_count = SerialRx_GetOverflowCount();
    g_parameter_service_diag.frame_timeout_count = 0U;
    g_parameter_service_diag.overflow_reset_count = 0U;
    g_parameter_service_diag.resync_count = 0U;
    s_last_transaction_valid = false;
    s_pending_valid = false;
    s_pending.transaction_id = 0U;
    s_pending.parameter_id = PARAMETER_ID_KP;
    s_pending.value = 0.0f;
    s_last_ack.transaction_id = 0U;
    s_last_ack.parameter_id = 0U;
    s_last_ack.status = PARAMETER_STATUS_BAD_FORMAT;
    s_last_ack.reserved = 0U;
    s_last_ack.applied_value = 0.0f;
    s_last_ack.apply_sequence = 0U;
    g_control_tuning_params.kp = 1.0f;
    g_control_tuning_params.ki = 0.0f;
    g_control_tuning_params.kd = 0.0f;
    g_control_tuning_params.target = 0.0f;
    g_control_tuning_params.update_sequence = 0U;
    g_parameter_service_diag.received_frame_count = 0U;
    g_parameter_service_diag.crc_error_count = 0U;
    g_parameter_service_diag.bad_length_count = 0U;
    g_parameter_service_diag.bad_type_count = 0U;
    g_parameter_service_diag.bad_value_count = 0U;
    g_parameter_service_diag.staged_count = 0U;
    g_parameter_service_diag.applied_count = 0U;
    g_parameter_service_diag.duplicate_count = 0U;
    g_parameter_service_diag.busy_count = 0U;
    g_parameter_service_diag.ack_publish_drop_count = 0U;
    g_parameter_service_diag.processed_byte_count = 0U;
    g_parameter_service_diag.last_transaction_id = 0U;
    g_parameter_service_diag.last_parameter_id = 0U;
    g_parameter_service_diag.last_status = PARAMETER_STATUS_BAD_FORMAT;
    g_parameter_service_diag.pending = 0U;
    g_parameter_service_diag.initialized = 1U;
}

void ParameterService_ProcessRx(void)
{
    uint8_t byte;
    uint16_t processed = 0U;
    uint32_t now_us = BSP_Time_GetUs();

    Parameter_HandleRxOverflow();
    if ((s_frame_length != 0U) &&
        ((uint32_t) (now_us - s_last_byte_us) >=
            PARAMETER_FRAME_TIMEOUT_US)) {
        Parameter_ResetParser();
        g_parameter_service_diag.frame_timeout_count++;
    }
    while ((processed < 128U) && SerialRx_TryRead(&byte)) {
        if (SerialRx_GetOverflowCount() != s_seen_rx_overflow_count) {
            Parameter_HandleRxOverflow();
            break;
        }
        Parameter_ConsumeByte(byte);
        g_parameter_service_diag.processed_byte_count++;
        processed++;
        now_us = BSP_Time_GetUs();
        if (s_frame_length != 0U) {
            s_last_byte_us = now_us;
        }
    }
    Parameter_HandleRxOverflow();
}

void ParameterService_GetSnapshot(control_tuning_parameters_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    taskENTER_CRITICAL();
    snapshot->kp = g_control_tuning_params.kp;
    snapshot->ki = g_control_tuning_params.ki;
    snapshot->kd = g_control_tuning_params.kd;
    snapshot->target = g_control_tuning_params.target;
    snapshot->update_sequence =
        g_control_tuning_params.update_sequence;
    taskEXIT_CRITICAL();
}

void ParameterService_ApplyPendingAtControlBoundary(void)
{
    pending_parameter_t pending;
    telemetry_parameter_ack_t ack;

    taskENTER_CRITICAL();
    if (!s_pending_valid) {
        taskEXIT_CRITICAL();
        return;
    }

    pending = s_pending;
    s_pending_valid = false;
    g_parameter_service_diag.pending = 0U;
    switch (pending.parameter_id) {
        case PARAMETER_ID_KP:
            g_control_tuning_params.kp = pending.value;
            break;
        case PARAMETER_ID_KI:
            g_control_tuning_params.ki = pending.value;
            break;
        case PARAMETER_ID_KD:
            g_control_tuning_params.kd = pending.value;
            break;
        case PARAMETER_ID_TARGET:
            g_control_tuning_params.target = pending.value;
            break;
        default:
            taskEXIT_CRITICAL();
            return;
    }
    g_control_tuning_params.update_sequence++;
    ack.apply_sequence = g_control_tuning_params.update_sequence;
    taskEXIT_CRITICAL();

    ack.transaction_id = pending.transaction_id;
    ack.parameter_id = (uint16_t) pending.parameter_id;
    ack.status = PARAMETER_STATUS_APPLIED;
    ack.reserved = 0U;
    ack.applied_value = pending.value;
    g_parameter_service_diag.applied_count++;
    Parameter_RecordAck(&ack, true);
}
