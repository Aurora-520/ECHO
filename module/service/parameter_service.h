#ifndef ECHO_PARAMETER_SERVICE_H
#define ECHO_PARAMETER_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PARAMETER_SCHEMA_VERSION 1U
#define PARAMETER_COUNT          4U

#define PARAMETER_FLAG_FIELD_EDITABLE (1UL << 0)
#define PARAMETER_FLAG_PERSISTENT     (1UL << 1)

typedef enum {
    PARAMETER_ID_KP = 1U,
    PARAMETER_ID_KI = 2U,
    PARAMETER_ID_KD = 3U,
    PARAMETER_ID_TARGET = 4U
} parameter_id_t;

typedef enum {
    PARAMETER_TYPE_FLOAT32 = 1U
} parameter_type_t;

typedef enum {
    PARAMETER_ORIGIN_UART = 1U,
    PARAMETER_ORIGIN_OLED = 2U
} parameter_origin_t;

typedef enum {
    PARAMETER_STATUS_APPLIED = 0U,
    PARAMETER_STATUS_BAD_PARAMETER = 1U,
    PARAMETER_STATUS_BAD_VALUE = 2U,
    PARAMETER_STATUS_BAD_FORMAT = 3U,
    PARAMETER_STATUS_BUSY = 4U,
    PARAMETER_STATUS_STAGED = 5U
} parameter_status_t;

typedef struct {
    parameter_id_t id;
    const char *name;
    parameter_type_t type;
    float default_value;
    float minimum_value;
    float maximum_value;
    float step;
    const char *units;
    uint32_t flags;
    uint16_t version;
} parameter_metadata_t;

typedef struct {
    float kp;
    float ki;
    float kd;
    float target;
    uint32_t update_sequence;
} control_tuning_parameters_t;

typedef struct {
    uint32_t received_frame_count;
    uint32_t crc_error_count;
    uint32_t bad_length_count;
    uint32_t bad_type_count;
    uint32_t bad_value_count;
    uint32_t frame_timeout_count;
    uint32_t overflow_reset_count;
    uint32_t resync_count;
    uint32_t staged_count;
    uint32_t applied_count;
    uint32_t defaults_staged_count;
    uint32_t defaults_applied_count;
    uint32_t validation_reject_count;
    uint32_t duplicate_count;
    uint32_t busy_count;
    uint32_t ack_publish_drop_count;
    uint32_t processed_byte_count;
    uint32_t last_transaction_id;
    uint16_t last_parameter_id;
    uint8_t last_status;
    uint8_t last_origin;
    uint8_t pending;
    uint8_t initialized;
} parameter_service_diagnostics_t;

extern volatile control_tuning_parameters_t g_control_tuning_params;
extern volatile parameter_service_diagnostics_t g_parameter_service_diag;

void ParameterService_Init(void);

/* CommandService parses UART; SystemTask is the only applied-value writer. */
void ParameterService_HandleUartSet(uint32_t transaction_id,
    uint16_t parameter_id, uint8_t value_type,
    uint8_t flags, float value);
void ParameterService_ApplyPendingAtControlBoundary(void);

parameter_status_t ParameterService_StageValue(uint32_t transaction_id,
    uint16_t parameter_id, float value, parameter_origin_t origin);
parameter_status_t ParameterService_StageDefaults(uint32_t transaction_id,
    parameter_origin_t origin);

size_t ParameterService_GetCount(void);
const parameter_metadata_t *ParameterService_GetMetadataByIndex(size_t index);
const parameter_metadata_t *ParameterService_GetMetadata(uint16_t id);
bool ParameterService_GetValue(parameter_id_t id, float *value);
void ParameterService_GetSnapshot(control_tuning_parameters_t *snapshot);
const char *ParameterService_StatusName(parameter_status_t status);

#endif
