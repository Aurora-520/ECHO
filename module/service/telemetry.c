#include "telemetry.h"

#include <stddef.h>
#include <string.h>

#include "bsp_time.h"
#include "queue.h"
#include "serial_tx.h"

#define TELEMETRY_QUEUE_LENGTH 8U
#define TELEMETRY_SYNC_0       0xA5U
#define TELEMETRY_SYNC_1       0x5AU
#define TELEMETRY_CRC_INIT     0xFFFFU
#define TELEMETRY_CRC_POLY     0x1021U

#define FRAME_OFFSET_SYNC_0      0U
#define FRAME_OFFSET_SYNC_1      1U
#define FRAME_OFFSET_VERSION     2U
#define FRAME_OFFSET_TYPE        3U
#define FRAME_OFFSET_PAYLOAD_LEN 4U
#define FRAME_OFFSET_SEQUENCE    6U
#define FRAME_OFFSET_TIMESTAMP   10U
#define FRAME_OFFSET_PAYLOAD     14U

typedef enum {
    TELEMETRY_MESSAGE_CONTROL = 1U,
    TELEMETRY_MESSAGE_PARAMETER_ACK = 2U,
    TELEMETRY_MESSAGE_HEALTH = 3U,
    TELEMETRY_MESSAGE_ACTUATOR_ACK = 4U,
    TELEMETRY_MESSAGE_MOTOR_PROFILE = 5U,
    TELEMETRY_MESSAGE_ZDT_ACK = 6U
} telemetry_message_kind_t;

typedef struct {
    uint16_t schema_version;
    uint16_t build_phase;
    uint32_t snapshot_sequence;
    uint32_t uptime_ticks;
    uint32_t active_issue_mask;
    uint32_t sticky_issue_mask;
    uint32_t system_period_us;
    uint32_t system_execution_us;
    uint32_t deadline_miss_count;
    uint32_t telemetry_publish_drop_count;
    uint32_t telemetry_transport_drop_count;
    uint32_t serial_tx_drop_count;
    uint32_t serial_rx_overflow_count;
    uint32_t i2c_error_count;
    uint32_t parameter_error_count;
    uint32_t heap_min_ever_free_bytes;
    uint32_t parameter_apply_sequence;
    uint16_t minimum_stack_free_words;
    uint8_t level;
    uint8_t active_issue;
    uint8_t first_fault_issue;
    uint8_t first_fault_valid;
    uint8_t oled_online;
    uint8_t actuator_output_permitted;
    uint8_t parameter_pending;
    uint8_t parameter_last_status;
    uint8_t reset_reason;
    uint8_t reset_reason_valid;
    uint32_t i2c_success_count;
    uint32_t quiet_acquired_count;
    uint32_t quiet_released_count;
    uint32_t max_quiet_window_us;
    uint32_t display_refresh_count;
    uint16_t system_stack_free_words;
    uint16_t service_stack_free_words;
    uint16_t telemetry_stack_free_words;
    uint16_t display_stack_free_words;
    uint16_t idle_stack_free_words;
    uint16_t timer_stack_free_words;
    uint16_t serial_ring_high_water_bytes;
    uint8_t quiet_window_active;
    uint8_t reserved;
    uint32_t encoder_isr_late_count;
} telemetry_health_sample_t;

typedef struct {
    uint16_t schema_version;
    uint16_t profile_id;
    uint16_t profile_version;
    uint16_t status_flags;
    uint32_t valid_fields;
    uint32_t rated_voltage_mv;
    uint32_t encoder_ppr;
    uint32_t left_counts_per_revolution;
    uint32_t right_counts_per_revolution;
    int8_t left_motor_output_sign;
    int8_t right_motor_output_sign;
    int8_t left_encoder_count_sign;
    int8_t right_encoder_count_sign;
    uint8_t left_decode_multiplier;
    uint8_t right_decode_multiplier;
    uint8_t actuator_test_ready;
    uint8_t output_locked;
} telemetry_motor_profile_sample_t;

