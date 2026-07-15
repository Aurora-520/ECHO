#include "imu_service.h"

#include <math.h>
#include <string.h>

#include "bsp_time.h"
#include "mpu6050.h"
#include "task.h"

#define IMU_SERVICE_SAMPLE_PERIOD       pdMS_TO_TICKS(10U)
#define IMU_SERVICE_SAMPLE_PHASE_DELAY  pdMS_TO_TICKS(6U)
#define IMU_SERVICE_RESET_WAIT          pdMS_TO_TICKS(100U)
#define IMU_SERVICE_SETTLE_TIME         pdMS_TO_TICKS(500U)
#define IMU_SERVICE_RETRY_PERIOD        pdMS_TO_TICKS(1000U)
#define IMU_SERVICE_CALIBRATION_SAMPLES 300U
#define IMU_SERVICE_MAX_SAMPLE_FAILURES 3U
#define IMU_SERVICE_ACCEL_NORM_MIN_SQ   0.7225f
#define IMU_SERVICE_ACCEL_NORM_MAX_SQ   1.3225f
#define IMU_SERVICE_FILTER_ALPHA        0.6110155f
#define IMU_SERVICE_DEBUG_AUTO_INJECT_FAILURES 0U
#define IMU_SERVICE_DEBUG_AUTO_INJECT_READY_DELAY pdMS_TO_TICKS(5000U)

volatile imu_service_snapshot_t g_imu_service_snapshot;
volatile imu_service_diagnostics_t g_imu_service_diag;
volatile uint32_t g_imu_service_debug_forced_sample_failures;

static TickType_t s_next_action_tick;
static TickType_t s_next_sample_tick;
static int64_t s_gyro_calibration_sum[3];
static uint16_t s_calibration_samples;
static uint8_t s_address;
static uint8_t s_who_am_i;
static float s_gyro_bias_counts[3];
static float s_filtered_dps[3];
static float s_calibration_temperature_c;
static bool s_filter_initialized;
#if IMU_SERVICE_DEBUG_AUTO_INJECT_FAILURES > 0U
static TickType_t s_debug_ready_tick;
static bool s_debug_auto_injected;
#endif

static bool ImuService_TimeReached(TickType_t now, TickType_t target)
{
    return (int32_t) (now - target) >= 0;
}

static void ImuService_SetState(imu_service_state_t state)
{
    if (g_imu_service_diag.state != (uint8_t) state) {
        g_imu_service_diag.state_transition_count++;
#if IMU_SERVICE_DEBUG_AUTO_INJECT_FAILURES > 0U
        if (state == IMU_SERVICE_STATE_READY) {
            s_debug_ready_tick = xTaskGetTickCount();
        }
#endif
    }
    g_imu_service_diag.state = (uint8_t) state;
    g_imu_service_diag.ready =
        (state == IMU_SERVICE_STATE_READY) ? 1U : 0U;
}

static void ImuService_ResetCalibration(void)
{
    memset(s_gyro_calibration_sum, 0, sizeof(s_gyro_calibration_sum));
    s_calibration_samples = 0U;
    s_filter_initialized = false;
}

static bool ImuService_IsCalibrationSampleStationary(
    const mpu6050_raw_sample_t *raw, float accel_norm_sq)
{
    const mpu6050_profile_t *profile =
        Mpu6050_GetProfile(s_who_am_i);
    int16_t gyro_still_counts = (profile != NULL) ?
        (int16_t) profile->gyro_still_counts : 655;

    return (raw->gyro[0] >= -gyro_still_counts) &&
        (raw->gyro[0] <= gyro_still_counts) &&
        (raw->gyro[1] >= -gyro_still_counts) &&
        (raw->gyro[1] <= gyro_still_counts) &&
        (raw->gyro[2] >= -gyro_still_counts) &&
        (raw->gyro[2] <= gyro_still_counts) &&
        (accel_norm_sq >= IMU_SERVICE_ACCEL_NORM_MIN_SQ) &&
        (accel_norm_sq <= IMU_SERVICE_ACCEL_NORM_MAX_SQ);
}

static void ImuService_PublishSnapshot(
    const imu_service_snapshot_t *snapshot)
{
    uint32_t previous_sequence;

    taskENTER_CRITICAL();
    previous_sequence = g_imu_service_snapshot.update_sequence;
    g_imu_service_snapshot.update_sequence = previous_sequence + 1U;
    g_imu_service_snapshot = *snapshot;
    g_imu_service_snapshot.update_sequence = previous_sequence + 2U;
    taskEXIT_CRITICAL();
}

