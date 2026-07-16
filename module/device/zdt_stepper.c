#include "zdt_stepper.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "bsp_zdt_uart.h"

#define ZDT_STEPPER_MIN_TX_INTERVAL_US   20000UL
#define ZDT_STEPPER_POLL_INTERVAL_US     50000UL
#define ZDT_STEPPER_RESPONSE_TIMEOUT_US  20000UL
#define ZDT_STEPPER_OFFLINE_TIMEOUTS     3U
#define ZDT_STEPPER_SPEED_LEASE_US       1500000UL

#define ZDT_STATUS_ENABLED         (1U << 0)
#define ZDT_STATUS_POSITION_REACHED (1U << 1)
#define ZDT_STATUS_STALLED         (1U << 2)
#define ZDT_STATUS_STALL_PROTECTED (1U << 3)

typedef enum {
    ZDT_MOTION_NONE = 0U,
    ZDT_MOTION_SPEED,
    ZDT_MOTION_POSITION
} zdt_motion_type_t;

typedef enum {
    ZDT_SHUTDOWN_NONE = 0U,
    ZDT_SHUTDOWN_STOP_PENDING,
    ZDT_SHUTDOWN_DISABLE_PENDING,
    ZDT_SHUTDOWN_COMPLETE
} zdt_shutdown_state_t;

typedef struct {
    zdt_protocol_frame_t pending_frame;
    zdt_protocol_frame_t last_motion_frame;
    uint32_t last_tx_us;
    uint32_t next_poll_us;
    uint32_t response_deadline_us;
    uint32_t speed_lease_deadline_us;
    uint8_t rx_frame[ZDT_PROTOCOL_MAX_FRAME_BYTES];
    uint8_t rx_length;
    uint8_t rx_expected_length;
    uint8_t expected_query;
    uint8_t poll_index;
    uint8_t consecutive_timeouts;
    uint8_t pending_valid;
    uint8_t last_motion_valid;
    uint8_t motion_type;
    uint8_t speed_lease_active;
    uint8_t shutdown_state;
} zdt_stepper_state_t;

const zdt_stepper_config_t
    g_zdt_stepper_config[ZDT_STEPPER_AXIS_COUNT] = {
        { ZDT_STEPPER_GENERATION_1, 1U, 3200U, 0U, 0U },
        { ZDT_STEPPER_GENERATION_2, 1U, 3200U, 1U, 0U }
    };

static zdt_stepper_state_t s_state[ZDT_STEPPER_AXIS_COUNT];
volatile zdt_stepper_diagnostics_t g_zdt_stepper_diag;

static bool ZdtStepper_AxisIsValid(zdt_stepper_axis_t axis)
{
    return (uint32_t) axis < ZDT_STEPPER_AXIS_COUNT;
}

static bsp_zdt_uart_port_t ZdtStepper_Port(zdt_stepper_axis_t axis)
{
    return (axis == ZDT_STEPPER_AXIS_GEN1) ?
        BSP_ZDT_UART_GEN1 : BSP_ZDT_UART_GEN2;
}

static bool ZdtStepper_TimeReached(uint32_t now_us, uint32_t target_us)
{
    return (int32_t) (now_us - target_us) >= 0;
}

static bool ZdtStepper_FrameEquals(const zdt_protocol_frame_t *left,
    const zdt_protocol_frame_t *right)
{
    return (left->length == right->length) &&
        (memcmp(left->bytes, right->bytes, left->length) == 0);
}

static zdt_stepper_request_status_t ZdtStepper_StageFrame(
    zdt_stepper_axis_t axis, const zdt_protocol_frame_t *frame,
    zdt_motion_type_t motion_type)
{
    zdt_stepper_state_t *state = &s_state[axis];

    if (state->pending_valid != 0U) {
        g_zdt_stepper_diag.axis[axis].rejected_request_count++;
        return ZDT_STEPPER_REQUEST_BUSY;
    }

    if ((motion_type != ZDT_MOTION_NONE) &&
        (state->last_motion_valid != 0U) &&
        ZdtStepper_FrameEquals(frame, &state->last_motion_frame)) {
        g_zdt_stepper_diag.axis[axis].duplicate_request_count++;
        return ZDT_STEPPER_REQUEST_DUPLICATE;
    }

    state->pending_frame = *frame;
    state->pending_valid = 1U;
    g_zdt_stepper_diag.axis[axis].pending_command = 1U;
    g_zdt_stepper_diag.axis[axis].accepted_request_count++;

    if (motion_type != ZDT_MOTION_NONE) {
        state->last_motion_frame = *frame;
        state->last_motion_valid = 1U;
        state->motion_type = (uint8_t) motion_type;
    }
    return ZDT_STEPPER_REQUEST_ACCEPTED;
}

