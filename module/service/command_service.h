#ifndef ECHO_COMMAND_SERVICE_H
#define ECHO_COMMAND_SERVICE_H

#include <stdint.h>

typedef struct {
    uint32_t received_frame_count;
    uint32_t parameter_frame_count;
    uint32_t actuator_frame_count;
    uint32_t crc_error_count;
    uint32_t bad_length_count;
    uint32_t bad_type_count;
    uint32_t frame_timeout_count;
    uint32_t overflow_reset_count;
    uint32_t resync_count;
    uint32_t actuator_ack_drop_count;
    uint32_t zdt_frame_count;
    uint32_t zdt_ack_drop_count;
    uint32_t processed_byte_count;
    uint8_t last_frame_type;
    uint8_t initialized;
    uint8_t reserved[2];
} command_service_diagnostics_t;

extern volatile command_service_diagnostics_t g_command_service_diag;

void CommandService_Init(void);
void CommandService_ProcessRx(void);

#endif
