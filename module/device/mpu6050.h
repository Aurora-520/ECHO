#ifndef ECHO_MPU6050_H
#define ECHO_MPU6050_H

#include <stdbool.h>
#include <stdint.h>

#define MPU6050_ADDRESS_AD0_LOW  0x68U
#define MPU6050_ADDRESS_AD0_HIGH 0x69U
#define MPU6050_I2C_ADDRESS      MPU6050_ADDRESS_AD0_LOW
#define MPU6050_WHO_AM_I_MPU6050 0x68U
#define MPU6050_WHO_AM_I_MPU6500 0x70U
#define MPU6050_SAMPLE_RATE_HZ   100U
#define MPU6050_GYRO_LSB_PER_DPS 65.5f
#define MPU6050_ACCEL_LSB_PER_G  8192.0f
#define MPU6050_PROFILE_VERSION  1U

typedef struct {
    const char *name;
    float temperature_lsb_per_c;
    float temperature_offset_c;
    uint16_t gyro_still_counts;
    uint8_t who_am_i;
    uint8_t version;
} mpu6050_profile_t;

typedef struct {
    int16_t accel[3];
    int16_t temperature;
    int16_t gyro[3];
} mpu6050_raw_sample_t;

typedef struct {
    uint32_t probe_attempt_count;
    uint32_t probe_success_count;
    uint32_t reset_count;
    uint32_t configure_attempt_count;
    uint32_t configure_success_count;
    uint32_t configure_failure_count;
    uint32_t sample_attempt_count;
    uint32_t sample_success_count;
    uint32_t sample_failure_count;
    uint32_t last_i2c_result;
    uint8_t address;
    uint8_t who_am_i;
    uint8_t configured;
    uint8_t profile_version;
} mpu6050_diagnostics_t;

extern volatile mpu6050_diagnostics_t g_mpu6050_diag;

void Mpu6050_InitDiagnostics(void);
bool Mpu6050_Probe(uint8_t *address, uint8_t *who_am_i);
bool Mpu6050_Reset(uint8_t address);
bool Mpu6050_Configure(uint8_t address);
bool Mpu6050_ReadSample(uint8_t address, mpu6050_raw_sample_t *sample);
const mpu6050_profile_t *Mpu6050_GetProfile(uint8_t who_am_i);
float Mpu6050_ConvertTemperatureC(uint8_t who_am_i, int16_t raw);
const char *Mpu6050_DeviceName(uint8_t who_am_i);

#endif