static void ImuService_PublishUnavailableSnapshot(
    imu_service_state_t state)
{
    imu_service_snapshot_t snapshot;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.version = IMU_SERVICE_SNAPSHOT_VERSION;
    snapshot.size_bytes = (uint16_t) sizeof(snapshot);
    snapshot.timestamp_us = BSP_Time_GetUs();
    snapshot.sample_count = g_imu_service_diag.sample_success_count;
    snapshot.calibration_target_samples =
        IMU_SERVICE_CALIBRATION_SAMPLES;
    snapshot.state = (uint8_t) state;
    snapshot.address = s_address;
    snapshot.who_am_i = s_who_am_i;
    snapshot.profile_version = g_mpu6050_diag.profile_version;
    ImuService_PublishSnapshot(&snapshot);
}

static void ImuService_Reinitialize(TickType_t now)
{
    g_imu_service_diag.online = 0U;
    g_imu_service_diag.ready = 0U;
    g_imu_service_diag.reinitialize_count++;
    ImuService_SetState(IMU_SERVICE_STATE_PROBE);
    ImuService_PublishUnavailableSnapshot(IMU_SERVICE_STATE_PROBE);
    s_next_action_tick = now + IMU_SERVICE_RETRY_PERIOD;
    ImuService_ResetCalibration();
}

static void ImuService_ProcessCalibration(
    const mpu6050_raw_sample_t *raw, float accel_norm_sq,
    float temperature_c)
{
    uint8_t axis;

    if (!ImuService_IsCalibrationSampleStationary(raw, accel_norm_sq)) {
        if (s_calibration_samples != 0U) {
            g_imu_service_diag.calibration_restart_count++;
        }
        ImuService_ResetCalibration();
        return;
    }

    for (axis = 0U; axis < 3U; axis++) {
        s_gyro_calibration_sum[axis] += raw->gyro[axis];
    }
    s_calibration_samples++;

    if (s_calibration_samples >= IMU_SERVICE_CALIBRATION_SAMPLES) {
        for (axis = 0U; axis < 3U; axis++) {
            s_gyro_bias_counts[axis] =
                (float) s_gyro_calibration_sum[axis] /
                (float) IMU_SERVICE_CALIBRATION_SAMPLES;
        }
        s_calibration_temperature_c = temperature_c;
        g_imu_service_diag.calibration_complete_count++;
        ImuService_SetState(IMU_SERVICE_STATE_READY);
        s_filter_initialized = false;
    }
}