static uint8_t ZdtStepper_ResponseLength(
    zdt_stepper_axis_t axis, uint8_t function)
{
    switch (function) {
        case (uint8_t) ZDT_QUERY_FIRMWARE_VERSION:
            return (g_zdt_stepper_config[axis].generation ==
                    ZDT_STEPPER_GENERATION_1) ? 5U : 7U;
        case (uint8_t) ZDT_QUERY_REALTIME_SPEED:
            return 6U;
        case (uint8_t) ZDT_QUERY_REALTIME_POSITION:
            return 8U;
        case (uint8_t) ZDT_QUERY_MOTOR_STATUS:
        case 0x00U:
        case 0xF3U:
        case 0xF6U:
        case 0xFDU:
        case 0xFEU:
        case 0xFFU:
            return 4U;
        default:
            return 0U;
    }
}

static void ZdtStepper_RecordValidResponse(
    zdt_stepper_axis_t axis, uint8_t function, uint32_t now_us)
{
    volatile zdt_stepper_axis_diagnostics_t *diagnostics =
        &g_zdt_stepper_diag.axis[axis];

    diagnostics->response_count++;
    diagnostics->last_response_us = now_us;
    diagnostics->last_function = function;
    diagnostics->online = 1U;
    s_state[axis].consecutive_timeouts = 0U;
    if (s_state[axis].expected_query == function) {
        s_state[axis].expected_query = 0U;
    }
}

static void ZdtStepper_ProcessResponse(zdt_stepper_axis_t axis,
    const uint8_t *data, uint8_t length, uint32_t now_us)
{
    volatile zdt_stepper_axis_diagnostics_t *diagnostics =
        &g_zdt_stepper_diag.axis[axis];
    zdt_protocol_reply_t reply;
    int32_t position_counts;
    int16_t speed_rpm;
    uint8_t status_flags;
    uint8_t function = data[1];

    if (function == (uint8_t) ZDT_QUERY_REALTIME_SPEED) {
        if (!ZdtProtocol_ParseSpeed(data, length,
                g_zdt_stepper_config[axis].address, &speed_rpm)) {
            diagnostics->invalid_response_count++;
            return;
        }
        diagnostics->speed_rpm = speed_rpm;
        ZdtStepper_RecordValidResponse(axis, function, now_us);
        return;
    }

    if (function == (uint8_t) ZDT_QUERY_REALTIME_POSITION) {
        int64_t millidegrees;

        if (!ZdtProtocol_ParsePosition(data, length,
                g_zdt_stepper_config[axis].address, &position_counts)) {
            diagnostics->invalid_response_count++;
            return;
        }
        millidegrees = ((int64_t) position_counts * 360000LL) / 65536LL;
        diagnostics->position_counts = position_counts;
        diagnostics->position_millidegrees = (int32_t) millidegrees;
        ZdtStepper_RecordValidResponse(axis, function, now_us);
        return;
    }

    if (function == (uint8_t) ZDT_QUERY_MOTOR_STATUS) {
        bool was_reached =
            (diagnostics->motor_status_flags &
             ZDT_STATUS_POSITION_REACHED) != 0U;

        if (!ZdtProtocol_ParseMotorStatus(data, length,
                g_zdt_stepper_config[axis].address, &status_flags)) {
            diagnostics->invalid_response_count++;
            return;
        }
        diagnostics->motor_status_flags = status_flags;
        diagnostics->enabled =
            ((status_flags & ZDT_STATUS_ENABLED) != 0U) ? 1U : 0U;
        diagnostics->stalled =
            ((status_flags & ZDT_STATUS_STALLED) != 0U) ? 1U : 0U;
        diagnostics->stall_protected =
            ((status_flags & ZDT_STATUS_STALL_PROTECTED) != 0U) ? 1U : 0U;
        if ((s_state[axis].motion_type ==
                (uint8_t) ZDT_MOTION_POSITION) &&
            ((status_flags & ZDT_STATUS_POSITION_REACHED) != 0U)) {
            diagnostics->motion_active = 0U;
            if (!was_reached) {
                diagnostics->position_reached_count++;
            }
        }
        ZdtStepper_RecordValidResponse(axis, function, now_us);
        return;
    }

    if (function == (uint8_t) ZDT_QUERY_FIRMWARE_VERSION) {
        if ((data[0] != g_zdt_stepper_config[axis].address) ||
            (data[length - 1U] != ZDT_PROTOCOL_CHECK_BYTE)) {
            diagnostics->invalid_response_count++;
            return;
        }
        if (length == 5U) {
            diagnostics->firmware_version = data[2];
            diagnostics->hardware_version = data[3];
        } else if (length == 7U) {
            diagnostics->firmware_version =
                (uint16_t) (((uint16_t) data[2] << 8) | data[3]);
            diagnostics->hardware_version =
                (uint16_t) (((uint16_t) data[4] << 8) | data[5]);
        } else {
            diagnostics->invalid_response_count++;
            return;
        }
        ZdtStepper_RecordValidResponse(axis, function, now_us);
        return;
    }

    if (!ZdtProtocol_ParseReply(data, length, &reply) ||
        (reply.address != g_zdt_stepper_config[axis].address)) {
        diagnostics->invalid_response_count++;
        return;
    }

    diagnostics->last_reply_status = (uint8_t) reply.status;
    if (reply.status == ZDT_REPLY_REACHED) {
        diagnostics->motion_active = 0U;
        diagnostics->position_reached_count++;
    }
    ZdtStepper_RecordValidResponse(axis, function, now_us);
}

