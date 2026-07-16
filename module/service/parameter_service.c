#include "parameter_service.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "telemetry.h"
#include "task.h"

typedef struct {
    uint32_t transaction_id;
    parameter_id_t parameter_id;
    float value;
    parameter_origin_t origin;
    bool restore_defaults;
} pending_parameter_t;

static const parameter_metadata_t s_parameter_metadata[PARAMETER_COUNT] = {
    { PARAMETER_ID_KP, "KP", PARAMETER_TYPE_FLOAT32,
      8.0f, 0.0f, 1000.0f, 0.1f, "",
      PARAMETER_FLAG_FIELD_EDITABLE | PARAMETER_FLAG_PERSISTENT,
      PARAMETER_SCHEMA_VERSION },
    { PARAMETER_ID_KI, "KI", PARAMETER_TYPE_FLOAT32,
      18.0f, 0.0f, 1000.0f, 0.1f, "",
      PARAMETER_FLAG_FIELD_EDITABLE | PARAMETER_FLAG_PERSISTENT,
      PARAMETER_SCHEMA_VERSION },
    { PARAMETER_ID_KD, "KD", PARAMETER_TYPE_FLOAT32,
      0.0f, 0.0f, 1000.0f, 0.01f, "",
      PARAMETER_FLAG_FIELD_EDITABLE | PARAMETER_FLAG_PERSISTENT,
      PARAMETER_SCHEMA_VERSION },
    { PARAMETER_ID_TARGET, "TARGET", PARAMETER_TYPE_FLOAT32,
      0.0f, -45.0f, 45.0f, 0.5f, "rpm",
      PARAMETER_FLAG_FIELD_EDITABLE | PARAMETER_FLAG_PERSISTENT,
      PARAMETER_SCHEMA_VERSION }
};

volatile control_tuning_parameters_t g_control_tuning_params = {
    8.0f, 18.0f, 0.0f, 0.0f, 0U
};
volatile parameter_service_diagnostics_t g_parameter_service_diag;

static bool s_last_transaction_valid;
static telemetry_parameter_ack_t s_last_ack;
static bool s_pending_valid;
static pending_parameter_t s_pending;

static bool Parameter_IsFinite(float value)
{
    uint32_t bits;
    uint32_t exponent;

    memcpy(&bits, &value, sizeof(bits));
    exponent = (bits >> 23) & 0xFFU;
    return exponent != 0xFFU;
}

static parameter_status_t Parameter_ValidateValue(
    uint16_t id, float value)
{
    const parameter_metadata_t *metadata = ParameterService_GetMetadata(id);

    if (metadata == NULL) {
        return PARAMETER_STATUS_BAD_PARAMETER;
    }
    if (!Parameter_IsFinite(value)) {
        return PARAMETER_STATUS_BAD_VALUE;
    }
    if ((value < metadata->minimum_value) ||
        (value > metadata->maximum_value)) {
        return PARAMETER_STATUS_BAD_VALUE;
    }
    return PARAMETER_STATUS_STAGED;
}

static void Parameter_SendAck(const telemetry_parameter_ack_t *ack)
{
    if (!Telemetry_PublishParameterAck(ack)) {
        g_parameter_service_diag.ack_publish_drop_count++;
    }
}

static void Parameter_RecordResult(const telemetry_parameter_ack_t *ack,
    bool cache, bool publish, parameter_origin_t origin)
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
    g_parameter_service_diag.last_origin = (uint8_t) origin;
    if (publish) {
        Parameter_SendAck(ack);
    }
}