static void ImuService_ProcessSample(TickType_t now)
{
    mpu6050_raw_sample_t raw;
    imu_service_snapshot_t snapshot;
    float accel_norm_sq = 0.0f;
    float temperature_c;
    bool sample_valid;
    uint8_t axis;

    if (g_imu_service_debug_forced_sample_failures != 0U) {
        g_imu_service_debug_forced_sample_failures--;
        g_imu_service_diag.forced_sample_failure_count++;
        sample_valid = false;
    } else {
        sample_valid = Mpu6050_ReadSample(s_address, &raw);
    }
    if (!sample_valid) {
        g_imu_service_diag.sample_failure_count++;
        g_imu_service_diag.consecutive_sample_failures++;
        if (g_imu_service_diag.consecutive_sample_failures >=
            IMU_SERVICE_MAX_SAMPLE_FAILURES) {
            ImuService_Reinitialize(now);
        }
        return;
    }

    g_imu_service_diag.sample_success_count++;
    g_imu_service_diag.consecutive_sample_failures = 0U;
    g_imu_service_diag.last_sample_tick = (uint32_t) now;
    g_imu_service_diag.last_sample_timestamp_us = BSP_Time_GetUs();

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.version = IMU_SERVICE_SNAPSHOT_VERSION;
    snapshot.size_bytes = (uint16_t) sizeof(snapshot);
    snapshot.timestamp_us = g_imu_service_diag.last_sample_timestamp_us;
    snapshot.sample_count = g_imu_service_diag.sample_success_count;
    snapshot.calibration_target_samples =
        IMU_SERVICE_CALIBRATION_SAMPLES;
    snapshot.state = g_imu_service_diag.state;
    snapshot.address = s_address;
    snapshot.who_am_i = s_who_am_i;
    snapshot.profile_version = g_mpu6050_diag.profile_version;
    snapshot.online = 1U;
    snapshot.valid = 1U;
    temperature_c = Mpu6050_ConvertTemperatureC(
        s_who_am_i, raw.temperature);
    snapshot.temperature_c = temperature_c;
    snapshot.calibration_temperature_c = s_calibration_temperature_c;

    for (axis = 0U; axis < 3U; axis++) {
        snapshot.accel_g[axis] =
            (float) raw.accel[axis] / MPU6050_ACCEL_LSB_PER_G;
        accel_norm_sq += snapshot.accel_g[axis] * snapshot.accel_g[axis];
        snapshot.gyro_raw_dps[axis] =
            (float) raw.gyro[axis] / MPU6050_GYRO_LSB_PER_DPS;
        snapshot.gyro_bias_dps[axis] =
            s_gyro_bias_counts[axis] / MPU6050_GYRO_LSB_PER_DPS;
    }
    snapshot.accel_norm_g = sqrtf(accel_norm_sq);

    if (g_imu_service_diag.state ==
            (uint8_t) IMU_SERVICE_STATE_CALIBRATING) {
        ImuService_ProcessCalibration(&raw, accel_norm_sq, temperature_c);
    }

    snapshot.state = g_imu_service_diag.state;
    snapshot.calibration_samples = s_calibration_samples;
    snapshot.calibrated = g_imu_service_diag.ready;
    for (axis = 0U; axis < 3U; axis++) {
        snapshot.gyro_bias_dps[axis] =
            s_gyro_bias_counts[axis] / MPU6050_GYRO_LSB_PER_DPS;
        snapshot.gyro_dps[axis] =
            ((float) raw.gyro[axis] - s_gyro_bias_counts[axis]) /
            MPU6050_GYRO_LSB_PER_DPS;
        if (g_imu_service_diag.ready == 0U) {
            snapshot.gyro_filtered_dps[axis] =
                snapshot.gyro_dps[axis];
        } else if (!s_filter_initialized) {
            s_filtered_dps[axis] = snapshot.gyro_dps[axis];
            snapshot.gyro_filtered_dps[axis] = s_filtered_dps[axis];
        } else {
            s_filtered_dps[axis] += IMU_SERVICE_FILTER_ALPHA *
                (snapshot.gyro_dps[axis] - s_filtered_dps[axis]);
            snapshot.gyro_filtered_dps[axis] = s_filtered_dps[axis];
        }
    }
    if (g_imu_service_diag.ready != 0U) {
        s_filter_initialized = true;
    }

    ImuService_PublishSnapshot(&snapshot);
}

void ImuService_Init(void)
{
    imu_service_snapshot_t initial;

    memset(&initial, 0, sizeof(initial));
    memset((void *) &g_imu_service_diag, 0,
        sizeof(g_imu_service_diag));
    g_imu_service_debug_forced_sample_failures = 0U;
    memset(s_gyro_bias_counts, 0, sizeof(s_gyro_bias_counts));
    memset(s_filtered_dps, 0, sizeof(s_filtered_dps));
    s_calibration_temperature_c = 0.0f;
#if IMU_SERVICE_DEBUG_AUTO_INJECT_FAILURES > 0U
    s_debug_ready_tick = 0U;
    s_debug_auto_injected = false;
#endif
    Mpu6050_InitDiagnostics();
    ImuService_ResetCalibration();

    initial.version = IMU_SERVICE_SNAPSHOT_VERSION;
    initial.size_bytes = (uint16_t) sizeof(initial);
    initial.calibration_target_samples =
        IMU_SERVICE_CALIBRATION_SAMPLES;
    initial.state = (uint8_t) IMU_SERVICE_STATE_PROBE;
    taskENTER_CRITICAL();
    g_imu_service_snapshot = initial;
    taskEXIT_CRITICAL();

    g_imu_service_diag.initialized = 1U;
    ImuService_SetState(IMU_SERVICE_STATE_PROBE);
    s_next_action_tick = 0U;
    s_next_sample_tick = 0U;
    s_address = 0U;
    s_who_am_i = 0U;
}