static void ZdtStepper_ConsumeRx(zdt_stepper_axis_t axis,
    uint32_t now_us)
{
    zdt_stepper_state_t *state = &s_state[axis];
    uint8_t byte;

    while (BSP_ZdtUart_TryRead(ZdtStepper_Port(axis), &byte)) {
        if (state->rx_length == 0U) {
            if (byte == g_zdt_stepper_config[axis].address) {
                state->rx_frame[0] = byte;
                state->rx_length = 1U;
            }
            continue;
        }

        if (state->rx_length == 1U) {
            state->rx_expected_length =
                ZdtStepper_ResponseLength(axis, byte);
            if (state->rx_expected_length == 0U) {
                state->rx_length = 0U;
                continue;
            }
        }

        if (state->rx_length >= ZDT_PROTOCOL_MAX_FRAME_BYTES) {
            state->rx_length = 0U;
            state->rx_expected_length = 0U;
            g_zdt_stepper_diag.axis[axis].invalid_response_count++;
            continue;
        }

        state->rx_frame[state->rx_length++] = byte;
        if ((state->rx_expected_length != 0U) &&
            (state->rx_length == state->rx_expected_length)) {
            ZdtStepper_ProcessResponse(
                axis, state->rx_frame, state->rx_length, now_us);
            state->rx_length = 0U;
            state->rx_expected_length = 0U;
        }
    }
}

static bool ZdtStepper_SendFrame(zdt_stepper_axis_t axis,
    const zdt_protocol_frame_t *frame, uint32_t now_us, bool is_query)
{
    if (!BSP_ZdtUart_TryWrite(
            ZdtStepper_Port(axis), frame->bytes, frame->length)) {
        return false;
    }

    s_state[axis].last_tx_us = now_us;
    if (is_query) {
        g_zdt_stepper_diag.axis[axis].tx_query_count++;
    } else {
        g_zdt_stepper_diag.axis[axis].tx_command_count++;
        g_zdt_stepper_diag.axis[axis].last_function = frame->bytes[1];
        if ((frame->bytes[1] == 0xF6U) ||
            (frame->bytes[1] == 0xFDU)) {
            g_zdt_stepper_diag.axis[axis].motion_active = 1U;
            if (frame->bytes[1] == 0xF6U) {
                s_state[axis].speed_lease_deadline_us =
                    now_us + ZDT_STEPPER_SPEED_LEASE_US;
                s_state[axis].speed_lease_active = 1U;
            } else {
                s_state[axis].speed_lease_active = 0U;
            }
        } else if ((frame->bytes[1] == 0xFEU) ||
                   ((frame->bytes[1] == 0xF3U) &&
                    (frame->bytes[3] == 0U))) {
            g_zdt_stepper_diag.axis[axis].motion_active = 0U;
            s_state[axis].speed_lease_active = 0U;
            s_state[axis].last_motion_valid = 0U;
            s_state[axis].motion_type = (uint8_t) ZDT_MOTION_NONE;
        }
    }
    return true;
}

