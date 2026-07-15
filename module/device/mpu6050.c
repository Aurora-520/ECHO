#include "mpu6050.h"

#include <string.h>

#include "bsp_i2c.h"

#define MPU6050_REG_SMPLRT_DIV   0x19U
#define MPU6050_REG_CONFIG       0x1AU
#define MPU6050_REG_GYRO_CONFIG  0x1BU
#define MPU6050_REG_ACCEL_CONFIG 0x1CU
#define MPU6050_REG_FIFO_EN      0x23U
#define MPU6050_REG_INT_ENABLE   0x38U
#define MPU6050_REG_ACCEL_XOUT_H 0x3BU
#define MPU6050_REG_USER_CTRL    0x6AU
#define MPU6050_REG_PWR_MGMT_1   0x6BU
#define MPU6050_REG_PWR_MGMT_2   0x6CU
#define MPU6050_REG_WHO_AM_I     0x75U

#define MPU6050_PWR_DEVICE_RESET 0x80U
#define MPU6050_PWR_PLL_X_GYRO   0x01U
#define MPU6050_DLPF_42_HZ       0x03U
#define MPU6050_GYRO_FS_500_DPS  0x08U
#define MPU6050_ACCEL_FS_4_G     0x08U
#define MPU6050_SAMPLE_DIVIDER   9U
#define MPU6050_SAMPLE_BYTES     14U

volatile mpu6050_diagnostics_t g_mpu6050_diag;

static const mpu6050_profile_t s_mpu6050_profile = {
    "MPU6050",
    340.0f,
    36.53f,
    1310U,
    MPU6050_WHO_AM_I_MPU6050,
    MPU6050_PROFILE_VERSION
};

static const mpu6050_profile_t s_mpu6500_profile = {
    "MPU6500-COMPAT",
    333.87f,
    21.0f,
    655U,
    MPU6050_WHO_AM_I_MPU6500,
    MPU6050_PROFILE_VERSION
};

static bool Mpu6050_WriteRegister(
    uint8_t address, uint8_t reg, uint8_t value)
{
    uint8_t data[2];
    bsp_i2c_result_t result;

    data[0] = reg;
    data[1] = value;
    result = BSP_I2C_Write(address, data, sizeof(data));
    g_mpu6050_diag.last_i2c_result = (uint32_t) result;
    return result == BSP_I2C_RESULT_OK;
}

static bool Mpu6050_ReadRegisters(uint8_t address, uint8_t reg,
    uint8_t *data, uint16_t length)
{
    bsp_i2c_result_t result =
        BSP_I2C_WriteRead(address, &reg, 1U, data, length);

    g_mpu6050_diag.last_i2c_result = (uint32_t) result;
    return result == BSP_I2C_RESULT_OK;
}

static bool Mpu6050_VerifyRegister(
    uint8_t address, uint8_t reg, uint8_t mask, uint8_t expected)
{
    uint8_t value;

    return Mpu6050_ReadRegisters(address, reg, &value, 1U) &&
        ((value & mask) == expected);
}

static int16_t Mpu6050_DecodeS16(const uint8_t *data)
{
    return (int16_t) ((uint16_t) ((uint16_t) data[0] << 8) |
        (uint16_t) data[1]);
}

void Mpu6050_InitDiagnostics(void)
{
    memset((void *) &g_mpu6050_diag, 0, sizeof(g_mpu6050_diag));
    g_mpu6050_diag.last_i2c_result =
        (uint32_t) BSP_I2C_RESULT_NOT_INITIALIZED;
}

bool Mpu6050_Probe(uint8_t *address, uint8_t *who_am_i)
{
    uint8_t identity = 0U;
    const mpu6050_profile_t *profile;

    if ((address == NULL) || (who_am_i == NULL)) {
        return false;
    }

    g_mpu6050_diag.probe_attempt_count++;
    if (Mpu6050_ReadRegisters(MPU6050_I2C_ADDRESS,
            MPU6050_REG_WHO_AM_I, &identity, 1U)) {
        g_mpu6050_diag.address = MPU6050_I2C_ADDRESS;
        g_mpu6050_diag.who_am_i = identity;
    }
    profile = Mpu6050_GetProfile(identity);
    if ((g_mpu6050_diag.last_i2c_result ==
            (uint32_t) BSP_I2C_RESULT_OK) && (profile != NULL)) {
        *address = MPU6050_I2C_ADDRESS;
        *who_am_i = identity;
        g_mpu6050_diag.profile_version = profile->version;
        g_mpu6050_diag.probe_success_count++;
        return true;
    }
    return false;
}