void ImuService_Process(TickType_t now)
{
    imu_service_state_t state;

    if (g_imu_service_diag.initialized == 0U) {
        return;
    }
    g_imu_service_diag.process_count++;
    state = (imu_service_state_t) g_imu_service_diag.state;
#if IMU_SERVICE_DEBUG_AUTO_INJECT_FAILURES > 0U
    if (!s_debug_auto_injected &&
        (state == IMU_SERVICE_STATE_READY) &&
        ImuService_TimeReached(now, s_debug_ready_tick +
            IMU_SERVICE_DEBUG_AUTO_INJECT_READY_DELAY)) {
        g_imu_service_debug_forced_sample_failures =
            IMU_SERVICE_DEBUG_AUTO_INJECT_FAILURES;
        s_debug_auto_injected = true;
    }
#endif

    if (state == IMU_SERVICE_STATE_PROBE) {
        if (!ImuService_TimeReached(now, s_next_action_tick)) {
            return;
        }
        if (Mpu6050_Probe(&s_address, &s_who_am_i) &&
            Mpu6050_Reset(s_address)) {
            ImuService_SetState(IMU_SERVICE_STATE_RESET_WAIT);
            s_next_action_tick = now + IMU_SERVICE_RESET_WAIT;
        } else {
            s_next_action_tick = now + IMU_SERVICE_RETRY_PERIOD;
        }
        return;
    }

    if (state == IMU_SERVICE_STATE_RESET_WAIT) {
        if (!ImuService_TimeReached(now, s_next_action_tick)) {
            return;
        }
        if (Mpu6050_Configure(s_address)) {
            g_imu_service_diag.online = 1U;
            ImuService_SetState(IMU_SERVICE_STATE_SETTLING);
            s_next_action_tick = now + IMU_SERVICE_SETTLE_TIME;
            /* Keep the I2C burst away from the 100 Hz UART frame. */
            s_next_sample_tick = now + IMU_SERVICE_SAMPLE_PHASE_DELAY;
        } else {
            ImuService_Reinitialize(now);
        }
        return;
    }

    if ((state == IMU_SERVICE_STATE_SETTLING) &&
        ImuService_TimeReached(now, s_next_action_tick)) {
        ImuService_ResetCalibration();
        ImuService_SetState(IMU_SERVICE_STATE_CALIBRATING);
    }

    if (!ImuService_TimeReached(now, s_next_sample_tick)) {
        return;
    }
    s_next_sample_tick += IMU_SERVICE_SAMPLE_PERIOD;
    if (ImuService_TimeReached(now,
            s_next_sample_tick + IMU_SERVICE_SAMPLE_PERIOD)) {
        s_next_sample_tick = now + IMU_SERVICE_SAMPLE_PERIOD;
    }
    ImuService_ProcessSample(now);
}

bool ImuService_NeedsBusAccess(TickType_t now)
{
    imu_service_state_t state;

    if (g_imu_service_diag.initialized == 0U) {
        return false;
    }
    state = (imu_service_state_t) g_imu_service_diag.state;
    if ((state == IMU_SERVICE_STATE_PROBE) ||
        (state == IMU_SERVICE_STATE_RESET_WAIT)) {
        return ImuService_TimeReached(now, s_next_action_tick);
    }
    return ImuService_TimeReached(now, s_next_sample_tick);
}

bool ImuService_GetSnapshot(imu_service_snapshot_t *snapshot)
{
    if ((snapshot == NULL) || (g_imu_service_diag.initialized == 0U)) {
        return false;
    }

    taskENTER_CRITICAL();
    *snapshot = g_imu_service_snapshot;
    taskEXIT_CRITICAL();
    return (snapshot->version == IMU_SERVICE_SNAPSHOT_VERSION) &&
        ((snapshot->update_sequence & 1U) == 0U);
}

bool ImuService_IsReady(void)
{
    return g_imu_service_diag.ready != 0U;
}

const char *ImuService_StateName(imu_service_state_t state)
{
    switch (state) {
        case IMU_SERVICE_STATE_PROBE:
            return "PROBE";
        case IMU_SERVICE_STATE_RESET_WAIT:
            return "RESET";
        case IMU_SERVICE_STATE_SETTLING:
            return "SETTLE";
        case IMU_SERVICE_STATE_CALIBRATING:
            return "CAL";
        case IMU_SERVICE_STATE_READY:
            return "READY";
        default:
            return "UNKNOWN";
    }
}