static void ZdtStepper_ServiceSpeedLease(
    zdt_stepper_axis_t axis, uint32_t now_us)
{
    zdt_stepper_state_t *state = &s_state[axis];
    zdt_protocol_frame_t frame;

    if ((state->speed_lease_active == 0U) ||
        !ZdtStepper_TimeReached(now_us, state->speed_lease_deadline_us)) {
        return;
    }

    state->speed_lease_active = 0U;
    state->pending_valid = 0U;
    g_zdt_stepper_diag.axis[axis].pending_command = 0U;
    if (ZdtProtocol_BuildStop(
            g_zdt_stepper_config[axis].address, false, &frame)) {
        state->pending_frame = frame;
        state->pending_valid = 1U;
        g_zdt_stepper_diag.axis[axis].pending_command = 1U;
        g_zdt_stepper_diag.axis[axis].speed_lease_expired_count++;
    }
}

static zdt_query_t ZdtStepper_NextQuery(zdt_stepper_state_t *state)
{
    static const zdt_query_t query_order[] = {
        ZDT_QUERY_FIRMWARE_VERSION,
        ZDT_QUERY_MOTOR_STATUS,
        ZDT_QUERY_REALTIME_SPEED,
        ZDT_QUERY_REALTIME_POSITION,
        ZDT_QUERY_MOTOR_STATUS,
        ZDT_QUERY_REALTIME_SPEED,
        ZDT_QUERY_REALTIME_POSITION
    };
    zdt_query_t query = query_order[state->poll_index];

    state->poll_index++;
    if (state->poll_index >=
        (uint8_t) (sizeof(query_order) / sizeof(query_order[0]))) {
        state->poll_index = 1U;
    }
    return query;
}

static void ZdtStepper_ServiceShutdown(zdt_stepper_axis_t axis)
{
    zdt_protocol_frame_t frame;

    if ((s_state[axis].shutdown_state ==
            (uint8_t) ZDT_SHUTDOWN_STOP_PENDING) &&
        (s_state[axis].pending_valid == 0U) &&
        ZdtProtocol_BuildStop(
            g_zdt_stepper_config[axis].address, false, &frame)) {
        s_state[axis].pending_frame = frame;
        s_state[axis].pending_valid = 1U;
        s_state[axis].shutdown_state =
            (uint8_t) ZDT_SHUTDOWN_DISABLE_PENDING;
    } else if ((s_state[axis].shutdown_state ==
                   (uint8_t) ZDT_SHUTDOWN_DISABLE_PENDING) &&
               (s_state[axis].pending_valid == 0U) &&
               ZdtProtocol_BuildEnable(
                   g_zdt_stepper_config[axis].address,
                   false, false, &frame)) {
        s_state[axis].pending_frame = frame;
        s_state[axis].pending_valid = 1U;
        s_state[axis].shutdown_state =
            (uint8_t) ZDT_SHUTDOWN_COMPLETE;
    }
}

static void ZdtStepper_ServiceAxis(zdt_stepper_axis_t axis,
    uint32_t now_us)
{
    zdt_stepper_state_t *state = &s_state[axis];
    zdt_protocol_frame_t query_frame;
    zdt_query_t query;

    ZdtStepper_ConsumeRx(axis, now_us);

    if ((state->expected_query != 0U) &&
        ZdtStepper_TimeReached(now_us, state->response_deadline_us)) {
        state->expected_query = 0U;
        state->consecutive_timeouts++;
        g_zdt_stepper_diag.axis[axis].response_timeout_count++;
        if (state->consecutive_timeouts >= ZDT_STEPPER_OFFLINE_TIMEOUTS) {
            g_zdt_stepper_diag.axis[axis].online = 0U;
        }
    }

    if (g_zdt_stepper_diag.shutdown_pending != 0U) {
        ZdtStepper_ServiceShutdown(axis);
    } else if (g_zdt_stepper_diag.backend_selected == 0U) {
        return;
    } else {
        ZdtStepper_ServiceSpeedLease(axis, now_us);
    }

    if (!BSP_ZdtUart_IsTxIdle(ZdtStepper_Port(axis)) ||
        !ZdtStepper_TimeReached(now_us,
            state->last_tx_us + ZDT_STEPPER_MIN_TX_INTERVAL_US)) {
        return;
    }

    if (state->pending_valid != 0U) {
        if (state->expected_query != 0U) {
            return;
        }
        if (ZdtStepper_SendFrame(
                axis, &state->pending_frame, now_us, false)) {
            state->pending_valid = 0U;
            g_zdt_stepper_diag.axis[axis].pending_command = 0U;
        }
        return;
    }

    if ((state->shutdown_state == (uint8_t) ZDT_SHUTDOWN_COMPLETE) ||
        (g_zdt_stepper_diag.shutdown_pending != 0U) ||
        (state->expected_query != 0U) ||
        !ZdtStepper_TimeReached(now_us, state->next_poll_us)) {
        return;
    }

    query = ZdtStepper_NextQuery(state);
    if (ZdtProtocol_BuildQuery(
            g_zdt_stepper_config[axis].address, query, &query_frame) &&
        ZdtStepper_SendFrame(axis, &query_frame, now_us, true)) {
        state->expected_query = (uint8_t) query;
        state->response_deadline_us =
            now_us + ZDT_STEPPER_RESPONSE_TIMEOUT_US;
        state->next_poll_us = now_us + ZDT_STEPPER_POLL_INTERVAL_US;
    }
}

