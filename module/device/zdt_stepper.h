#ifndef ECHO_ZDT_STEPPER_H
#define ECHO_ZDT_STEPPER_H

#include <stdbool.h>
#include <stdint.h>

#include "zdt_protocol.h"

#define ZDT_STEPPER_AXIS_COUNT 2U

typedef enum {
    ZDT_STEPPER_AXIS_GEN1 = 0U,
    ZDT_STEPPER_AXIS_GEN2,
    ZDT_STEPPER_AXIS_INVALID
} zdt_stepper_axis_t;

typedef enum {
    ZDT_STEPPER_GENERATION_1 = 1U,
    ZDT_STEPPER_GENERATION_2 = 2U
} zdt_stepper_generation_t;

typedef enum {
    ZDT_STEPPER_REQUEST_ACCEPTED = 0U,
    ZDT_STEPPER_REQUEST_DISABLED,
    ZDT_STEPPER_REQUEST_BUSY,
    ZDT_STEPPER_REQUEST_DUPLICATE,
    ZDT_STEPPER_REQUEST_INVALID
} zdt_stepper_request_status_t;

typedef struct {
    zdt_stepper_generation_t generation;
    uint8_t address;
    uint16_t pulses_per_revolution;
    uint8_t allow_position_interrupt;
    uint8_t reserved;
} zdt_stepper_config_t;

typedef struct {
    uint32_t tx_command_count;
    uint32_t tx_query_count;
    uint32_t accepted_request_count;
    uint32_t rejected_request_count;
    uint32_t duplicate_request_count;
    uint32_t response_count;
    uint32_t invalid_response_count;
    uint32_t response_timeout_count;
    uint32_t position_reached_count;
    uint32_t speed_lease_expired_count;
    uint32_t last_response_us;
    int32_t position_counts;
    int32_t position_millidegrees;
    int16_t speed_rpm;
    uint16_t firmware_version;
    uint16_t hardware_version;
    uint8_t motor_status_flags;
    uint8_t last_function;
    uint8_t last_reply_status;
    uint8_t online;
    uint8_t enabled;
    uint8_t motion_active;
    uint8_t stalled;
    uint8_t stall_protected;
    uint8_t pending_command;
    uint8_t initialized;
} zdt_stepper_axis_diagnostics_t;

typedef struct {
    zdt_stepper_axis_diagnostics_t axis[ZDT_STEPPER_AXIS_COUNT];
    uint32_t select_count;
    uint32_t deselect_count;
    uint8_t backend_selected;
    uint8_t shutdown_pending;
    uint8_t initialized;
    uint8_t reserved;
} zdt_stepper_diagnostics_t;

extern const zdt_stepper_config_t
    g_zdt_stepper_config[ZDT_STEPPER_AXIS_COUNT];
extern volatile zdt_stepper_diagnostics_t g_zdt_stepper_diag;

void ZdtStepper_Init(void);
void ZdtStepper_Service(uint32_t now_us);

bool ZdtStepper_SelectBackupBackend(void);
void ZdtStepper_DeselectBackupBackend(void);

zdt_stepper_request_status_t ZdtStepper_RequestEnable(
    zdt_stepper_axis_t axis, bool enable);
zdt_stepper_request_status_t ZdtStepper_RequestSpeed(
    zdt_stepper_axis_t axis, int16_t speed_rpm,
    uint32_t acceleration_rpm_s);
zdt_stepper_request_status_t ZdtStepper_RequestPosition(
    zdt_stepper_axis_t axis, int32_t position_millidegrees,
    uint16_t speed_rpm, uint32_t acceleration_rpm_s,
    zdt_position_mode_t mode);
zdt_stepper_request_status_t ZdtStepper_RequestStop(
    zdt_stepper_axis_t axis);

#endif
