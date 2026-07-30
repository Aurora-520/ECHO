#ifndef ECHO_LINE_FOLLOWER_6CH_H
#define ECHO_LINE_FOLLOWER_6CH_H

#include <stdbool.h>
#include <stdint.h>

#define LINE_FOLLOWER6_I2C_ADDRESS             0x5CU
#define LINE_FOLLOWER6_CHANNEL_COUNT               6U
#define LINE_FOLLOWER6_LEGACY_CHANNEL_COUNT        8U
#define LINE_FOLLOWER6_DIGITAL_REG                 5U
#define LINE_FOLLOWER6_ANALOG_FIRST_REG            6U
#define LINE_FOLLOWER6_THRESHOLD_FIRST_REG        18U
#define LINE_FOLLOWER6_REGISTER_STRIDE             2U
#define LINE_FOLLOWER6_ANALOG_BYTES               12U
#define LINE_FOLLOWER6_THRESHOLD_BYTES            12U

/* Matches the existing 125 Hz reflectance scan cadence. */
#define LINE_FOLLOWER6_SERVICE_FRAME_PERIOD_US   8000UL
#define LINE_FOLLOWER6_OFFLINE_RETRY_PERIOD_US 500000UL

#define LINE_FOLLOWER6_CHANNEL_ORDER_UNCONFIRMED  0U
#define LINE_FOLLOWER6_CHANNEL_ORDER_1_TO_6        1U
#define LINE_FOLLOWER6_CHANNEL_ORDER_6_TO_1        2U

typedef uint8_t line_follower6_channel_order_t;

typedef enum {
    LINE_FOLLOWER6_RESULT_OK = 0,
    LINE_FOLLOWER6_RESULT_NOT_INITIALIZED,
    LINE_FOLLOWER6_RESULT_INVALID_ARGUMENT,
    LINE_FOLLOWER6_RESULT_I2C_ERROR
} line_follower6_result_t;

typedef struct {
    uint32_t scan_sequence;
    uint16_t raw[LINE_FOLLOWER6_CHANNEL_COUNT];
    uint16_t threshold[LINE_FOLLOWER6_CHANNEL_COUNT];
    int32_t margin[LINE_FOLLOWER6_CHANNEL_COUNT];
    uint16_t minimum_raw;
    uint16_t maximum_raw;
    uint8_t digital_mask;
    uint8_t digital_bit[LINE_FOLLOWER6_CHANNEL_COUNT];
    uint8_t threshold_valid;
    uint8_t reserved;
} line_follower6_sample_t;

typedef struct {
    uint16_t raw[LINE_FOLLOWER6_CHANNEL_COUNT];
    uint16_t threshold[LINE_FOLLOWER6_CHANNEL_COUNT];
    int32_t margin[LINE_FOLLOWER6_CHANNEL_COUNT];
    uint16_t minimum_raw;
    uint16_t maximum_raw;
    uint32_t sample_count;
    uint32_t success_count;
    uint32_t failure_count;
    uint32_t init_attempt_count;
    uint32_t init_success_count;
    uint32_t init_failure_count;
    uint32_t threshold_read_count;
    uint32_t threshold_failure_count;
    uint32_t reconnect_count;
    uint32_t offline_count;
    uint32_t consecutive_failure_count;
    uint32_t service_call_count;
    uint32_t service_deferred_count;
    uint32_t last_attempt_us;
    uint32_t last_success_us;
    uint32_t last_i2c_result;
    uint8_t digital_mask;
    uint8_t digital_bit[LINE_FOLLOWER6_CHANNEL_COUNT];
    uint8_t address;
    uint8_t last_register;
    uint8_t last_error;
    uint8_t service_stage;
    uint8_t threshold_valid;
    uint8_t online;
    uint8_t initialized;
    uint8_t reserved;
} line_follower6_snapshot_t;

/* Watch/debug readers must treat this as read-only. */
extern volatile line_follower6_snapshot_t g_line_follower6_snapshot;

void LineFollower6_Reset(void);
bool LineFollower6_Init(void);
bool LineFollower6_Update(void);
bool LineFollower6_ReadAll(void);
bool LineFollower6_ReadThresholds(void);

/*
 * Call from a periodic task with the wrapping 1 MHz BSP timestamp. Each call
 * performs at most one I2C transaction and returns true only for a new frame.
 */
bool LineFollower6_Service(
    uint32_t now_us, line_follower6_sample_t *sample);

bool LineFollower6_GetSnapshot(line_follower6_snapshot_t *snapshot);
void LineFollower6_MarkOffline(void);

line_follower6_result_t LineFollower6_ParseSample(
    uint8_t digital_mask, const uint8_t *analog_data,
    uint16_t analog_length, line_follower6_snapshot_t *snapshot);
line_follower6_result_t LineFollower6_ParseThresholds(
    const uint8_t *data, uint16_t length,
    line_follower6_snapshot_t *snapshot);

/*
 * Resamples six evenly spaced physical channels onto the eight positions used
 * by the existing reflectance control path. Channel order must be confirmed
 * on hardware; UNCONFIRMED is rejected.
 */
bool LineFollower6_ExpandRawToLegacy8(
    const uint16_t source[LINE_FOLLOWER6_CHANNEL_COUNT],
    line_follower6_channel_order_t order,
    uint16_t destination[LINE_FOLLOWER6_LEGACY_CHANNEL_COUNT]);

#endif