void ParameterService_HandleUartSet(uint32_t transaction_id,
    uint16_t raw_parameter_id, uint8_t value_type,
    uint8_t flags, float value)
{
    telemetry_parameter_ack_t ack;
    telemetry_parameter_ack_t cached_ack;
    bool cached_duplicate = false;
    bool pending_duplicate = false;
    bool busy = false;
    parameter_status_t status;

    g_parameter_service_diag.received_frame_count++;
    ack.transaction_id = transaction_id;
    ack.parameter_id = raw_parameter_id;
    ack.status = PARAMETER_STATUS_BAD_FORMAT;
    ack.reserved = 0U;
    ack.applied_value = 0.0f;
    ack.apply_sequence = g_control_tuning_params.update_sequence;

    if ((value_type != 1U) || (flags != 0U)) {
        g_parameter_service_diag.bad_value_count++;
        Parameter_RecordResult(&ack, true, true, PARAMETER_ORIGIN_UART);
        return;
    }

    taskENTER_CRITICAL();
    if (s_last_transaction_valid &&
        (transaction_id == s_last_ack.transaction_id)) {
        cached_ack = s_last_ack;
        cached_duplicate = true;
    } else if (s_pending_valid) {
        if ((s_pending.origin == PARAMETER_ORIGIN_UART) &&
            (transaction_id == s_pending.transaction_id)) {
            pending_duplicate = true;
        } else {
            busy = true;
        }
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
        Parameter_RecordResult(
            &ack, false, true, PARAMETER_ORIGIN_UART);
        return;
    }

    status = ParameterService_StageValue(transaction_id,
        raw_parameter_id, value, PARAMETER_ORIGIN_UART);
    if (status == PARAMETER_STATUS_STAGED) {
        return;
    }

    ack.status = (uint8_t) status;
    if ((status == PARAMETER_STATUS_BAD_PARAMETER) ||
        (status == PARAMETER_STATUS_BAD_VALUE)) {
        g_parameter_service_diag.bad_value_count++;
        Parameter_RecordResult(&ack, true, true, PARAMETER_ORIGIN_UART);
    } else {
        Parameter_RecordResult(&ack, false, true, PARAMETER_ORIGIN_UART);
    }
}

void ParameterService_Init(void)
{
    memset((void *) &g_parameter_service_diag, 0,
        sizeof(g_parameter_service_diag));
    s_last_transaction_valid = false;
    s_pending_valid = false;
    s_pending.transaction_id = 0U;
    s_pending.parameter_id = PARAMETER_ID_KP;
    s_pending.value = 0.0f;
    s_pending.origin = PARAMETER_ORIGIN_UART;
    s_pending.restore_defaults = false;
    s_last_ack.transaction_id = 0U;
    s_last_ack.parameter_id = 0U;
    s_last_ack.status = PARAMETER_STATUS_BAD_FORMAT;
    s_last_ack.reserved = 0U;
    s_last_ack.applied_value = 0.0f;
    s_last_ack.apply_sequence = 0U;
    g_control_tuning_params.kp = s_parameter_metadata[0].default_value;
    g_control_tuning_params.ki = s_parameter_metadata[1].default_value;
    g_control_tuning_params.kd = s_parameter_metadata[2].default_value;
    g_control_tuning_params.target = s_parameter_metadata[3].default_value;
    g_control_tuning_params.update_sequence = 0U;
    g_parameter_service_diag.last_status = PARAMETER_STATUS_BAD_FORMAT;
    g_parameter_service_diag.initialized = 1U;
}

size_t ParameterService_GetCount(void)
{
    return PARAMETER_COUNT;
}

const parameter_metadata_t *ParameterService_GetMetadataByIndex(size_t index)
{
    if (index >= PARAMETER_COUNT) {
        return NULL;
    }
    return &s_parameter_metadata[index];
}

const parameter_metadata_t *ParameterService_GetMetadata(uint16_t id)
{
    size_t index;

    for (index = 0U; index < PARAMETER_COUNT; index++) {
        if ((uint16_t) s_parameter_metadata[index].id == id) {
            return &s_parameter_metadata[index];
        }
    }
    return NULL;
}

parameter_status_t ParameterService_StageValue(uint32_t transaction_id,
    uint16_t parameter_id, float value, parameter_origin_t origin)
{
    parameter_status_t status =
        Parameter_ValidateValue(parameter_id, value);

    if (status != PARAMETER_STATUS_STAGED) {
        g_parameter_service_diag.validation_reject_count++;
        g_parameter_service_diag.last_transaction_id = transaction_id;
        g_parameter_service_diag.last_parameter_id = parameter_id;
        g_parameter_service_diag.last_status = (uint8_t) status;
        g_parameter_service_diag.last_origin = (uint8_t) origin;
        return status;
    }

    taskENTER_CRITICAL();
    if (s_pending_valid) {
        taskEXIT_CRITICAL();
        g_parameter_service_diag.busy_count++;
        g_parameter_service_diag.last_transaction_id = transaction_id;
        g_parameter_service_diag.last_parameter_id = parameter_id;
        g_parameter_service_diag.last_status = PARAMETER_STATUS_BUSY;
        g_parameter_service_diag.last_origin = (uint8_t) origin;
        return PARAMETER_STATUS_BUSY;
    }
    s_pending.transaction_id = transaction_id;
    s_pending.parameter_id = (parameter_id_t) parameter_id;
    s_pending.value = value;
    s_pending.origin = origin;
    s_pending.restore_defaults = false;
    s_pending_valid = true;
    g_parameter_service_diag.pending = 1U;
    taskEXIT_CRITICAL();

    g_parameter_service_diag.staged_count++;
    g_parameter_service_diag.last_transaction_id = transaction_id;
    g_parameter_service_diag.last_parameter_id = parameter_id;
    g_parameter_service_diag.last_status = PARAMETER_STATUS_STAGED;
    g_parameter_service_diag.last_origin = (uint8_t) origin;
    return PARAMETER_STATUS_STAGED;
}