void ZdtStepper_Init(void)
{
    uint32_t axis;

    memset(s_state, 0, sizeof(s_state));
    memset((void *) &g_zdt_stepper_diag, 0,
        sizeof(g_zdt_stepper_diag));
    BSP_ZdtUart_Init();

    for (axis = 0U; axis < ZDT_STEPPER_AXIS_COUNT; axis++) {
        g_zdt_stepper_diag.axis[axis].initialized = 1U;
    }
    g_zdt_stepper_diag.initialized = 1U;
}

void ZdtStepper_Service(uint32_t now_us)
{
    uint32_t axis;
    bool shutdown_complete = true;

    for (axis = 0U; axis < ZDT_STEPPER_AXIS_COUNT; axis++) {
        ZdtStepper_ServiceAxis((zdt_stepper_axis_t) axis, now_us);
        if ((s_state[axis].shutdown_state !=
                (uint8_t) ZDT_SHUTDOWN_COMPLETE) ||
            (s_state[axis].pending_valid != 0U) ||
            !BSP_ZdtUart_IsTxIdle((bsp_zdt_uart_port_t) axis)) {
            shutdown_complete = false;
        }
    }

    if ((g_zdt_stepper_diag.shutdown_pending != 0U) &&
        shutdown_complete) {
        g_zdt_stepper_diag.shutdown_pending = 0U;
        g_zdt_stepper_diag.backend_selected = 0U;
    }
}

bool ZdtStepper_SelectBackupBackend(void)
{
    uint32_t axis;

    if ((g_zdt_stepper_diag.initialized == 0U) ||
        (g_zdt_stepper_diag.shutdown_pending != 0U)) {
        return false;
    }

    for (axis = 0U; axis < ZDT_STEPPER_AXIS_COUNT; axis++) {
        s_state[axis].shutdown_state = (uint8_t) ZDT_SHUTDOWN_NONE;
        s_state[axis].next_poll_us = 0U;
    }
    g_zdt_stepper_diag.backend_selected = 1U;
    g_zdt_stepper_diag.select_count++;
    return true;
}

void ZdtStepper_DeselectBackupBackend(void)
{
    uint32_t axis;

    if ((g_zdt_stepper_diag.backend_selected == 0U) ||
        (g_zdt_stepper_diag.shutdown_pending != 0U)) {
        return;
    }

    for (axis = 0U; axis < ZDT_STEPPER_AXIS_COUNT; axis++) {
        s_state[axis].pending_valid = 0U;
        s_state[axis].expected_query = 0U;
        s_state[axis].speed_lease_active = 0U;
        s_state[axis].shutdown_state =
            (uint8_t) ZDT_SHUTDOWN_STOP_PENDING;
        g_zdt_stepper_diag.axis[axis].pending_command = 0U;
    }
    g_zdt_stepper_diag.shutdown_pending = 1U;
    g_zdt_stepper_diag.deselect_count++;
}

zdt_stepper_request_status_t ZdtStepper_RequestEnable(
    zdt_stepper_axis_t axis, bool enable)
{
    zdt_protocol_frame_t frame;

    if (!ZdtStepper_AxisIsValid(axis) ||
        !ZdtProtocol_BuildEnable(
            g_zdt_stepper_config[axis].address,
            enable, false, &frame)) {
        return ZDT_STEPPER_REQUEST_INVALID;
    }
    if ((g_zdt_stepper_diag.backend_selected == 0U) ||
        (g_zdt_stepper_diag.shutdown_pending != 0U)) {
        g_zdt_stepper_diag.axis[axis].rejected_request_count++;
        return ZDT_STEPPER_REQUEST_DISABLED;
    }
    return ZdtStepper_StageFrame(axis, &frame, ZDT_MOTION_NONE);
}