typedef struct {
    uint8_t kind;
    uint8_t reserved[3];
    uint32_t sequence;
    uint32_t timestamp_us;
    union {
        telemetry_control_sample_t control;
        telemetry_parameter_ack_t parameter_ack;
        telemetry_actuator_ack_t actuator_ack;
        telemetry_zdt_ack_t zdt_ack;
        telemetry_motor_profile_sample_t motor_profile;
        telemetry_health_sample_t health;
    } data;
} telemetry_message_t;

volatile telemetry_diagnostics_t g_telemetry_diag;

static StaticQueue_t s_queue_control;
static uint8_t
    s_queue_storage[TELEMETRY_QUEUE_LENGTH * sizeof(telemetry_message_t)];
static QueueHandle_t s_queue;

static StaticTask_t s_task_control;
static StackType_t s_task_stack[TELEMETRY_TASK_STACK_WORDS];
static uint32_t s_outgoing_sequence;

static void Telemetry_PutU16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t) value;
    destination[1] = (uint8_t) (value >> 8);
}

static void Telemetry_PutU32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t) value;
    destination[1] = (uint8_t) (value >> 8);
    destination[2] = (uint8_t) (value >> 16);
    destination[3] = (uint8_t) (value >> 24);
}

static void Telemetry_PutFloat(uint8_t *destination, float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    Telemetry_PutU32(destination, bits);
}