parameter_status_t ParameterService_StageDefaults(uint32_t transaction_id,
    parameter_origin_t origin)
{
    taskENTER_CRITICAL();
    if (s_pending_valid) {
        taskEXIT_CRITICAL();
        g_parameter_service_diag.busy_count++;
        g_parameter_service_diag.last_transaction_id = transaction_id;
        g_parameter_service_diag.last_parameter_id = 0U;
        g_parameter_service_diag.last_status = PARAMETER_STATUS_BUSY;
        g_parameter_service_diag.last_origin = (uint8_t) origin;
        return PARAMETER_STATUS_BUSY;
    }
    s_pending.transaction_id = transaction_id;
    s_pending.parameter_id = (parameter_id_t) 0U;
    s_pending.value = 0.0f;
    s_pending.origin = origin;
    s_pending.restore_defaults = true;
    s_pending_valid = true;
    g_parameter_service_diag.pending = 1U;
    taskEXIT_CRITICAL();

    g_parameter_service_diag.staged_count++;
    g_parameter_service_diag.defaults_staged_count++;
    g_parameter_service_diag.last_transaction_id = transaction_id;
    g_parameter_service_diag.last_parameter_id = 0U;
    g_parameter_service_diag.last_status = PARAMETER_STATUS_STAGED;
    g_parameter_service_diag.last_origin = (uint8_t) origin;
    return PARAMETER_STATUS_STAGED;
}

bool ParameterService_GetValue(parameter_id_t id, float *value)
{
    bool valid = true;

    if (value == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    switch (id) {
        case PARAMETER_ID_KP:
            *value = g_control_tuning_params.kp;
            break;
        case PARAMETER_ID_KI:
            *value = g_control_tuning_params.ki;
            break;
        case PARAMETER_ID_KD:
            *value = g_control_tuning_params.kd;
            break;
        case PARAMETER_ID_TARGET:
            *value = g_control_tuning_params.target;
            break;
        default:
            valid = false;
            break;
    }
    taskEXIT_CRITICAL();
    return valid;
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
    if (pending.restore_defaults) {
        g_control_tuning_params.kp = s_parameter_metadata[0].default_value;
        g_control_tuning_params.ki = s_parameter_metadata[1].default_value;
        g_control_tuning_params.kd = s_parameter_metadata[2].default_value;
        g_control_tuning_params.target =
            s_parameter_metadata[3].default_value;
    } else {
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
    }
    g_control_tuning_params.update_sequence++;
    ack.apply_sequence = g_control_tuning_params.update_sequence;
    taskEXIT_CRITICAL();

    ack.transaction_id = pending.transaction_id;
    ack.parameter_id = pending.restore_defaults ?
        0U : (uint16_t) pending.parameter_id;
    ack.status = PARAMETER_STATUS_APPLIED;
    ack.reserved = 0U;
    ack.applied_value = pending.restore_defaults ? 0.0f : pending.value;
    g_parameter_service_diag.applied_count++;
    if (pending.restore_defaults) {
        g_parameter_service_diag.defaults_applied_count++;
    }
    Parameter_RecordResult(&ack,
        pending.origin == PARAMETER_ORIGIN_UART,
        pending.origin == PARAMETER_ORIGIN_UART, pending.origin);
}

const char *ParameterService_StatusName(parameter_status_t status)
{
    switch (status) {
        case PARAMETER_STATUS_APPLIED:
            return "APPLIED";
        case PARAMETER_STATUS_BAD_PARAMETER:
            return "BAD ID";
        case PARAMETER_STATUS_BAD_VALUE:
            return "BAD VALUE";
        case PARAMETER_STATUS_BAD_FORMAT:
            return "BAD FORMAT";
        case PARAMETER_STATUS_BUSY:
            return "BUSY";
        case PARAMETER_STATUS_STAGED:
            return "STAGED";
        default:
            return "UNKNOWN";
    }
}
