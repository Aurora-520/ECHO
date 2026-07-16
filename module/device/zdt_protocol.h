#ifndef ECHO_ZDT_PROTOCOL_H
#define ECHO_ZDT_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define ZDT_PROTOCOL_CHECK_BYTE      0x6BU
#define ZDT_PROTOCOL_MAX_FRAME_BYTES 20U
#define ZDT_PROTOCOL_MAX_SPEED_RPM   3000U

typedef enum {
    ZDT_DIRECTION_CW = 0U,
    ZDT_DIRECTION_CCW = 1U
} zdt_direction_t;

typedef enum {
    ZDT_POSITION_RELATIVE_TO_LAST_TARGET = 0U,
    ZDT_POSITION_ABSOLUTE = 1U,
    ZDT_POSITION_RELATIVE_TO_CURRENT = 2U
} zdt_position_mode_t;

typedef enum {
    ZDT_REPLY_ACCEPTED = 0x02U,
    ZDT_REPLY_REACHED = 0x9FU,
    ZDT_REPLY_CONDITION_REJECTED = 0xE2U,
    ZDT_REPLY_COMMAND_ERROR = 0xEEU
} zdt_reply_status_t;

typedef enum {
    ZDT_QUERY_FIRMWARE_VERSION = 0x1FU,
    ZDT_QUERY_REALTIME_SPEED = 0x35U,
    ZDT_QUERY_REALTIME_POSITION = 0x36U,
    ZDT_QUERY_MOTOR_STATUS = 0x3AU
} zdt_query_t;

typedef struct {
    uint8_t bytes[ZDT_PROTOCOL_MAX_FRAME_BYTES];
    uint8_t length;
} zdt_protocol_frame_t;

typedef struct {
    uint8_t address;
    uint8_t function;
    zdt_reply_status_t status;
} zdt_protocol_reply_t;

uint8_t ZdtProtocol_AccelerationCode(uint32_t acceleration_rpm_s);
uint32_t ZdtProtocol_AccelerationRpmPerSecond(uint8_t code);

bool ZdtProtocol_BuildEnable(uint8_t address, bool enable,
    bool synchronize, zdt_protocol_frame_t *frame);
bool ZdtProtocol_BuildSpeed(uint8_t address, int16_t speed_rpm,
    uint32_t acceleration_rpm_s, bool synchronize,
    zdt_protocol_frame_t *frame);
bool ZdtProtocol_BuildPosition(uint8_t address, int32_t pulse_count,
    uint16_t speed_rpm, uint32_t acceleration_rpm_s,
    zdt_position_mode_t mode, bool synchronize,
    zdt_protocol_frame_t *frame);
bool ZdtProtocol_BuildStop(uint8_t address, bool synchronize,
    zdt_protocol_frame_t *frame);
bool ZdtProtocol_BuildSynchronousStart(uint8_t address,
    zdt_protocol_frame_t *frame);
bool ZdtProtocol_BuildQuery(uint8_t address, zdt_query_t query,
    zdt_protocol_frame_t *frame);

bool ZdtProtocol_ParseReply(const uint8_t *data, uint8_t length,
    zdt_protocol_reply_t *reply);
bool ZdtProtocol_ParseSpeed(const uint8_t *data, uint8_t length,
    uint8_t expected_address, int16_t *speed_rpm);
bool ZdtProtocol_ParsePosition(const uint8_t *data, uint8_t length,
    uint8_t expected_address, int32_t *position_counts);
bool ZdtProtocol_ParseMotorStatus(const uint8_t *data, uint8_t length,
    uint8_t expected_address, uint8_t *status_flags);

#endif