bool Mpu6050_Reset(uint8_t address)
{
    if (!Mpu6050_WriteRegister(
            address, MPU6050_REG_PWR_MGMT_1, MPU6050_PWR_DEVICE_RESET)) {
        return false;
    }
    g_mpu6050_diag.reset_count++;
    g_mpu6050_diag.configured = 0U;
    return true;
}

bool Mpu6050_Configure(uint8_t address)
{
    bool success;

    g_mpu6050_diag.configure_attempt_count++;
    success = Mpu6050_WriteRegister(address, MPU6050_REG_PWR_MGMT_1,
                  MPU6050_PWR_PLL_X_GYRO) &&
        Mpu6050_WriteRegister(address, MPU6050_REG_PWR_MGMT_2, 0x00U) &&
        Mpu6050_WriteRegister(address, MPU6050_REG_SMPLRT_DIV,
            MPU6050_SAMPLE_DIVIDER) &&
        Mpu6050_WriteRegister(
            address, MPU6050_REG_CONFIG, MPU6050_DLPF_42_HZ) &&
        Mpu6050_WriteRegister(
            address, MPU6050_REG_GYRO_CONFIG, MPU6050_GYRO_FS_500_DPS) &&
        Mpu6050_WriteRegister(
            address, MPU6050_REG_ACCEL_CONFIG, MPU6050_ACCEL_FS_4_G) &&
        Mpu6050_WriteRegister(address, MPU6050_REG_FIFO_EN, 0x00U) &&
        Mpu6050_WriteRegister(address, MPU6050_REG_INT_ENABLE, 0x00U) &&
        Mpu6050_WriteRegister(address, MPU6050_REG_USER_CTRL, 0x00U) &&
        Mpu6050_VerifyRegister(address, MPU6050_REG_PWR_MGMT_1,
            0x7FU, MPU6050_PWR_PLL_X_GYRO) &&
        Mpu6050_VerifyRegister(address, MPU6050_REG_SMPLRT_DIV,
            0xFFU, MPU6050_SAMPLE_DIVIDER) &&
        Mpu6050_VerifyRegister(address, MPU6050_REG_CONFIG,
            0x07U, MPU6050_DLPF_42_HZ) &&
        Mpu6050_VerifyRegister(address, MPU6050_REG_GYRO_CONFIG,
            0x18U, MPU6050_GYRO_FS_500_DPS) &&
        Mpu6050_VerifyRegister(address, MPU6050_REG_ACCEL_CONFIG,
            0x18U, MPU6050_ACCEL_FS_4_G);

    if (success) {
        g_mpu6050_diag.configure_success_count++;
        g_mpu6050_diag.configured = 1U;
    } else {
        g_mpu6050_diag.configure_failure_count++;
        g_mpu6050_diag.configured = 0U;
    }
    return success;
}

bool Mpu6050_ReadSample(uint8_t address, mpu6050_raw_sample_t *sample)
{
    uint8_t data[MPU6050_SAMPLE_BYTES];

    g_mpu6050_diag.sample_attempt_count++;
    if ((sample == NULL) ||
        !Mpu6050_ReadRegisters(address, MPU6050_REG_ACCEL_XOUT_H,
            data, sizeof(data))) {
        g_mpu6050_diag.sample_failure_count++;
        return false;
    }

    sample->accel[0] = Mpu6050_DecodeS16(&data[0]);
    sample->accel[1] = Mpu6050_DecodeS16(&data[2]);
    sample->accel[2] = Mpu6050_DecodeS16(&data[4]);
    sample->temperature = Mpu6050_DecodeS16(&data[6]);
    sample->gyro[0] = Mpu6050_DecodeS16(&data[8]);
    sample->gyro[1] = Mpu6050_DecodeS16(&data[10]);
    sample->gyro[2] = Mpu6050_DecodeS16(&data[12]);
    g_mpu6050_diag.sample_success_count++;
    return true;
}

const char *Mpu6050_DeviceName(uint8_t who_am_i)
{
    const mpu6050_profile_t *profile = Mpu6050_GetProfile(who_am_i);

    return (profile != NULL) ? profile->name : "UNSUPPORTED";
}

const mpu6050_profile_t *Mpu6050_GetProfile(uint8_t who_am_i)
{
    if (who_am_i == MPU6050_WHO_AM_I_MPU6050) {
        return &s_mpu6050_profile;
    }
    if (who_am_i == MPU6050_WHO_AM_I_MPU6500) {
        return &s_mpu6500_profile;
    }
    return NULL;
}

float Mpu6050_ConvertTemperatureC(uint8_t who_am_i, int16_t raw)
{
    const mpu6050_profile_t *profile = Mpu6050_GetProfile(who_am_i);

    if (profile == NULL) {
        return 0.0f;
    }
    return ((float) raw / profile->temperature_lsb_per_c) +
        profile->temperature_offset_c;
}