zdt_stepper_request_status_t ZdtStepper_RequestSpeed(
    zdt_stepper_axis_t axis, int16_t speed_rpm,
    uint32_t acceleration_rpm_s)
{
    zdt_protocol_frame_t frame;

    if (speed_rpm == 0) {
        return ZdtStepper_RequestStop(axis);
    }
    if (!ZdtStepper_AxisIsValid(axis) ||
        !ZdtProtocol_BuildSpeed(
            g_zdt_stepper_config[axis].address,
            speed_rpm, acceleration_rpm_s, false, &frame)) {
        return ZDT_STEPPER_REQUEST_INVALID;
    }
    if ((g_zdt_stepper_diag.backend_selected == 0U) ||
        (g_zdt_stepper_diag.shutdown_pending != 0U)) {
        g_zdt_stepper_diag.axis[axis].rejected_request_count++;
        return ZDT_STEPPER_REQUEST_DISABLED;
    }
    return ZdtStepper_StageFrame(axis, &frame, ZDT_MOTION_SPEED);
}

zdt_stepper_request_status_t ZdtStepper_RequestPosition(
    zdt_stepper_axis_t axis, int32_t position_millidegrees,
    uint16_t speed_rpm, uint32_t acceleration_rpm_s,
    zdt_position_mode_t mode)
{
    zdt_protocol_frame_t frame;
    int64_t scaled_pulses;

    if (!ZdtStepper_AxisIsValid(axis) ||
        ((mode == ZDT_POSITION_RELATIVE_TO_CURRENT) &&
         (g_zdt_stepper_config[axis].generation ==
          ZDT_STEPPER_GENERATION_1))) {
        return ZDT_STEPPER_REQUEST_INVALID;
    }
    if ((g_zdt_stepper_diag.backend_selected == 0U) ||
        (g_zdt_stepper_diag.shutdown_pending != 0U)) {
        g_zdt_stepper_diag.axis[axis].rejected_request_count++;
        return ZDT_STEPPER_REQUEST_DISABLED;
    }
    if ((g_zdt_stepper_diag.axis[axis].motion_active != 0U) &&
        (s_state[axis].motion_type == (uint8_t) ZDT_MOTION_POSITION) &&
        (g_zdt_stepper_config[axis].allow_position_interrupt == 0U)) {
        g_zdt_stepper_diag.axis[axis].rejected_request_count++;
        return ZDT_STEPPER_REQUEST_BUSY;
    }

    scaled_pulses = (int64_t) position_millidegrees *
        (int64_t) g_zdt_stepper_config[axis].pulses_per_revolution;
    if (scaled_pulses >= 0) {
        scaled_pulses = (scaled_pulses + 180000LL) / 360000LL;
    } else {
        scaled_pulses = (scaled_pulses - 180000LL) / 360000LL;
    }
    if ((scaled_pulses > INT32_MAX) || (scaled_pulses < INT32_MIN) ||
        !ZdtProtocol_BuildPosition(
            g_zdt_stepper_config[axis].address,
            (int32_t) scaled_pulses, speed_rpm,
            acceleration_rpm_s, mode, false, &frame)) {
        return ZDT_STEPPER_REQUEST_INVALID;
    }
    return ZdtStepper_StageFrame(axis, &frame, ZDT_MOTION_POSITION);
}

zdt_stepper_request_status_t ZdtStepper_RequestStop(
    zdt_stepper_axis_t axis)
{
    zdt_protocol_frame_t frame;

    if (!ZdtStepper_AxisIsValid(axis) ||
        !ZdtProtocol_BuildStop(
            g_zdt_stepper_config[axis].address, false, &frame)) {
        return ZDT_STEPPER_REQUEST_INVALID;
    }
    if ((g_zdt_stepper_diag.backend_selected == 0U) ||
        (g_zdt_stepper_diag.shutdown_pending != 0U)) {
        g_zdt_stepper_diag.axis[axis].rejected_request_count++;
        return ZDT_STEPPER_REQUEST_DISABLED;
    }
    s_state[axis].pending_valid = 0U;
    g_zdt_stepper_diag.axis[axis].pending_command = 0U;
    return ZdtStepper_StageFrame(axis, &frame, ZDT_MOTION_NONE);
}
