#ifndef ECHO_BSP_I2C_H
#define ECHO_BSP_I2C_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_I2C_MAX_WRITE_BYTES 8U
#define BSP_I2C_MAX_READ_BYTES  16U

typedef enum {
    BSP_I2C_RESULT_OK = 0,
    BSP_I2C_RESULT_NOT_INITIALIZED,
    BSP_I2C_RESULT_INVALID_ARGUMENT,
    BSP_I2C_RESULT_MUTEX_TIMEOUT,
    BSP_I2C_RESULT_BUS_BUSY_TIMEOUT,
    BSP_I2C_RESULT_TRANSFER_TIMEOUT,
    BSP_I2C_RESULT_NACK,
    BSP_I2C_RESULT_ARBITRATION_LOST,
    BSP_I2C_RESULT_FIFO_ERROR
} bsp_i2c_result_t;

typedef struct {
    uint32_t write_attempt_count;
    uint32_t write_success_count;
    uint32_t read_attempt_count;
    uint32_t read_success_count;
    uint32_t nack_count;
    uint32_t arbitration_lost_count;
    uint32_t bus_busy_timeout_count;
    uint32_t transfer_timeout_count;
    uint32_t mutex_timeout_count;
    uint32_t fifo_error_count;
    uint32_t recovery_count;
    uint32_t bus_clear_attempt_count;
    uint32_t bus_clear_success_count;
    uint32_t bus_clear_failure_count;
    uint32_t last_controller_status;
    uint16_t last_length;
    uint16_t last_read_length;
    uint8_t last_address;
    uint8_t initialized;
    uint32_t last_result;
} bsp_i2c_diagnostics_t;

extern volatile bsp_i2c_diagnostics_t g_bsp_i2c_diag;

void BSP_I2C_Init(void);
bsp_i2c_result_t BSP_I2C_Write(
    uint8_t address, const uint8_t *data, uint16_t length);
bsp_i2c_result_t BSP_I2C_WriteRead(uint8_t address,
    const uint8_t *write_data, uint16_t write_length,
    uint8_t *read_data, uint16_t read_length);
const volatile bsp_i2c_diagnostics_t *BSP_I2C_GetDiagnostics(void);

#endif
