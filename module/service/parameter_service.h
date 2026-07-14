#ifndef ECHO_PARAMETER_SERVICE_H
#define ECHO_PARAMETER_SERVICE_H

#include <stdint.h>

typedef enum {
    PARAMETER_ID_KP = 1U,
    PARAMETER_ID_KI = 2U,
    PARAMETER_ID_KD = 3U,
    PARAMETER_ID_TARGET = 4U
} parameter_id_t;

typedef enum {
    PARAMETER_STATUS_APPLIED = 0U,
    PARAMETER_STATUS_BAD_PARAMETER = 1U,
    PARAMETER_STATUS_BAD_VALUE = 2U,
    PARAMETER_STATUS_BAD_FORMAT = 3U,
    PARAMETER_STATUS_BUSY = 4U
} parameter_status_t;

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
    uint32_t duplicate_count;
    uint32_t busy_count;
    uint32_t ack_publish_drop_count;
    uint32_t processed_byte_count;
    uint32_t last_transaction_id;
    uint16_t last_parameter_id;
    uint8_t last_status;
    uint8_t pending;
    uint8_t initialized;
} parameter_service_diagnostics_t;

extern volatile control_tuning_parameters_t g_control_tuning_params;
extern volatile parameter_service_diagnostics_t g_parameter_service_diag;

void ParameterService_Init(void);

/*
 * ServiceTask parses and validates UART data into a single pending update.
 * SystemTask applies at most one pending update at a control-cycle boundary.
 */
void ParameterService_ProcessRx(void);
void ParameterService_ApplyPendingAtControlBoundary(void);
void ParameterService_GetSnapshot(
    control_tuning_parameters_t *snapshot);

#endif
