#ifndef ECHO_LINE_FOLLOWER_6CH_H
#define ECHO_LINE_FOLLOWER_6CH_H

#include <stdbool.h>
#include <stdint.h>

#define LINE_FOLLOWER6_I2C_ADDRESS 0x5CU
#define LINE_FOLLOWER6_CHANNEL_COUNT 6U
#define LINE_FOLLOWER6_DIGITAL_REG 5U
#define LINE_FOLLOWER6_ANALOG_FIRST_REG 6U
#define LINE_FOLLOWER6_THRESHOLD_FIRST_REG 18U
#define LINE_FOLLOWER6_SENSOR_FRAME_BYTES 13U
#define LINE_FOLLOWER6_THRESHOLD_FRAME_BYTES 12U

typedef enum {
    LINE_FOLLOWER6_RESULT_OK = 0,
    LINE_FOLLOWER6_RESULT_NOT_INITIALIZED,
    LINE_FOLLOWER6_RESULT_INVALID_ARGUMENT,
    LINE_FOLLOWER6_RESULT_I2C_ERROR
} line_follower6_result_t;

typedef struct {
    uint16_t raw[LINE_FOLLOWER6_CHANNEL_COUNT];
    uint16_t threshold[LINE_FOLLOWER6_CHANNEL_COUNT];
    int32_t margin[LINE_FOLLOWER6_CHANNEL_COUNT];
    uint8_t target_detected[LINE_FOLLOWER6_CHANNEL_COUNT];
    uint8_t digital_mask;
    uint8_t threshold_valid;
    uint8_t online;
    uint8_t initialized;
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
    uint32_t last_i2c_result;
    uint8_t address;
    uint8_t last_register;
    uint8_t last_error;
    uint8_t reserved;
} line_follower6_snapshot_t;

extern volatile line_follower6_snapshot_t g_line_follower6_snapshot;

void LineFollower6_Reset(void);
bool LineFollower6_Init(void);
bool LineFollower6_Update(void);
bool LineFollower6_ReadAll(void);
bool LineFollower6_ReadThresholds(void);
bool LineFollower6_GetSnapshot(line_follower6_snapshot_t *snapshot);
void LineFollower6_MarkOffline(void);

line_follower6_result_t LineFollower6_ParseSensorFrame(
    const uint8_t *data, uint16_t length,
    line_follower6_snapshot_t *snapshot);
line_follower6_result_t LineFollower6_ParseThresholdFrame(
    const uint8_t *data, uint16_t length,
    line_follower6_snapshot_t *snapshot);

#endif