static uint16_t Telemetry_Crc16(
    const uint8_t *data, uint16_t length)
{
    uint16_t crc = TELEMETRY_CRC_INIT;
    uint16_t index;

    for (index = 0U; index < length; index++) {
        uint8_t bit;

        crc ^= (uint16_t) data[index] << 8;
        for (bit = 0U; bit < 8U; bit++) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t) ((crc << 1) ^ TELEMETRY_CRC_POLY);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static uint16_t Telemetry_BeginFrame(uint8_t *frame,
    uint8_t type, uint16_t payload_length,
    uint32_t sequence, uint32_t timestamp_us)
{
    frame[FRAME_OFFSET_SYNC_0] = TELEMETRY_SYNC_0;
    frame[FRAME_OFFSET_SYNC_1] = TELEMETRY_SYNC_1;
    frame[FRAME_OFFSET_VERSION] = TELEMETRY_PROTOCOL_VERSION;
    frame[FRAME_OFFSET_TYPE] = type;
    Telemetry_PutU16(&frame[FRAME_OFFSET_PAYLOAD_LEN], payload_length);
    Telemetry_PutU32(&frame[FRAME_OFFSET_SEQUENCE], sequence);
    Telemetry_PutU32(&frame[FRAME_OFFSET_TIMESTAMP], timestamp_us);
    return (uint16_t) (FRAME_OFFSET_PAYLOAD + payload_length);
}

static uint16_t Telemetry_EndFrame(uint8_t *frame,
    uint16_t payload_length)
{
    uint16_t crc = Telemetry_Crc16(&frame[FRAME_OFFSET_VERSION],
        (uint16_t) (12U + payload_length));
    Telemetry_PutU16(
        &frame[FRAME_OFFSET_PAYLOAD + payload_length], crc);
    return (uint16_t) (16U + payload_length);
}

static uint16_t Telemetry_EncodeControl(
    const telemetry_message_t *message, uint8_t *frame)
{
    const telemetry_control_sample_t *sample = &message->data.control;
    uint8_t *payload = &frame[FRAME_OFFSET_PAYLOAD];
    uint16_t payload_length = TELEMETRY_CONTROL_PAYLOAD_BYTES;

    (void) Telemetry_BeginFrame(frame, TELEMETRY_FRAME_TYPE_CONTROL,
        payload_length, message->sequence, message->timestamp_us);
    Telemetry_PutFloat(&payload[0], sample->setpoint);
    Telemetry_PutFloat(&payload[4], sample->measurement);
    Telemetry_PutFloat(&payload[8], sample->control_output);
    Telemetry_PutFloat(&payload[12], sample->auxiliary);
    Telemetry_PutU32(&payload[16], sample->loop_count);
    Telemetry_PutU32(&payload[20], sample->period_us);
    Telemetry_PutU32(&payload[24], sample->execution_us);
    Telemetry_PutU32(&payload[28], sample->jitter_us);
    Telemetry_PutU32(&payload[32], sample->deadline_miss_count);
    Telemetry_PutU32(&payload[36], sample->flags);
    return Telemetry_EndFrame(frame, payload_length);
}

static uint16_t Telemetry_EncodeParameterAck(
    const telemetry_message_t *message, uint8_t *frame)
{
    const telemetry_parameter_ack_t *ack =
        &message->data.parameter_ack;
    uint8_t *payload = &frame[FRAME_OFFSET_PAYLOAD];
    uint16_t payload_length = TELEMETRY_PARAMETER_ACK_PAYLOAD_BYTES;

    (void) Telemetry_BeginFrame(frame,
        TELEMETRY_FRAME_TYPE_PARAMETER_ACK, payload_length,
        message->sequence, message->timestamp_us);
    Telemetry_PutU32(&payload[0], ack->transaction_id);
    Telemetry_PutU16(&payload[4], ack->parameter_id);
    payload[6] = ack->status;
    payload[7] = ack->reserved;
    Telemetry_PutFloat(&payload[8], ack->applied_value);
    Telemetry_PutU32(&payload[12], ack->apply_sequence);
    return Telemetry_EndFrame(frame, payload_length);
}

static uint16_t Telemetry_EncodeActuatorAck(
    const telemetry_message_t *message, uint8_t *frame)
{
    const telemetry_actuator_ack_t *ack =
        &message->data.actuator_ack;
    uint8_t *payload = &frame[FRAME_OFFSET_PAYLOAD];
    uint16_t payload_length = TELEMETRY_ACTUATOR_ACK_PAYLOAD_BYTES;

    (void) Telemetry_BeginFrame(frame,
        TELEMETRY_FRAME_TYPE_ACTUATOR_ACK, payload_length,
        message->sequence, message->timestamp_us);
    Telemetry_PutU32(&payload[0], ack->sequence);
    Telemetry_PutU16(&payload[4],
        (uint16_t) ack->left_electrical_permille);
    Telemetry_PutU16(&payload[6],
        (uint16_t) ack->right_electrical_permille);
    Telemetry_PutU16(&payload[8], ack->duration_ms);
    payload[10] = ack->status;
    payload[11] = ack->reserved;
    Telemetry_PutU32(&payload[12], ack->accepted_request_count);
    return Telemetry_EndFrame(frame, payload_length);
}

static uint16_t Telemetry_EncodeZdtAck(
    const telemetry_message_t *message, uint8_t *frame)
{
    const telemetry_zdt_ack_t *ack = &message->data.zdt_ack;
    uint8_t *payload = &frame[FRAME_OFFSET_PAYLOAD];
    uint16_t payload_length = TELEMETRY_ZDT_ACK_PAYLOAD_BYTES;

    (void) Telemetry_BeginFrame(frame,
        TELEMETRY_FRAME_TYPE_ZDT_ACK, payload_length,
        message->sequence, message->timestamp_us);
    Telemetry_PutU32(&payload[0], ack->sequence);
    Telemetry_PutU32(&payload[4], (uint32_t) ack->value);
    Telemetry_PutU32(&payload[8], ack->gen1_response_count);
    Telemetry_PutU32(&payload[12], ack->gen2_response_count);
    Telemetry_PutU16(&payload[16], ack->gen1_timeout_count);
    Telemetry_PutU16(&payload[18], ack->gen2_timeout_count);
    payload[20] = ack->operation;
    payload[21] = ack->axis;
    payload[22] = ack->status;
    payload[23] = ack->flags;
    for (uint32_t axis = 0U; axis < 2U; axis++) {
        const telemetry_zdt_axis_snapshot_t *snapshot =
            &ack->axis_snapshot[axis];
        uint8_t *axis_payload = &payload[24U +
            (axis * TELEMETRY_ZDT_AXIS_SNAPSHOT_BYTES)];

        Telemetry_PutU32(&axis_payload[0], snapshot->tx_command_count);
        Telemetry_PutU32(&axis_payload[4], snapshot->tx_query_count);
        Telemetry_PutU32(&axis_payload[8], snapshot->invalid_response_count);
        Telemetry_PutU32(&axis_payload[12], snapshot->position_reached_count);
        Telemetry_PutU32(&axis_payload[16],
            snapshot->speed_lease_expired_count);
        Telemetry_PutU32(&axis_payload[20],
            (uint32_t) snapshot->position_counts);
        Telemetry_PutU32(&axis_payload[24],
            (uint32_t) snapshot->position_millidegrees);
        Telemetry_PutU16(&axis_payload[28],
            (uint16_t) snapshot->speed_rpm);
        Telemetry_PutU16(&axis_payload[30], snapshot->firmware_version);
        Telemetry_PutU16(&axis_payload[32], snapshot->hardware_version);
        axis_payload[34] = snapshot->motor_status_flags;
        axis_payload[35] = snapshot->last_function;
        axis_payload[36] = snapshot->last_reply_status;
        axis_payload[37] = snapshot->stalled;
        axis_payload[38] = snapshot->stall_protected;
        axis_payload[39] = snapshot->reserved;
    }
    return Telemetry_EndFrame(frame, payload_length);
}

static uint16_t Telemetry_EncodeMotorProfile(
    const telemetry_message_t *message, uint8_t *frame)
{
    const telemetry_motor_profile_sample_t *profile =
        &message->data.motor_profile;
    uint8_t *payload = &frame[FRAME_OFFSET_PAYLOAD];
    uint16_t payload_length = TELEMETRY_MOTOR_PROFILE_PAYLOAD_BYTES;

    (void) Telemetry_BeginFrame(frame,
        TELEMETRY_FRAME_TYPE_MOTOR_PROFILE, payload_length,
        message->sequence, message->timestamp_us);
    Telemetry_PutU16(&payload[0], profile->schema_version);
    Telemetry_PutU16(&payload[2], profile->profile_id);
    Telemetry_PutU16(&payload[4], profile->profile_version);
    Telemetry_PutU16(&payload[6], profile->status_flags);
    Telemetry_PutU32(&payload[8], profile->valid_fields);
    Telemetry_PutU32(&payload[12], profile->rated_voltage_mv);
    Telemetry_PutU32(&payload[16], profile->encoder_ppr);
    Telemetry_PutU32(&payload[20],
        profile->left_counts_per_revolution);
    Telemetry_PutU32(&payload[24],
        profile->right_counts_per_revolution);
    payload[28] = (uint8_t) profile->left_motor_output_sign;
    payload[29] = (uint8_t) profile->right_motor_output_sign;
    payload[30] = (uint8_t) profile->left_encoder_count_sign;
    payload[31] = (uint8_t) profile->right_encoder_count_sign;
    payload[32] = profile->left_decode_multiplier;
    payload[33] = profile->right_decode_multiplier;
    payload[34] = profile->actuator_test_ready;
    payload[35] = profile->output_locked;
    return Telemetry_EndFrame(frame, payload_length);
}

static uint16_t Telemetry_EncodeHealth(
    const telemetry_message_t *message, uint8_t *frame)
{
    const telemetry_health_sample_t *health = &message->data.health;
    uint8_t *payload = &frame[FRAME_OFFSET_PAYLOAD];
    uint16_t payload_length = TELEMETRY_HEALTH_PAYLOAD_BYTES;

    (void) Telemetry_BeginFrame(frame, TELEMETRY_FRAME_TYPE_HEALTH,
        payload_length, message->sequence, message->timestamp_us);
    Telemetry_PutU16(&payload[0], health->schema_version);
    Telemetry_PutU16(&payload[2], health->build_phase);
    Telemetry_PutU32(&payload[4], health->snapshot_sequence);
    Telemetry_PutU32(&payload[8], health->uptime_ticks);
    Telemetry_PutU32(&payload[12], health->active_issue_mask);
    Telemetry_PutU32(&payload[16], health->sticky_issue_mask);
    Telemetry_PutU32(&payload[20], health->system_period_us);
    Telemetry_PutU32(&payload[24], health->system_execution_us);
    Telemetry_PutU32(&payload[28], health->deadline_miss_count);
    Telemetry_PutU32(&payload[32],
        health->telemetry_publish_drop_count);
    Telemetry_PutU32(&payload[36],
        health->telemetry_transport_drop_count);
    Telemetry_PutU32(&payload[40], health->serial_tx_drop_count);
    Telemetry_PutU32(&payload[44], health->serial_rx_overflow_count);
    Telemetry_PutU32(&payload[48], health->i2c_error_count);
    Telemetry_PutU32(&payload[52], health->parameter_error_count);
    Telemetry_PutU32(&payload[56], health->heap_min_ever_free_bytes);
    Telemetry_PutU32(&payload[60], health->parameter_apply_sequence);
    Telemetry_PutU16(&payload[64], health->minimum_stack_free_words);
    payload[66] = health->level;
    payload[67] = health->active_issue;
    payload[68] = health->first_fault_issue;
    payload[69] = health->first_fault_valid;
    payload[70] = health->oled_online;
    payload[71] = health->actuator_output_permitted;
    payload[72] = health->parameter_pending;
    payload[73] = health->parameter_last_status;
    payload[74] = health->reset_reason;
    payload[75] = health->reset_reason_valid;
    Telemetry_PutU32(&payload[76], health->i2c_success_count);
    Telemetry_PutU32(&payload[80], health->quiet_acquired_count);
    Telemetry_PutU32(&payload[84], health->quiet_released_count);
    Telemetry_PutU32(&payload[88], health->max_quiet_window_us);
    Telemetry_PutU32(&payload[92], health->display_refresh_count);
    Telemetry_PutU16(&payload[96], health->system_stack_free_words);
    Telemetry_PutU16(&payload[98], health->service_stack_free_words);
    Telemetry_PutU16(&payload[100], health->telemetry_stack_free_words);
    Telemetry_PutU16(&payload[102], health->display_stack_free_words);
    Telemetry_PutU16(&payload[104], health->idle_stack_free_words);
    Telemetry_PutU16(&payload[106], health->timer_stack_free_words);
    Telemetry_PutU16(&payload[108],
        health->serial_ring_high_water_bytes);
    payload[110] = health->quiet_window_active;
    payload[111] = 0U;
    Telemetry_PutU32(&payload[112], health->encoder_isr_late_count);
    return Telemetry_EndFrame(frame, payload_length);
}

static void Telemetry_Task(void *context)
{
    telemetry_message_t message;
    uint8_t frame[TELEMETRY_MAX_FRAME_BYTES];
    uint16_t frame_length;
    uint16_t crc_offset;

    (void) context;

    for (;;) {
        if (xQueueReceive(s_queue, &message, portMAX_DELAY) != pdPASS) {
            continue;
        }

        g_telemetry_diag.task_run_count++;
        g_telemetry_diag.last_task_wake_tick = xTaskGetTickCount();
        message.sequence = s_outgoing_sequence;
        s_outgoing_sequence++;
        if (message.kind == TELEMETRY_MESSAGE_CONTROL) {
            frame_length = Telemetry_EncodeControl(&message, frame);
            g_telemetry_diag.last_frame_type =
                TELEMETRY_FRAME_TYPE_CONTROL;
        } else if (message.kind == TELEMETRY_MESSAGE_PARAMETER_ACK) {
            frame_length = Telemetry_EncodeParameterAck(&message, frame);
            g_telemetry_diag.last_frame_type =
                TELEMETRY_FRAME_TYPE_PARAMETER_ACK;
        } else if (message.kind == TELEMETRY_MESSAGE_HEALTH) {
            frame_length = Telemetry_EncodeHealth(&message, frame);
            g_telemetry_diag.last_frame_type =
                TELEMETRY_FRAME_TYPE_HEALTH;
        } else if (message.kind == TELEMETRY_MESSAGE_ACTUATOR_ACK) {
            frame_length = Telemetry_EncodeActuatorAck(&message, frame);
            g_telemetry_diag.last_frame_type =
                TELEMETRY_FRAME_TYPE_ACTUATOR_ACK;
        } else if (message.kind == TELEMETRY_MESSAGE_MOTOR_PROFILE) {
            frame_length = Telemetry_EncodeMotorProfile(&message, frame);
            g_telemetry_diag.last_frame_type =
                TELEMETRY_FRAME_TYPE_MOTOR_PROFILE;
        } else {
            frame_length = Telemetry_EncodeZdtAck(&message, frame);
            g_telemetry_diag.last_frame_type = TELEMETRY_FRAME_TYPE_ZDT_ACK;
        }

        crc_offset = (uint16_t) (frame_length - 2U);
        g_telemetry_diag.last_crc =
            (uint16_t) frame[crc_offset] |
            (uint16_t) ((uint16_t) frame[crc_offset + 1U] << 8);
        g_telemetry_diag.frames_encoded_count++;
        g_telemetry_diag.last_sequence = message.sequence;
        g_telemetry_diag.last_timestamp_us = message.timestamp_us;
        g_telemetry_diag.last_frame_length = frame_length;

        if (SerialTx_TryWrite(frame, frame_length)) {
            g_telemetry_diag.frames_queued_count++;
        } else {
            g_telemetry_diag.transport_dropped_count++;
        }
    }
}

TaskHandle_t Telemetry_CreateTask(UBaseType_t priority)
{
    if (g_telemetry_diag.initialized != 0U) {
        return g_telemetry_diag.task_handle;
    }

    s_outgoing_sequence = 0U;
    s_queue = xQueueCreateStatic(TELEMETRY_QUEUE_LENGTH,
        sizeof(telemetry_message_t), s_queue_storage, &s_queue_control);
    if (s_queue == NULL) {
        return NULL;
    }
    vQueueAddToRegistry(s_queue, "TelemetryQ");

    g_telemetry_diag.task_handle = xTaskCreateStatic(Telemetry_Task,
        "Telemetry", TELEMETRY_TASK_STACK_WORDS, NULL, priority,
        s_task_stack, &s_task_control);
    if (g_telemetry_diag.task_handle == NULL) {
        return NULL;
    }

    g_telemetry_diag.initialized = 1U;
    return g_telemetry_diag.task_handle;
}

static bool Telemetry_EnqueueMessage(telemetry_message_t *message)
{
    UBaseType_t queue_depth;

    if (s_queue == NULL) {
        return false;
    }

    message->timestamp_us = BSP_Time_GetUs();

    if (xQueueSend(s_queue, message, 0U) != pdPASS) {
        return false;
    }

    queue_depth = uxQueueMessagesWaiting(s_queue);
    if (queue_depth > g_telemetry_diag.queue_high_water) {
        g_telemetry_diag.queue_high_water = queue_depth;
    }
    return true;
}

bool Telemetry_PublishControl(
    const telemetry_control_sample_t *sample)
{
    telemetry_message_t message;
    bool accepted;

    g_telemetry_diag.publish_attempt_count++;
    if ((sample == NULL) || (s_queue == NULL)) {
        g_telemetry_diag.publish_dropped_count++;
        return false;
    }

    message.kind = TELEMETRY_MESSAGE_CONTROL;
    message.data.control = *sample;
    accepted = Telemetry_EnqueueMessage(&message);
    if (accepted) {
        g_telemetry_diag.publish_accepted_count++;
    } else {
        g_telemetry_diag.publish_dropped_count++;
    }
    return accepted;
}

bool Telemetry_PublishParameterAck(
    const telemetry_parameter_ack_t *ack)
{
    telemetry_message_t message;
    bool accepted;

    g_telemetry_diag.ack_attempt_count++;
    if ((ack == NULL) || (s_queue == NULL)) {
        g_telemetry_diag.ack_dropped_count++;
        return false;
    }

    message.kind = TELEMETRY_MESSAGE_PARAMETER_ACK;
    message.data.parameter_ack = *ack;
    accepted = Telemetry_EnqueueMessage(&message);
    if (accepted) {
        g_telemetry_diag.ack_accepted_count++;
    } else {
        g_telemetry_diag.ack_dropped_count++;
    }
    return accepted;
}

bool Telemetry_PublishActuatorAck(const telemetry_actuator_ack_t *ack)
{
    telemetry_message_t message;
    bool accepted;

    g_telemetry_diag.actuator_ack_attempt_count++;
    if ((ack == NULL) || (s_queue == NULL)) {
        g_telemetry_diag.actuator_ack_dropped_count++;
        return false;
    }

    message.kind = TELEMETRY_MESSAGE_ACTUATOR_ACK;
    message.data.actuator_ack = *ack;
    accepted = Telemetry_EnqueueMessage(&message);
    if (accepted) {
        g_telemetry_diag.actuator_ack_accepted_count++;
    } else {
        g_telemetry_diag.actuator_ack_dropped_count++;
    }
    return accepted;
}

bool Telemetry_PublishZdtAck(const telemetry_zdt_ack_t *ack)
{
    telemetry_message_t message;
    bool accepted;

    g_telemetry_diag.zdt_ack_attempt_count++;
    if ((ack == NULL) || (s_queue == NULL)) {
        g_telemetry_diag.zdt_ack_dropped_count++;
        return false;
    }

    message.kind = TELEMETRY_MESSAGE_ZDT_ACK;
    message.data.zdt_ack = *ack;
    accepted = Telemetry_EnqueueMessage(&message);
    if (accepted) {
        g_telemetry_diag.zdt_ack_accepted_count++;
    } else {
        g_telemetry_diag.zdt_ack_dropped_count++;
    }
    return accepted;
}

bool Telemetry_PublishMotorProfile(const motor_profile_t *profile)
{
    telemetry_message_t message;
    telemetry_motor_profile_sample_t *sample =
        &message.data.motor_profile;
    const motor_wheel_profile_t *left;
    const motor_wheel_profile_t *right;
    bool accepted;

    g_telemetry_diag.motor_profile_attempt_count++;
    if ((profile == NULL) || (s_queue == NULL)) {
        g_telemetry_diag.motor_profile_dropped_count++;
        return false;
    }
    left = &profile->wheel[MOTOR_WHEEL_LEFT];
    right = &profile->wheel[MOTOR_WHEEL_RIGHT];
    sample->schema_version = profile->schema_version;
    sample->profile_id = profile->profile_id;
    sample->profile_version = profile->profile_version;
    sample->status_flags = (uint16_t) profile->status_flags;
    sample->valid_fields = profile->valid_fields;
    sample->rated_voltage_mv = profile->rated_voltage_mv;
    sample->encoder_ppr = profile->encoder_ppr;
    sample->left_counts_per_revolution =
        left->counts_per_output_revolution;
    sample->right_counts_per_revolution =
        right->counts_per_output_revolution;
    sample->left_motor_output_sign = left->motor_output_sign;
    sample->right_motor_output_sign = right->motor_output_sign;
    sample->left_encoder_count_sign = left->encoder_count_sign;
    sample->right_encoder_count_sign = right->encoder_count_sign;
    sample->left_decode_multiplier =
        left->encoder_decode_multiplier;
    sample->right_decode_multiplier =
        right->encoder_decode_multiplier;
    sample->actuator_test_ready =
        g_motor_profile_diag.actuator_test_ready;
    sample->output_locked = g_motor_profile_diag.output_locked;

    message.kind = TELEMETRY_MESSAGE_MOTOR_PROFILE;
    accepted = Telemetry_EnqueueMessage(&message);
    if (accepted) {
        g_telemetry_diag.motor_profile_accepted_count++;
    } else {
        g_telemetry_diag.motor_profile_dropped_count++;
    }
    return accepted;
}

static uint16_t Telemetry_MinimumStackWords(
    const system_health_snapshot_t *snapshot)
{
    uint16_t minimum = snapshot->system_stack_free_words;

    if (snapshot->service_stack_free_words < minimum) {
        minimum = snapshot->service_stack_free_words;
    }
    if (snapshot->telemetry_stack_free_words < minimum) {
        minimum = snapshot->telemetry_stack_free_words;
    }
    if (snapshot->display_stack_free_words < minimum) {
        minimum = snapshot->display_stack_free_words;
    }
    if (snapshot->idle_stack_free_words < minimum) {
        minimum = snapshot->idle_stack_free_words;
    }
    if (snapshot->timer_stack_free_words < minimum) {
        minimum = snapshot->timer_stack_free_words;
    }
    return minimum;
}

bool Telemetry_PublishHealth(const system_health_snapshot_t *snapshot)
{
    telemetry_message_t message;
    telemetry_health_sample_t *health = &message.data.health;
    bool accepted;

    g_telemetry_diag.health_attempt_count++;
    if ((snapshot == NULL) || (s_queue == NULL)) {
        g_telemetry_diag.health_dropped_count++;
        return false;
    }

    message.kind = TELEMETRY_MESSAGE_HEALTH;
    health->schema_version = snapshot->version;
    health->build_phase = snapshot->build_phase;
    health->snapshot_sequence = snapshot->update_sequence;
    health->uptime_ticks = snapshot->uptime_ticks;
    health->active_issue_mask = snapshot->active_issue_mask;
    health->sticky_issue_mask = snapshot->sticky_issue_mask;
    health->system_period_us = snapshot->system_period_us;
    health->system_execution_us = snapshot->system_execution_us;
    health->deadline_miss_count = snapshot->system_deadline_miss_count;
    health->telemetry_publish_drop_count =
        snapshot->telemetry_publish_drop_count;
    health->telemetry_transport_drop_count =
        snapshot->telemetry_transport_drop_count;
    health->serial_tx_drop_count = snapshot->serial_tx_drop_count;
    health->serial_rx_overflow_count = snapshot->serial_rx_overflow_count;
    health->i2c_error_count = snapshot->i2c_error_count;
    health->parameter_error_count = snapshot->parameter_error_count;
    health->heap_min_ever_free_bytes = snapshot->heap_min_ever_free_bytes;
    health->parameter_apply_sequence = snapshot->parameter_apply_sequence;
    health->minimum_stack_free_words =
        Telemetry_MinimumStackWords(snapshot);
    health->level = snapshot->level;
    health->active_issue = snapshot->active_issue;
    health->first_fault_issue = snapshot->first_fault_issue;
    health->first_fault_valid = snapshot->first_fault_valid;
    health->oled_online = snapshot->oled_online;
    health->actuator_output_permitted =
        snapshot->actuator_output_permitted;
    health->parameter_pending = snapshot->parameter_pending;
    health->parameter_last_status = snapshot->parameter_last_status;
    health->reset_reason = snapshot->reset_reason;
    health->reset_reason_valid = snapshot->reset_reason_valid;
    health->i2c_success_count = snapshot->i2c_success_count;
    health->quiet_acquired_count = snapshot->quiet_acquired_count;
    health->quiet_released_count = snapshot->quiet_released_count;
    health->max_quiet_window_us = snapshot->max_quiet_window_us;
    health->display_refresh_count = snapshot->display_refresh_count;
    health->system_stack_free_words = snapshot->system_stack_free_words;
    health->service_stack_free_words = snapshot->service_stack_free_words;
    health->telemetry_stack_free_words = snapshot->telemetry_stack_free_words;
    health->display_stack_free_words = snapshot->display_stack_free_words;
    health->idle_stack_free_words = snapshot->idle_stack_free_words;
    health->timer_stack_free_words = snapshot->timer_stack_free_words;
    health->serial_ring_high_water_bytes =
        snapshot->serial_ring_high_water_bytes;
    health->quiet_window_active = snapshot->quiet_window_active;
    health->reserved = 0U;
    health->encoder_isr_late_count = snapshot->encoder_isr_late_count;

    accepted = Telemetry_EnqueueMessage(&message);
    if (accepted) {
        g_telemetry_diag.health_accepted_count++;
    } else {
        g_telemetry_diag.health_dropped_count++;
    }
    return accepted;
}
